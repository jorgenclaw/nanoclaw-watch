/* The Clear BSD License
 *
 * Copyright (c) 2025 EdgeImpulse Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the disclaimer
 * below) provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 *   * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY
 * THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "../ei_classifier_porting.h"
#if EI_PORTING_ESPRESSIF == 1

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstring>
// Include FreeRTOS for delay
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// for millis and micros
#include "esp_timer.h"
#include "esp_idf_version.h"

// memory handling
#include "esp_heap_caps.h"
#include "multi_heap.h"
#include <string.h>

// NanoClaw watch customization: route all EI allocations through a
// private 1.5 MB PSRAM heap registered before LilyGoLib boots, so the
// EI runtime never competes with the display/LVGL/SPI DMA for memory.
// See src/wake_word.cpp for how the heap is set up.
extern "C" {
    extern multi_heap_handle_t g_ei_heap;
}

#define EI_WEAK_FN __attribute__((weak))

EI_WEAK_FN EI_IMPULSE_ERROR ei_run_impulse_check_canceled() {
    return EI_IMPULSE_OK;
}

EI_WEAK_FN EI_IMPULSE_ERROR ei_sleep(int32_t time_ms) {
    vTaskDelay(time_ms / portTICK_RATE_MS);
    return EI_IMPULSE_OK;
}

uint64_t ei_read_timer_ms() {
    return esp_timer_get_time()/1000;
}

uint64_t ei_read_timer_us() {
    return esp_timer_get_time();
}

void ei_putchar(char c)
{
    /* Send char to serial output */
    putchar(c);
}

/**
 *  Printf function uses vsnprintf and output using USB Serial
 */
__attribute__((weak)) void ei_printf(const char *format, ...) {
    static char print_buf[1024] = { 0 };

    va_list args;
    va_start(args, format);
    int r = vsnprintf(print_buf, sizeof(print_buf), format, args);
    va_end(args);

    if (r > 0) {
       printf(print_buf);
    }
}

__attribute__((weak)) void ei_printf_float(float f) {
    ei_printf("%f", f);
}

// we use alligned alloc instead of regular malloc
// due to https://github.com/espressif/esp-nn/issues/7
//
// NanoClaw watch customization: the EI tensor arena + DSP scratch together
// exceed the ~250 KB on-chip DRAM heap budget on the T-Watch S3. The
// unmodified version used MALLOC_CAP_DEFAULT (internal DRAM only) and
// returned NULL on DSP scratch allocation, causing EIDSP_OUT_OF_MEM on the
// very first inference. Prefer PSRAM (8 MB on T-Watch S3), fall back to
// internal DRAM if PSRAM is unavailable or exhausted.
__attribute__((weak)) void *ei_malloc(size_t size) {
    if (g_ei_heap) {
        void *p = multi_heap_aligned_alloc(g_ei_heap, size, 16);
        if (!p) {
            printf("[ei_malloc] FAIL size=%u  ei_heap_free=%u\n",
                   (unsigned)size,
                   (unsigned)multi_heap_free_size(g_ei_heap));
        }
        return p;
    }
    // Fallback (should never happen — wake_word_preallocate runs first):
    return malloc(size);
}

__attribute__((weak)) void *ei_calloc(size_t nitems, size_t size) {
    size_t total = nitems * size;
    if (g_ei_heap) {
        void *p = multi_heap_aligned_alloc(g_ei_heap, total, 16);
        if (p) {
            memset(p, 0, total);
        } else {
            printf("[ei_calloc] FAIL n=%u sz=%u  ei_heap_free=%u\n",
                   (unsigned)nitems, (unsigned)size,
                   (unsigned)multi_heap_free_size(g_ei_heap));
        }
        return p;
    }
    return calloc(nitems, size);
}

__attribute__((weak)) void ei_free(void *ptr) {
    if (!ptr) return;
    if (g_ei_heap) {
        multi_heap_free(g_ei_heap, ptr);
        return;
    }
    free(ptr);
}

#if defined(__cplusplus) && EI_C_LINKAGE == 1
extern "C"
#endif
__attribute__((weak)) void DebugLog(const char* s) {
    ei_printf("%s", s);
}

#endif // EI_PORTING_ESPRESSIF == 1
