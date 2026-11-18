// AD7302 and relay hardware. The pinout follows awg-control/awg-control.kicad_sch.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DAC_PIN_D0    0   // GP0..GP7 -> DB0..DB7 (AD7302 pins 8..1), consecutive for PIO `out`
#define DAC_PIN_WR    8   // GP8  -> /WR  (pin 10), PIO side-set
#define DAC_PIN_AB    9   // GP9  -> A/B  (pin 11). LOW selects DAC A: the datasheet pin is "Ā/B"
                          //        (the KiCad symbol label "A/~B" has the bar on the wrong letter)
#define DAC_PIN_CLR  10   // GP10 -> /CLR (pin 14). R10 pulls it up to 3V3
                          // /CS, /LDAC -> GND. /PD, VDD, REFIN -> 3V3 (internal VDD/2 ref => 0..VDD out)
#define RELAY_PIN_K1 16   // GP16..GP19 -> 1k -> Q1..Q4 (2N4401) -> K1..K4 coils (+12 V)
#define RELAY_COUNT   4

void dac_init(void);                 // pins, PIO, DMA. Output = mid-scale, relays off
void dac_stop(void);                 // stop the waveform. VOUTA holds its last code
void dac_hold(uint8_t code);         // stop the waveform and latch a single code
// Loop table[0..n-1] forever at fs = clk_sys / (8 * div256/256). The table must stay
// valid and unchanged until the next dac_stop/dac_hold/dac_play.
void dac_play(const uint8_t *table, uint32_t n, uint32_t div256);
void dac_clear(void);                // stop, then pulse /CLR: both DAC registers -> 0 (0 V)
uint32_t dac_sys_hz(void);

void relay_set(unsigned k, bool on); // k = 1..4
bool relay_get(unsigned k);
