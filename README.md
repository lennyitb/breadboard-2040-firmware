# Breadboard AWG firmware (Pico W → AD7302)

This is a minimal proof of concept for the `awg-control` design. A Pico W (RP2040) drives the
**AD7302** 8-bit parallel DAC and plays waveforms on **VOUTA**. VOUTA goes through R1/C1 and K1
into the AWG Scaler's `DAC1_IN`. Control is plain-text commands over **USB serial**.

The output boots at **mid-scale (code 128 = 1.65 V)**, with the relays set to **3V3 mode**.

## Wiring (from `awg-control/awg-control.kicad_sch`)

| Pico W | AD7302 / other | Notes |
|---|---|---|
| GP0…GP7 (pins 1,2,4,5,6,7,9,10) | DB0…DB7 (pins 8…1) | GP0 → DB0 (LSB) … GP7 → DB7 (MSB) |
| GP8 (pin 11) | /WR (pin 10) | PIO side-set strobe |
| GP9 (pin 12) | A/B (pin 11) | **driven LOW = DAC A** (see notes below) |
| GP10 (pin 14) | /CLR (pin 14) | R10 10 k pull-up to 3V3 |
| 3V3 (pin 36) | VDD (15), REFIN (16), /PD (12) | REFIN = VDD selects the internal VDD/2 reference → output 0…VDD |
| GND | /CS (9), /LDAC (13), AGND (17), DGND (20) | /LDAC low: the output updates on every /WR rising edge |
| — | VOUTA (19) → R1 1 k → C1 1 nF → K1 | RC corner ≈ 159 kHz |
| GP16…GP19 (pins 21,22,24,25) | 1 k → Q1…Q4 → K1…K4 | high = coil energised |

`picotool info -p build/bb_awg.uf2` prints the same pin map from the binary.

Relays (contact sense read from the G5V-1 symbol: pin 1 = NC, pin 10 = NO, pins 5/6 = COM):

| Relay | off (de-energised) | on |
|---|---|---|
| K1 (GP16) | DAC (R1/C1 node) → DAC1_IN | EXT_IN BNC → DAC1_IN |
| K2 (GP17) | VREF → OFFSET5 | VREF → OFFSET3 |
| K3 (GP18) | SUMOUT → SCALE3 | SUMOUT → SCALE5 |
| K4 (GP19) | AWG OUTPUT → J2 | J2 disconnected (mute) |

## Build and flash

The project uses the same `~/.pico-sdk` toolchain as `../pico` (SDK 2.3.1, board `pico_w`).

```sh
export PATH=$HOME/.pico-sdk/toolchain/15_2_Rel1/bin:$PATH
cmake -S . -B build -G Ninja && ninja -C build
# Hold BOOTSEL while plugging in, then drag build/bb_awg.uf2 onto RPI-RP2, or:
~/.pico-sdk/picotool/2.3.1/picotool/picotool load -fx build/bb_awg.uf2
```

Open the serial port with `screen /dev/tty.usbmodem* 115200` or the VS Code Serial Monitor.
Every line gets a one-line reply.

Host tests cover the parser, table generation and frequency planning, with no hardware needed:

```sh
cc -std=c11 -Wall -Wextra -I src test/host_test.c src/wave.c src/cmd.c -lm -o build/host_test && build/host_test
```

## Commands

Tokens are case-insensitive and applied left to right. `key=value` also works.
If any token is bad, the whole line is rejected and nothing changes.

| command | meaning |
|---|---|
| `sine` `square` `tri` `saw` `[Hz]` | play a waveform, optionally setting the frequency (`1k`, `2.5k`, `440hz`) |
| `ramp [Hz]` | step through every code 0…255 once per period. This is the wiring self-test (1…3906 Hz) |
| `dc <0-255>` | hold one raw code. Vout = 3.3 V × code / 256 |
| `f <Hz>` / bare number | frequency, 0.1 Hz … 100 kHz |
| `a <%>` | amplitude, % of full swing (default 100 = 0…3.3 V) |
| `o <%>` | offset, % of half-scale (−100…100). The reply says `(clips)` if a + \|o\| > 100 |
| `d <%>` | square duty, 1…99 |
| `on` / `off` | resume the last output / hold mid-scale (1.65 V) |
| `clr` | pulse /CLR. VOUTA goes to 0 V, which tests the GP10 line |
| `k1`…`k4 on\|off` | individual relays |
| `mode 3` / `mode 5` | AWG Scaler range: `3` = K2 on, K3 off; `5` = K2 off, K3 on |
| `?` / `help` | status / command list |

Example: `square 2k d 25 a 80` replies with
`square 2000Hz a80% d25% [N=500 fs=1000kS/s] | K1-4 0100 (3V3 mode)`.

