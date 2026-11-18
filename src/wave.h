// Waveform tables and sample-clock planning for the AD7302 stream.
// Pure C (no Pico SDK), so test/host_test.c can check it on the Mac.
//
// One period of the waveform is stored as a table of n codes. The DMA replays it forever,
// and the PIO clock divider sets the sample rate. Then f = fs / n, with
// fs = clk_sys / (WAVE_PIO_CYCLES * div), and div in 1/256 steps.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WAVE_PIO_CYCLES 8u        // PIO cycles per sample; must match src/ad7302.pio
#define WAVE_FS_MAX 1000000.0     // max sample rate. AD7302 settling: 1.2 us typ / 2 us max full-scale
#define WAVE_N_MAX 4096u          // longest table (one byte per sample)
#define WAVE_F_MIN 0.1
#define WAVE_F_MAX 100000.0
#define WAVE_VDD 3.3f             // AD7302 VDD = REFIN = Pico 3V3, so Vout = VDD * code / 256

typedef enum { SHAPE_SINE, SHAPE_SQUARE, SHAPE_TRI, SHAPE_SAW, SHAPE_RAMP, SHAPE_DC } shape_t;

typedef struct {
    uint32_t n;       // samples per period
    uint32_t div256;  // PIO clock divider, 8.8 fixed point (256 = divide by 1)
    double fs;        // actual sample rate, Hz
    double f;         // actual output frequency, Hz
} wave_plan_t;

// Chooses the table length and divider for frequency f.
// n_fixed = 0 picks the longest table (up to WAVE_N_MAX) that keeps fs <= WAVE_FS_MAX.
// Returns NULL on success, or a short error message.
const char *wave_plan(double f, uint32_t sys_hz, uint32_t n_fixed, wave_plan_t *out);

// Fills t[0..n-1] with one period.
//   amp:    0..1, fraction of full swing
//   offset: -1..1, fraction of half-scale
//   duty:   0..1, square only
// SHAPE_RAMP ignores amp and offset and steps through every code once (needs n = 256).
void wave_fill(uint8_t *t, uint32_t n, shape_t shape, float amp, float offset, float duty);

const char *wave_shape_name(shape_t s);
static inline float wave_code_volts(unsigned code) { return WAVE_VDD * (float)code / 256.0f; }
