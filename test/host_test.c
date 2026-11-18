// Host-side tests for the pure-C parts (wave.c, cmd.c) against a recording stub of dac.h.
// Run:  cc -std=c11 -Wall -Wextra -I src test/host_test.c src/wave.c src/cmd.c -lm -o build/host_test && build/host_test
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "dac.h"
#include "wave.h"

// ---- dac.h stub ----------------------------------------------------------------------------
static struct {
    char log[512];
    bool playing, relay[RELAY_COUNT + 1];
    int held;            // last held code, -1 if playing
    const uint8_t *table;
    uint32_t n, div256;
    int clears;
} hw;

static void logf_(const char *s) { strncat(hw.log, s, sizeof hw.log - strlen(hw.log) - 1); }
void dac_init(void) {}
void dac_stop(void) { if (hw.playing) logf_("stop "); hw.playing = false; }
void dac_hold(uint8_t code) { dac_stop(); hw.held = code; logf_("hold "); }
void dac_play(const uint8_t *t, uint32_t n, uint32_t div256) {
    if (hw.playing) logf_("PLAY-WHILE-PLAYING ");
    hw.playing = true; hw.held = -1; hw.table = t; hw.n = n; hw.div256 = div256; logf_("play ");
}
void dac_clear(void) { dac_stop(); hw.clears++; hw.held = 0; logf_("clr "); }
uint32_t dac_sys_hz(void) { return 125000000u; }
void relay_set(unsigned k, bool on) { hw.relay[k] = on; }
bool relay_get(unsigned k) { return hw.relay[k]; }

// ---- tiny test harness -----------------------------------------------------------------------
static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char reply[256];
static const char *run(const char *line) {
    hw.log[0] = 0;
    cmd_exec(line, reply, sizeof reply);
    printf("  > %-28s %s\n", line, reply);
    return reply;
}

static void test_plan(void) {
    wave_plan_t p;
    CHECK(!wave_plan(1000, 125000000, 0, &p) && p.n == 1000 && fabs(p.f - 1000) < 0.1 && p.fs <= 1.0001e6,
          "1 kHz: n=%u f=%f fs=%f", p.n, p.f, p.fs);
    CHECK(!wave_plan(0.1, 125000000, 0, &p) && p.n == WAVE_N_MAX && fabs(p.f - 0.1) / 0.1 < 1e-4, "0.1 Hz: f=%f", p.f);
    CHECK(!wave_plan(100000, 125000000, 0, &p) && p.n == 10 && p.div256 >= 256, "100 kHz: n=%u", p.n);
    CHECK(wave_plan(100001, 125000000, 0, &p) != NULL, "above f max accepted");
    CHECK(wave_plan(0.05, 125000000, 0, &p) != NULL, "below f min accepted");
    CHECK(!wave_plan(1000, 125000000, 256, &p) && p.n == 256 && fabs(p.f - 1000) < 0.1, "ramp 1 kHz: f=%f", p.f);
    CHECK(!wave_plan(3906, 125000000, 256, &p), "ramp 3906 Hz rejected");
    CHECK(wave_plan(4000, 125000000, 256, &p) != NULL, "ramp 4 kHz accepted (fs > 1 MHz)");
    CHECK(wave_plan(0.5, 125000000, 256, &p) != NULL, "ramp 0.5 Hz accepted (divider overflow)");
    CHECK(!wave_plan(1.0, 125000000, 256, &p), "ramp 1 Hz rejected");
    // worst frequency error over a sweep (divider is quantised to 1/256)
    double worst = 0;
    for (double f = 0.1; f <= 100000; f *= 1.0137) {
        if (wave_plan(f, 125000000, 0, &p)) { CHECK(0, "plan failed at %f", f); break; }
        double e = fabs(p.f - f) / f;
        if (e > worst) worst = e;
        CHECK(p.fs <= WAVE_FS_MAX * 1.0001 && p.div256 >= 256 && p.div256 <= 0xffffff, "bad plan at %f Hz", f);
    }
    printf("  worst relative frequency error 0.1 Hz..100 kHz: %.2e\n", worst);
    // Bound: half a divider step at the smallest divider (1 MS/s -> 15.625): 1 / (2 * 15.625 * 256) = 125 ppm
    CHECK(worst <= 1.26e-4, "frequency error %.2e", worst);
}

