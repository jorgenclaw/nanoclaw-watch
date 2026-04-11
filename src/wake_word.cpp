#include "wake_word.h"
#include "config.h"
#include "state.h"

#include <LilyGoLib.h>
#include <Jorgenclaw-project-1_inferencing.h>
#include <esp_heap_caps.h>
#include <multi_heap.h>

// =============================================================================
// Private EI heap
// =============================================================================
// LilyGoLib's display + LVGL init grabs nearly all of the 8 MB PSRAM during
// instance.begin() — by the time the wake-word task runs, only ~130 KB of
// PSRAM is free, which is not enough for the 213 KB EI tensor arena, plus
// scratch + overflow + DSP buffers (~500 KB total).
//
// To work around this we carve a 1.5 MB region out of PSRAM as the very
// first thing in setup(), before LilyGoLib touches anything, and register
// it as a separate multi_heap. The patched ei_malloc/ei_calloc in
// porting/espressif/ei_classifier_porting.cpp routes ALL EI allocations
// through this private heap, leaving the system PSRAM untouched for
// LilyGoLib's framebuffers, SPI DMA, etc.

extern "C" {
    multi_heap_handle_t g_ei_heap = nullptr;
}

// Total private heap size. Empirically observed via serial logs the
// EON-compiled model consumes ~2.08 MB during run_classifier_init:
//   tensor arena               ~ 213 KB
//   overflow persistent buffers ~ 1.87 MB (the model's tensor_arena is
//                                          undersized, so most ops spill
//                                          to AllocatePersistentBufferImpl
//                                          which calls ei_calloc)
//   DSP MFE scratch (per call) ~  16 KB
//   feature/slice buffers      ~  24 KB
//   multi_heap block headers   ~  ~30 KB at this allocation count
// 3 MB leaves ~900 KB headroom for DSP scratch + safety. LilyGoLib uses
// most of the rest of the 8 MB PSRAM for its display + LVGL buffers.
static const size_t kEiHeapSize = 3 * 1024 * 1024;

void wake_word_preallocate() {
    void *region = heap_caps_aligned_alloc(
        16, kEiHeapSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!region) {
        Serial.printf("[ww] heap preallocate FAILED size=%u  free_psram=%u\n",
                      (unsigned)kEiHeapSize,
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return;
    }
    g_ei_heap = multi_heap_register(region, kEiHeapSize);
    if (!g_ei_heap) {
        Serial.println("[ww] multi_heap_register FAILED");
        return;
    }
    Serial.printf("[ww] private EI heap: %u bytes at %p (free=%u)\n",
                  (unsigned)kEiHeapSize, region,
                  (unsigned)multi_heap_free_size(g_ei_heap));
}

// =============================================================================
// Wake word ("Hey Jorgenclaw") — implementation notes
// =============================================================================
//
// Model: Edge Impulse continuous-mode classifier, 16 kHz mono, 1 s window,
// 4 slices per window (250 ms per slice), 3 labels:
//   0 = "hey_jorgenclaw"   ← the one we care about
//   1 = "noise"
//   2 = "unknown"
//
// Mic sharing strategy:
//   We DO NOT initialize a second I2S peripheral. The T-Watch S3 has one
//   physical PDM mic wired to fixed pins, and the LilyGoLib `instance.mic`
//   I2SClass object is already continuously DMA-buffering from it. We just
//   call `instance.mic.readBytes()` from this task when the watch is NOT
//   actively recording.
//
//   When the user triggers a recording (tap blue button, or this task fires
//   a wake word trigger), state transitions to STATE_RECORDING and the
//   recording loop in doVoiceCapture() starts reading the same mic. Our
//   task yields as soon as it sees that state change. When recording
//   completes and state returns to STATE_HOME, we resume — and call
//   run_classifier_init() to reset the continuous classifier's internal
//   buffer state, because it expects an uninterrupted sample stream.

// --- Tuning ------------------------------------------------------------------

// Threshold for "hey_jorgenclaw" label score. Raise to reduce false
// positives; lower to catch more quiet triggers. The model was trained
// on audio from a different mic so wake scores peak around 0.40-0.50
// on T-Watch PDM audio instead of the typical 0.90+. We require TWO
// consecutive windows above threshold to suppress false positives from
// the lower threshold.
#define WAKE_WORD_THRESHOLD     0.40f

// Task stack — Edge Impulse inference needs plenty of headroom.
#define WAKE_WORD_STACK_BYTES   16384

// Task priority (1 = low — main loop + LVGL run higher on core 1).
#define WAKE_WORD_PRIORITY      1

// Full 1-second window in samples. The non-continuous run_classifier
// API processes a full window per call — simpler and less stateful than
// the continuous API (which had a "results never change" bug that we
// couldn't root-cause quickly).
#define WINDOW_SAMPLES  (EI_CLASSIFIER_RAW_SAMPLE_COUNT)    // 16000
// How much we advance the window between classifications. 4000 samples
// = 250 ms stride, 4 classifications per second, matching the original
// slice-based design.
#define STRIDE_SAMPLES  (EI_CLASSIFIER_RAW_SAMPLE_COUNT / 4)  // 4000

// --- Shared state ------------------------------------------------------------

static volatile bool g_triggered = false;

// Rolling window of raw int16 audio (1 second @ 16 kHz = 32 KB) plus a
// matching float buffer for the EI signal callback. Both in the private
// EI heap, allocated in wake_word_task_start().
static int16_t* g_window_i16 = nullptr;  // 16000 int16 = 32 KB
static float*   g_features   = nullptr;  // 16000 float = 64 KB

// --- EI signal callback ------------------------------------------------------
// The EI SDK pulls feature samples through this callback while DSP is
// running. We've already converted the current window into g_features,
// so this is just a copy.

static int get_signal_data(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = g_features[offset + i];
    }
    return EIDSP_OK;
}

