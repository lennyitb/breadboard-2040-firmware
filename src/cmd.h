// Text commands from USB serial. See README.md for the full list.
// Tokens are whitespace separated, case-insensitive, and applied left to right.
// The whole line is checked first: if any token is bad, nothing changes.
//   sine|square|tri|saw [Hz]   play a waveform (1k, 2.5k, 440hz all work)
//   ramp [Hz]                  every code 0..255 once per period (wiring self-test)
//   dc <0..255>                hold one raw code
//   f <Hz>  a <%>  o <%>  d <%>  frequency, amplitude, offset (% of half-scale), square duty
//   on | off                   resume / hold mid-scale (code 128)
//   clr                        pulse /CLR (output -> 0 V)
//   k1..k4 on|off              relays;  mode 3|5   AWG Scaler range via K2/K3
//   ? | help
#pragma once
#include <stddef.h>

#ifndef CMD_BOOT_MODE
#define CMD_BOOT_MODE 3   // AWG Scaler range set at boot: 3 (K2 on), 5 (K3 on), 0 = leave relays off
#endif

void cmd_init(void);
// Runs one command line and writes a one-line reply (without newline) into `reply`.
void cmd_exec(const char *line, char *reply, size_t reply_len);
