#include "ir_remote.h"
#include <IRsend.h>

#ifndef IR_SEND
#define IR_SEND 2  // GPIO2 — T-Watch S3 IR LED
#endif

static IRsend irsend(IR_SEND);
static bool initialized = false;

void ir_begin() {
    if (!initialized) {
        irsend.begin();
        initialized = true;
        Serial.println("[ir] initialized on GPIO2");
    }
}

void ir_sendNEC(uint32_t code) {
    ir_begin();
    irsend.sendNEC(code, kNECBits, 1);
    Serial.printf("[ir] sent NEC 0x%08X\n", code);
}
