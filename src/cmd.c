#include "cmd.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "dac.h"
#include "wave.h"

typedef struct {
    shape_t shape;
    float f;           // Hz (requested)
    float amp_pct;     // 0..100, % of full swing
    float offset_pct;  // -100..100, % of half-scale
    float duty_pct;    // 1..99, square only
    unsigned code;     // SHAPE_DC
    bool on;           // false: hold mid-scale
} out_state_t;

static out_state_t s_out = {SHAPE_SINE, 1000.0f, 100.0f, 0.0f, 50.0f, 128, false};
static wave_plan_t s_plan;            // valid while a table is playing
static uint8_t s_table[WAVE_N_MAX];   // owned here and replayed by the DMA

static const char *const HELP =
    "sine|square|tri|saw [Hz], ramp [Hz], dc <0-255>, f <Hz>, a <%>, o <%>, d <%>, "
    "on, off, clr, k1..k4 on|off, mode 3|5, ?";

static void set_mode(int volts) {   // K2: VREF -> OFFSET5 (off) / OFFSET3 (on); K3: SUMOUT -> SCALE3 (off) / SCALE5 (on)
    relay_set(2, volts == 3);
    relay_set(3, volts == 5);
}

static bool parse_num(const char *tok, float *out) {
    char *end;
    float v = strtof(tok, &end);
    if (end == tok) return false;
    if (*end == 'k') { v *= 1e3f; end++; }
    if (!strcmp(end, "hz") || !strcmp(end, "%")) end += strlen(end);
    if (*end) return false;
    *out = v;
    return true;
}

static bool parse_onoff(const char *tok, bool *out) {
    if (!tok) return false;
    if (!strcmp(tok, "on") || !strcmp(tok, "1")) { *out = true; return true; }
    if (!strcmp(tok, "off") || !strcmp(tok, "0")) { *out = false; return true; }
    return false;
}

static bool is_periodic(shape_t s) { return s != SHAPE_DC; }

// Plans the sample clock for `o`. Returns NULL on success, else an error message.
static const char *plan_for(const out_state_t *o, wave_plan_t *p) {
    if (!o->on || !is_periodic(o->shape)) return NULL;
    return wave_plan(o->f, dac_sys_hz(), o->shape == SHAPE_RAMP ? 256u : 0u, p);
}

static void apply_output(const wave_plan_t *p) {
    if (!s_out.on) { dac_hold(128); return; }
    if (s_out.shape == SHAPE_DC) { dac_hold((uint8_t)s_out.code); return; }
    dac_stop();   // the DMA must not be reading the table while we rewrite it
    wave_fill(s_table, p->n, s_out.shape, s_out.amp_pct / 100.0f, s_out.offset_pct / 100.0f,
              s_out.duty_pct / 100.0f);
    s_plan = *p;
    dac_play(s_table, p->n, p->div256);
}

static int status(char *r, size_t n) {
    int len;
    if (!s_out.on) {
        len = snprintf(r, n, "off: mid-scale 128 = %.3fV", (double)wave_code_volts(128));
    } else if (s_out.shape == SHAPE_DC) {
        len = snprintf(r, n, "dc %u = %.3fV", s_out.code, (double)wave_code_volts(s_out.code));
    } else {
        len = snprintf(r, n, "%s %.6gHz", wave_shape_name(s_out.shape), s_plan.f);
        if (s_out.shape != SHAPE_RAMP) {
            len += snprintf(r + len, n - len, " a%.4g%%", (double)s_out.amp_pct);
            if (s_out.offset_pct != 0.0f) len += snprintf(r + len, n - len, " o%+.4g%%", (double)s_out.offset_pct);
            if (s_out.shape == SHAPE_SQUARE) len += snprintf(r + len, n - len, " d%.4g%%", (double)s_out.duty_pct);
            if (s_out.amp_pct + (s_out.offset_pct < 0 ? -s_out.offset_pct : s_out.offset_pct) > 100.0f)
                len += snprintf(r + len, n - len, " (clips)");
        }
        len += snprintf(r + len, n - len, " [N=%lu fs=%.6gkS/s]", (unsigned long)s_plan.n, s_plan.fs / 1000.0);
    }
    len += snprintf(r + len, n - len, " | K1-4 ");
    for (unsigned k = 1; k <= RELAY_COUNT; k++) len += snprintf(r + len, n - len, "%d", relay_get(k));
    bool k2 = relay_get(2), k3 = relay_get(3);
    return len + snprintf(r + len, n - len, " (%s)", k2 && !k3 ? "3V3 mode" : !k2 && k3 ? "5V mode" : "MIXED mode");
}

void cmd_init(void) {
    if (CMD_BOOT_MODE) set_mode(CMD_BOOT_MODE);
    dac_hold(128);
}

