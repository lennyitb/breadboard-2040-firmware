// Breadboard AWG proof of concept: Pico W -> AD7302 8-bit parallel DAC (VOUTA) -> AWG Scaler DAC1_IN.
// USB serial text commands (see cmd.h / README.md). The DAC boots at mid-scale (1.65 V).
#include <stdio.h>

#include "pico/binary_info.h"
#include "pico/stdlib.h"

#include "cmd.h"
#include "dac.h"

// Shown by `picotool info -p`: a quick way to check the breadboard wiring against the firmware.
bi_decl(bi_pin_mask_with_names(0xffu << DAC_PIN_D0, "DB0|DB1|DB2|DB3|DB4|DB5|DB6|DB7"));
bi_decl(bi_1pin_with_name(DAC_PIN_WR, "AD7302 /WR"));
bi_decl(bi_1pin_with_name(DAC_PIN_AB, "AD7302 A/B (low = DAC A)"));
bi_decl(bi_1pin_with_name(DAC_PIN_CLR, "AD7302 /CLR"));
bi_decl(bi_pin_mask_with_names(0xfu << RELAY_PIN_K1, "K1 DAC/EXT|K2 OFFSET5/3|K3 SCALE3/5|K4 OUT/MUTE"));

int main(void) {
    dac_init();   // first, so the bus and relays reach a known state immediately
    cmd_init();
    stdio_init_all();

    char line[128];
    size_t n = 0;
    for (;;) {
        int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == '\r' || c == '\n') {
            if (!n) continue;
            line[n] = 0;
            n = 0;
            char reply[192];
            cmd_exec(line, reply, sizeof reply);
            printf("%s\n", reply);
        } else if (c == '\b' || c == 0x7f) {
            if (n) n--;
        } else if (n < sizeof line - 1) {
            line[n++] = (char)c;
        }
    }
}