static void test_fill(void) {
    static uint8_t t[WAVE_N_MAX];
    wave_fill(t, 256, SHAPE_RAMP, 1, 0, 0.5f);
    bool ok = true;
    for (int i = 0; i < 256; i++) ok &= t[i] == i;
    CHECK(ok, "ramp is not 0..255");

    wave_fill(t, 1000, SHAPE_SINE, 1, 0, 0.5f);
    int lo = 255, hi = 0;
    for (int i = 0; i < 1000; i++) { if (t[i] < lo) lo = t[i]; if (t[i] > hi) hi = t[i]; }
    CHECK(lo == 0 && hi == 255 && t[0] == 128 && t[250] == 255 && t[750] == 0, "sine lo=%d hi=%d t0=%d", lo, hi, t[0]);

    wave_fill(t, 1000, SHAPE_SINE, 0.5f, 0, 0.5f);
    lo = 255; hi = 0;
    for (int i = 0; i < 1000; i++) { if (t[i] < lo) lo = t[i]; if (t[i] > hi) hi = t[i]; }
    CHECK(lo == 64 && hi == 191, "sine a50 lo=%d hi=%d", lo, hi);

    wave_fill(t, 1000, SHAPE_SQUARE, 1, 0, 0.25f);
    int highs = 0;
    for (int i = 0; i < 1000; i++) highs += t[i] == 255;
    CHECK(highs == 250 && t[999] == 0, "square d25 highs=%d", highs);

    wave_fill(t, 1000, SHAPE_TRI, 1, 0, 0.5f);
    CHECK(t[0] == 0 && t[500] == 255 && t[250] == 128, "tri t0=%d t500=%d t250=%d", t[0], t[500], t[250]);

    wave_fill(t, 1000, SHAPE_SINE, 1, 0.5f, 0.5f);   // clips at the top
    CHECK(t[250] == 255 && t[750] == 64, "offset sine t250=%d t750=%d", t[250], t[750]);
}

static void test_cmd(void) {
    cmd_init();
    CHECK(hw.held == 128 && hw.relay[2] && !hw.relay[3], "boot: held=%d K2=%d K3=%d", hw.held, hw.relay[2], hw.relay[3]);
    CHECK(strstr(run("?"), "off: mid-scale 128") && strstr(reply, "3V3 mode"), "status");

    run("sine 1k");
    CHECK(hw.playing && hw.n == 1000 && hw.table[250] == 255, "sine 1k: n=%u", hw.n);
    CHECK(strstr(reply, "sine 1000") != NULL, "reply");

    run("square 2k d 25 a 80 o -10");
    CHECK(strstr(hw.log, "stop") == hw.log && hw.n == 500, "must stop before rewriting table: log='%s'", hw.log);
    CHECK(strstr(reply, "d25%") && strstr(reply, "a80%") && strstr(reply, "o-10%"), "square params");

    uint32_t n = hw.n;
    CHECK(strncmp(run("sine 1k a 150"), "ERR", 3) == 0 && hw.n == n && hw.log[0] == 0, "range error must change nothing");
    CHECK(strncmp(run("k1 on bogus"), "ERR", 3) == 0 && !hw.relay[1], "bad token must not touch relays");
    CHECK(strncmp(run("ramp 5k"), "ERR", 3) == 0 && hw.n == n, "ramp too fast");
    CHECK(strncmp(run("sine 200k"), "ERR", 3) == 0 && hw.n == n, "f above max");
    CHECK(strncmp(run("dc 256"), "ERR", 3) == 0, "dc 256");
    CHECK(strncmp(run("dc 1.5"), "ERR", 3) == 0, "dc 1.5");

    run("ramp 1k");
    CHECK(hw.playing && hw.n == 256 && hw.table[17] == 17, "ramp");
    run("2.5k");
    CHECK(hw.playing && hw.n == 256 && strstr(reply, "ramp 2500"), "bare frequency keeps shape");

    run("dc 200");
    CHECK(!hw.playing && hw.held == 200 && strstr(reply, "2.578V"), "dc 200");
    run("off");
    CHECK(hw.held == 128, "off");
    run("on");
    CHECK(!hw.playing && hw.held == 200, "on resumes the last output (dc 200): held=%d", hw.held);
    run("sine");
    CHECK(hw.playing && strstr(reply, "sine 2500"), "shape without freq keeps f");

    run("k1 on k4 1");
    CHECK(hw.relay[1] && hw.relay[4] && hw.playing && hw.log[0] == 0, "relays only: output untouched, log='%s'", hw.log);
    run("mode 5");
    CHECK(!hw.relay[2] && hw.relay[3] && strstr(reply, "5V mode"), "mode 5");
    run("K2 ON");
    CHECK(strstr(reply, "MIXED"), "mixed mode flagged");
    run("mode 3v3 k1 off k4 off");

    run("clr");
    CHECK(hw.clears == 1 && !hw.playing && strstr(reply, "dc 0"), "clr");
    CHECK(strncmp(run("help"), "sine", 4) == 0, "help");
    CHECK(strncmp(run("f"), "ERR", 3) == 0, "f without value");
    CHECK(strncmp(run(""), "ERR", 3) == 0, "empty");
    run("tri f=440hz a=50%");
    CHECK(hw.playing && strstr(reply, "tri 440") && strstr(reply, "a50%"), "key=value form");
}

int main(void) {
    printf("plan\n");  test_plan();
    printf("fill\n");  test_fill();
    printf("cmd\n");   test_cmd();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
