#pragma once
#include <Arduino.h>

void ir_begin();
void ir_sendNEC(uint32_t code);

// Vizio V505-G9 NEC codes (V15)
//
// IRremoteESP8266 sendNEC transmits the 32-bit value MSB-first, but the
// TV's NEC decoder reads the bit stream LSB-first per byte. The result is
// that the TV sees bit-reversed bytes. All codes therefore use the 0x20DF
// address (device=0x20, ~device=0xDF on the wire → TV decodes as 0x04, 0xFB).
// Command bytes are the bit-reversals of the XRT136 function codes.
//
// Verified pattern: Power=0x10→bit_rev=0x08 ✓, VolUp=0x40→0x02 ✓, OK=0x22→0x44 ✓
#define TV_POWER      0x20DF10EF  // XRT136 fn 0x08
#define TV_INPUT      0x20DFF40B  // XRT136 fn 0x2F (was 0x04FB2FD0 — broken)
#define TV_VOL_UP     0x20DF40BF  // XRT136 fn 0x02
#define TV_VOL_DOWN   0x20DFC03F  // XRT136 fn 0x03
#define TV_MUTE       0x20DF906F  // XRT136 fn 0x09
#define TV_CH_UP      0x20DF00FF  // XRT136 fn 0x00
#define TV_CH_DOWN    0x20DF807F  // XRT136 fn 0x01
#define TV_UP         0x20DFA25D  // XRT136 fn 0x45 (was 0x04FB45BA — broken)
#define TV_DOWN       0x20DF629D  // XRT136 fn 0x46 (was 0x04FB46B9 — broken)
#define TV_LEFT       0x20DFE21D  // XRT136 fn 0x47 (was 0x04FB47B8 — broken)
#define TV_RIGHT      0x20DF12ED  // XRT136 fn 0x48 (was 0x04FB48B7 — broken)
#define TV_OK         0x20DF22DD  // XRT136 fn 0x44
#define TV_BACK       0x20DF52AD  // XRT136 fn 0x4A (was 0x04FB4AB5 — broken)
#define TV_MENU       0x20DFF20D  // XRT136 fn 0x4F (was 0x04FB4FB0 — broken)
#define TV_HOME       0x20DFB44B  // XRT136 fn 0x2D (was 0x04FB2DD2 — broken)
#define TV_PLAY       0x20DF04FB  // XRT136 fn 0x20