// --- Public API --------------------------------------------------------------

bool wake_word_triggered() {
    if (g_triggered) {
        g_triggered = false;
        return true;
    }
    return false;
}

// --- Task --------------------------------------------------------------------

static void wake_word_task(void* param) {
    Serial.printf("[ww] task started, WINDOW=%d STRIDE=%d thresh=%.2f\n",
                  WINDOW_SAMPLES, STRIDE_SAMPLES, WAKE_WORD_THRESHOLD);
    Serial.printf("[ww] heap: ei=%u psram=%u internal=%u\n",
                  (unsigned)(g_ei_heap ? multi_heap_free_size(g_ei_heap) : 0),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    // Prime the 1-second rolling window with fresh audio before the
    // first classification. Otherwise the first window is half-silence.
    {
        size_t total = 0;
        while (total < WINDOW_SAMPLES) {
            size_t got = instance.mic.readBytes(
                (char*)(g_window_i16 + total),
                (WINDOW_SAMPLES - total) * sizeof(int16_t));
            total += got / sizeof(int16_t);
        }
        Serial.println("[ww] window primed");
    }

    WatchState last_state = currentState();
    uint32_t iter = 0;

    while (true) {
        WatchState s = currentState();

        // Pause while the recording pipeline owns the mic.
        if (s != STATE_HOME) {
            if (last_state == STATE_HOME) {
                Serial.printf("[ww] pausing (state %d)\n", (int)s);
            }
            last_state = s;
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // Resuming from a non-HOME state — re-prime the window.
        if (last_state != STATE_HOME) {
            Serial.println("[ww] resuming — re-priming window");
            size_t total = 0;
            while (total < WINDOW_SAMPLES) {
                size_t got = instance.mic.readBytes(
                    (char*)(g_window_i16 + total),
                    (WINDOW_SAMPLES - total) * sizeof(int16_t));
                total += got / sizeof(int16_t);
            }
            last_state = s;
        }

        // Slide the window left by STRIDE_SAMPLES and read STRIDE_SAMPLES
        // of fresh audio into the tail. This gives a 250 ms hop with 75 %
        // overlap between consecutive classifications.
        memmove(g_window_i16,
                g_window_i16 + STRIDE_SAMPLES,
                (WINDOW_SAMPLES - STRIDE_SAMPLES) * sizeof(int16_t));

        int16_t* tail = g_window_i16 + (WINDOW_SAMPLES - STRIDE_SAMPLES);
        size_t tail_filled = 0;
        bool bail = false;
        while (tail_filled < STRIDE_SAMPLES) {
            if (currentState() != STATE_HOME) { bail = true; break; }
            size_t got = instance.mic.readBytes(
                (char*)(tail + tail_filled),
                (STRIDE_SAMPLES - tail_filled) * sizeof(int16_t));
            tail_filled += got / sizeof(int16_t);
        }
        if (bail) continue;

        // Pass 1: compute mean (for DC offset removal). The T-Watch PDM
        // mic has a consistent ~-1300 DC bias — if we don't remove it,
        // the model sees a DC-shifted signal that doesn't match its
        // training distribution.
        int64_t mean_sum = 0;
        for (int i = 0; i < WINDOW_SAMPLES; i++) {
            mean_sum += g_window_i16[i];
        }
        int16_t dc_offset = (int16_t)(mean_sum / WINDOW_SAMPLES);

        // Pass 2: DC-block + amplify + convert to float + stats.
        // IMPORTANT: EI's numpy::int16_to_float just casts int16 -> float
        // without dividing by 32768. The model was trained on raw int16
        // magnitudes cast to float, NOT normalized to [-1, 1].
        // The T-Watch PDM mic outputs ~1/4 the level of a phone/laptop
        // mic, so we apply a 4x software gain to bring the signal range
        // into the model's training distribution.
        const int kGain = 8;
        int64_t sumsq = 0;
        int16_t minv = 32767, maxv = -32768;
        for (int i = 0; i < WINDOW_SAMPLES; i++) {
            int32_t centered = (int32_t)g_window_i16[i] - dc_offset;
            int32_t amped = centered * kGain;
            // Clip to int16 range so features don't overflow.
            if (amped > 32767)  amped = 32767;
            if (amped < -32768) amped = -32768;
            int16_t v = (int16_t)amped;
            g_features[i] = (float)v;
            sumsq += (int32_t)v * v;
            if (v < minv) minv = v;
            if (v > maxv) maxv = v;
        }
        float rms = sqrtf((float)(sumsq / WINDOW_SAMPLES));

        // Classify the full 1-second window.
        signal_t signal;
        signal.total_length = WINDOW_SAMPLES;
        signal.get_data     = &get_signal_data;

        ei_impulse_result_t result;
        EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
        if (err != EI_IMPULSE_OK) {
            Serial.printf("[ww] classifier error %d\n", (int)err);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        float wake_score    = result.classification[0].value;
        float noise_score   = result.classification[1].value;
        float unknown_score = result.classification[2].value;

        // Heartbeat every 20 iterations (~5 sec) — quieter now that
        // the feature is working. Bump this back up to 4 if diagnosing
        // a regression.
        if (++iter % 20 == 0) {
            Serial.printf("[ww] wake=%.2f n=%.2f u=%.2f  rms=%.0f min=%d max=%d dc=%d\n",
                          wake_score, noise_score, unknown_score,
                          rms, (int)minv, (int)maxv, (int)dc_offset);
        }

        // Consensus: require two consecutive windows above threshold
        // before firing. Since our windows overlap 75 %, consecutive
        // high scores on the same utterance are a strong signal, while
        // isolated high scores on other sounds get filtered out.
        static int consecutive_high = 0;
        if (wake_score >= WAKE_WORD_THRESHOLD) {
            consecutive_high++;
            if (consecutive_high >= 2) {
                Serial.printf("[ww] HEY JORGENCLAW %.2f  (n=%.2f u=%.2f)\n",
                              wake_score, noise_score, unknown_score);
                g_triggered = true;
                consecutive_high = 0;
                vTaskDelay(1500 / portTICK_PERIOD_MS);
            }
        } else {
            consecutive_high = 0;
        }
    }
}

void wake_word_task_start() {
    // Rolling 1-second int16 window (32 KB) + float feature buffer (64 KB),
    // both from the private EI heap. Total 96 KB — the 3 MB EI heap has
    // plenty of room after the tensor arena.
    if (g_ei_heap) {
        g_window_i16 = (int16_t*)multi_heap_malloc(g_ei_heap, WINDOW_SAMPLES * sizeof(int16_t));
        g_features   = (float*)multi_heap_malloc(g_ei_heap, WINDOW_SAMPLES * sizeof(float));
    }
    if (!g_window_i16 || !g_features) {
        Serial.println("[ww] ERROR: alloc for audio buffers failed");
        return;
    }

    BaseType_t rc = xTaskCreatePinnedToCore(
        wake_word_task,
        "wake_word",
        WAKE_WORD_STACK_BYTES,
        NULL,
        WAKE_WORD_PRIORITY,
        NULL,
        0   // Core 0 — Arduino loop + LVGL run on core 1
    );
    if (rc != pdPASS) {
        Serial.println("[ww] ERROR: task creation failed");
    }
}
