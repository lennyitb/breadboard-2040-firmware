#include "wave.h"

#include <math.h>
#include <stddef.h>

const char *wave_plan(double f, uint32_t sys_hz, uint32_t n_fixed, wave_plan_t *out) {
    if (!(f >= WAVE_F_MIN) || f > WAVE_F_MAX) return "f out of range";

    uint32_t n = n_fixed;
    if (!n) {
        double fit = WAVE_FS_MAX / f;
        n = fit >= WAVE_N_MAX ? WAVE_N_MAX : (uint32_t)fit;
        if (n < 2) return "f too high";
    }
    if ((double)n * f > WAVE_FS_MAX * 1.0001) return "f too high for this shape";

    double div = (double)sys_hz / (WAVE_PIO_CYCLES * f * n);
    double div256 = floor(div * 256.0 + 0.5);
    if (div256 < 256.0) return "f too high";
    if (div256 > 65535.0 * 256.0 + 255.0) return "f too low for this shape";

    out->n = n;
    out->div256 = (uint32_t)div256;
    out->fs = (double)sys_hz * 256.0 / (WAVE_PIO_CYCLES * div256);
    out->f = out->fs / n;
    return NULL;
}

static uint8_t to_code(float v) {   // v: -1..1 maps onto 0..255, clipped
    float c = 127.5f + 127.5f * v;
    if (c < 0.0f) c = 0.0f;
    if (c > 255.0f) c = 255.0f;
    return (uint8_t)(c + 0.5f);
}

void wave_fill(uint8_t *t, uint32_t n, shape_t shape, float amp, float offset, float duty) {
    for (uint32_t i = 0; i < n; i++) {
        float x = (float)i / (float)n;   // phase, 0..1
        float s;
        switch (shape) {
        case SHAPE_SINE:   s = sinf(6.2831853f * x); break;
        case SHAPE_SQUARE: s = x < duty ? 1.0f : -1.0f; break;
        case SHAPE_TRI:    s = 1.0f - 4.0f * fabsf(x - 0.5f); break;   // -1 -> +1 at half period -> -1
        case SHAPE_SAW:    s = 2.0f * x - 1.0f; break;
        case SHAPE_RAMP:   t[i] = (uint8_t)((i * 256u) / n); continue;
        default:           s = 0.0f; break;
        }
        t[i] = to_code(offset + amp * s);
    }
}

const char *wave_shape_name(shape_t s) {
    static const char *const names[] = {"sine", "square", "tri", "saw", "ramp", "dc"};
    return (unsigned)s < sizeof names / sizeof names[0] ? names[s] : "?";
}