## First power-up checklist

1. **Before inserting the AD7302** (or with VDD lifted): flash the board and check with a meter.
   GP9 should read 0 V, GP10 3.3 V, GP8 3.3 V. GP0…GP7 should all read 0 V except GP7 (code 128 = `1000 0000`).
2. **Static levels**, measured at VOUTA (pin 19). Measure the Pico's 3V3 first: the output scales with it.
   * `dc 0` → ≈ 0 V. The zero-code error is about 3 LSB, so up to about 40 mV is normal.
   * `dc 128` → 1.650 V. `dc 255` → 3.287 V. The datasheet allows ±1 % gain error.
   * **Bit check.** Gain and zero errors cancel if you measure from `dc 128`. Then
     `dc 129`, `130`, `132`, `136`, `144`, `160`, `192` should each be higher by
     **13, 26, 52, 103, 206, 413, 825 mV** (bits 0…6). Check bit 7 as `dc 128` minus `dc 0`.
     A wrong step points at one specific data line.
3. **Ramp.** Run `ramp 1k` with the scope on VOUTA: you should see a 256-step staircase from 0 to 3.29 V,
   3.9 µs per step, one ramp per 1 ms. Faults look like this:
   * a missing or stuck bit gives repeated flat steps or a jump;
   * swapped data lines make the staircase non-monotonic, with a sawtooth pattern inside it.
   On the R1/C1 node you will see the same ramp, smoothed.
4. **Waveforms:** `sine 1k`, then `tri 10k`, `square 1k d 25`.
5. **/CLR:** `dc 200` then `clr` → 0 V. **Relays** (if fitted with +12 V): `k1 on` should click and
   hand DAC1_IN to EXT_IN. `k4 on` mutes J2.

Troubleshooting: if VOUTA never moves while the ramp is running, check that
A/B is low (GP9) and that /PD and REFIN are at 3V3.
If VOUTB (pin 18) moves instead, A/B is high.

## How it works

* **PIO** (`src/ad7302.pio`, 3 instructions): `out pins, 8` puts a byte on DB0…7, then side-set pulses
  /WR (8 PIO cycles per sample). At the fastest possible clock the timing is 48 ns setup,
  32 ns /WR low and 16 ns hold (AD7302 limits: 15, 20 and 4.5 ns). The firmware never runs faster
  than 1 MS/s, which leaves a ≥ 15× margin.
* **DMA:** one period of the waveform is a table of N bytes. A data channel streams it into the PIO
  FIFO, paced by the PIO DREQ. A second channel rewrites the first one's read address and
  re-triggers it. The loop runs entirely in hardware: the CPU is idle and the samples are gapless.
* **Frequency** = fs / N.
  * N is the longest table (≤ 4096) that keeps fs ≤ 1 MS/s. The AD7302 settles in 1.2 µs typ, 2 µs max.
  * fs comes from the PIO clock divider (16.8 fixed point), so the frequency error is ≤ 125 ppm.
  * At 100 kHz, N = 10. Above roughly 20 kHz the shapes get coarse, partly smoothed by R1/C1.
* Changing waveform stops the DMA, rewrites the table and restarts it. This causes a brief glitch.
  It is not phase-continuous like the DDS in `../pico`, but every period is identical.
* GP9 (A/B) and GP10 (/CLR) are plain GPIO. Code: `src/dac.c` (hardware), `src/wave.c`
  (tables and planning, pure C), `src/cmd.c` (parser, pure C), `src/main.c` (USB loop).

## Notes on the schematic

* **A/B polarity:** the datasheet pin is **Ā/B**: low selects DAC A, high selects DAC B
  (Figure 21: A/B drives DAC B SEL directly and DAC A SEL through an inverter).
  The KiCad symbol label `A/~B` puts the bar on the wrong letter. The firmware drives GP9 low.
* **Annotation:** the Pico and AD7302 are still annotated `A?` and `U?`, and `J1` is used twice.
  With those, KiCad's netlist **drops the A/B ↔ GP9 net**: ERC reports the wire as dangling and
  both pins vanish from the netlist. Annotating them (`A1`, `U1`) restores the net, so the
  drawing is right. Annotate before generating a PCB netlist.
* **K2/K3 sense:** K2 and K3 are wired in opposite senses, so with both relays off the scaler sits
  in a **mixed** OFFSET5 + SCALE3 state. That is the state before the firmware runs, or with the
  Pico unplugged. The firmware sets `mode 3` at boot (`CMD_BOOT_MODE` in `src/cmd.h`) and
  flags `MIXED mode` in the status. Swapping K2's pins 1 and 10 would make "all off" = 5 V mode,
  which is consistent.