void cmd_exec(const char *line, char *reply, size_t reply_len) {
    char buf[128];
    strncpy(buf, line, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (char *p = buf; *p; p++) *p = *p == '=' ? ' ' : (char)tolower((unsigned char)*p);

    static const struct { const char *name; shape_t shape; } shapes[] = {
        {"sine", SHAPE_SINE}, {"sin", SHAPE_SINE}, {"square", SHAPE_SQUARE}, {"sq", SHAPE_SQUARE},
        {"tri", SHAPE_TRI}, {"triangle", SHAPE_TRI}, {"saw", SHAPE_SAW}, {"ramp", SHAPE_RAMP},
    };

    out_state_t o = s_out;          // committed only if the whole line parses
    int relay[RELAY_COUNT + 1] = {-1, -1, -1, -1, -1};   // -1 = unchanged
    int mode = 0;
    bool out_dirty = false, clr = false;
    const char *err = NULL;
    const char *delims = " \t,;";
    char *save, *tok = strtok_r(buf, delims, &save);
    if (!tok) err = "empty";

    while (tok && !err) {
        float v;
        bool shape_cmd = false;
        for (size_t i = 0; i < sizeof shapes / sizeof shapes[0]; i++)
            if (!strcmp(tok, shapes[i].name)) { o.shape = shapes[i].shape; shape_cmd = true; }

        if (shape_cmd) {
            o.on = true;
            out_dirty = true;
            char *peek = strtok_r(NULL, delims, &save);   // optional frequency right after the name
            if (peek && parse_num(peek, &v)) o.f = v;
            else { tok = peek; continue; }
        } else if (!strcmp(tok, "dc")) {
            char *arg = strtok_r(NULL, delims, &save);
            if (!arg || !parse_num(arg, &v) || v < 0 || v > 255 || v != (float)(int)v) { err = "dc needs a code 0..255"; break; }
            o.shape = SHAPE_DC; o.code = (unsigned)v; o.on = true; out_dirty = true;
        } else if (!strcmp(tok, "on"))  { o.on = true;  out_dirty = true; }
        else if (!strcmp(tok, "off"))    { o.on = false; out_dirty = true; }
        else if (!strcmp(tok, "clr"))    clr = true;
        else if (!strcmp(tok, "?") || !strcmp(tok, "status")) {}
        else if (!strcmp(tok, "help") || !strcmp(tok, "h")) { snprintf(reply, reply_len, "%s", HELP); return; }
        else if (tok[0] == 'k' && tok[1] >= '1' && tok[1] <= '0' + RELAY_COUNT && !tok[2]) {
            bool on;
            if (!parse_onoff(strtok_r(NULL, delims, &save), &on)) { err = "relay needs on|off"; break; }
            relay[tok[1] - '0'] = on;
        } else if (!strcmp(tok, "mode")) {
            char *arg = strtok_r(NULL, delims, &save);
            if (arg && (!strcmp(arg, "3") || !strcmp(arg, "3v3") || !strcmp(arg, "3.3"))) mode = 3;
            else if (arg && (!strcmp(arg, "5") || !strcmp(arg, "5v"))) mode = 5;
            else { err = "mode needs 3 or 5"; break; }
        } else {
            float *target, lo = 0, hi = 100;
            const char *arg = NULL;
            if (parse_num(tok, &v))                               { target = &o.f; arg = tok; }   // bare number = frequency
            else if (!strcmp(tok, "f") || !strcmp(tok, "freq"))   target = &o.f;
            else if (!strcmp(tok, "a") || !strcmp(tok, "amp"))    target = &o.amp_pct;
            else if (!strcmp(tok, "o") || !strcmp(tok, "offset")) { target = &o.offset_pct; lo = -100; }
            else if (!strcmp(tok, "d") || !strcmp(tok, "duty"))   { target = &o.duty_pct; lo = 1; hi = 99; }
            else { err = tok; break; }
            if (target == &o.f) { lo = (float)WAVE_F_MIN; hi = (float)WAVE_F_MAX; }

            if (!arg) arg = strtok_r(NULL, delims, &save);
            if (!arg || !parse_num(arg, &v)) { err = arg ? arg : "missing value"; break; }
            if (v < lo || v > hi) {
                snprintf(reply, reply_len, "ERR %s must be %g..%g", tok, (double)lo, (double)hi);
                return;
            }
            *target = v;
            out_dirty = true;
        }
        tok = strtok_r(NULL, delims, &save);
    }
    if (err) { snprintf(reply, reply_len, "ERR %s (try help)", err); return; }

    wave_plan_t plan = s_plan;
    if (out_dirty && !clr) {
        const char *perr = plan_for(&o, &plan);
        if (perr) {
            snprintf(reply, reply_len, "ERR %s %.6gHz: %s (waves %g..%gHz, ramp 1..%gHz)",
                     wave_shape_name(o.shape), (double)o.f, perr, WAVE_F_MIN, WAVE_F_MAX, WAVE_FS_MAX / 256);
            return;
        }
    }

    // Commit.
    if (mode) set_mode(mode);
    for (unsigned k = 1; k <= RELAY_COUNT; k++)
        if (relay[k] >= 0) relay_set(k, relay[k]);
    s_out = o;
    if (clr) {
        dac_clear();
        s_out.shape = SHAPE_DC; s_out.code = 0; s_out.on = true;
    } else if (out_dirty) {
        apply_output(&plan);
    }
    status(reply, reply_len);
}
