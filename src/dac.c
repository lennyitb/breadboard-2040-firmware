// AD7302 driver: PIO strobes the parallel bus, two chained DMA channels loop a table into it.
//
//   data DMA : table[0..n-1] -> PIO TX FIFO, 8-bit, paced by the PIO TX DREQ, CHAIN_TO ctrl
//   ctrl DMA : &table -> data channel's READ_ADDR_TRIG (one word), restarting the data channel
//
// The two channels loop in hardware with no CPU involvement. The PIO's 8-deep TX FIFO covers
// the few cycles the ctrl channel needs, so the output is gapless and jitter-free.
#include "dac.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "ad7302.pio.h"
#include "wave.h"

_Static_assert(AD7302_PIO_CYCLES_PER_SAMPLE == WAVE_PIO_CYCLES, "ad7302.pio and wave.h disagree");

static PIO s_pio = pio0;
static uint s_sm;
static uint s_dma_data, s_dma_ctrl;
static bool s_playing;
static const uint8_t *volatile s_table_addr;   // read by the ctrl DMA channel

#define HOLD_DIV 16u   // PIO divider used for single writes: ~1 MS/s, well inside the timing limits

void dac_init(void) {
    // A/B low selects DAC A; /CLR is driven high (inactive). Set the level before the direction to avoid a glitch.
    gpio_init(DAC_PIN_AB);
    gpio_put(DAC_PIN_AB, 0);
    gpio_set_dir(DAC_PIN_AB, GPIO_OUT);
    gpio_init(DAC_PIN_CLR);
    gpio_put(DAC_PIN_CLR, 1);
    gpio_set_dir(DAC_PIN_CLR, GPIO_OUT);
    for (unsigned k = 1; k <= RELAY_COUNT; k++) {
        gpio_init(RELAY_PIN_K1 + k - 1);
        gpio_set_dir(RELAY_PIN_K1 + k - 1, GPIO_OUT);   // gpio_init leaves the level low = coil off
    }

    uint offset = pio_add_program(s_pio, &ad7302_program);
    s_sm = pio_claim_unused_sm(s_pio, true);
    ad7302_program_init(s_pio, s_sm, offset, DAC_PIN_D0, DAC_PIN_WR);

    s_dma_data = dma_claim_unused_channel(true);
    s_dma_ctrl = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(s_dma_ctrl);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(s_dma_ctrl, &c, &dma_hw->ch[s_dma_data].al3_read_addr_trig,
                          &s_table_addr, 1, false);

    dac_hold(128);   // the first write also releases the AD7302 power-on reset (outputs held at 0 V until then)
}

void dac_stop(void) {
    if (!s_playing) return;
    // RP2040-E13: aborting a channel can still fire its CHAIN_TO. Break the loop first
    // (chain data -> itself means no chain), then abort ctrl before data, in case ctrl
    // re-triggers data in the meantime.
    dma_channel_config c = dma_get_channel_config(s_dma_data);
    channel_config_set_chain_to(&c, s_dma_data);
    dma_channel_set_config(s_dma_data, &c, false);
    dma_channel_abort(s_dma_ctrl);
    dma_channel_abort(s_dma_data);
    s_playing = false;
}

static void set_div256(uint32_t div256) {
    pio_sm_set_clkdiv_int_frac8(s_pio, s_sm, div256 >> 8, div256 & 0xff);
    pio_sm_clkdiv_restart(s_pio, s_sm);
}

void dac_hold(uint8_t code) {
    dac_stop();
    pio_sm_clear_fifos(s_pio, s_sm);   // drop queued samples from the old waveform
    set_div256(HOLD_DIV << 8);
    pio_sm_put(s_pio, s_sm, code);
}

void dac_play(const uint8_t *table, uint32_t n, uint32_t div256) {
    dac_stop();
    pio_sm_clear_fifos(s_pio, s_sm);
    set_div256(div256);

    s_table_addr = table;
    dma_channel_config c = dma_channel_get_default_config(s_dma_data);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);   // byte lands in all lanes; `out` takes bits 0..7
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm, true));
    channel_config_set_chain_to(&c, s_dma_ctrl);
    dma_channel_configure(s_dma_data, &c, &s_pio->txf[s_sm], table, n, false);   // n is reloaded on every trigger
    dma_channel_start(s_dma_ctrl);
    s_playing = true;
}

void dac_clear(void) {
    dac_stop();
    pio_sm_clear_fifos(s_pio, s_sm);
    sleep_us(10);                    // let an in-flight write finish
    gpio_put(DAC_PIN_CLR, 0);
    sleep_us(1);                     // t10 >= 20 ns
    gpio_put(DAC_PIN_CLR, 1);
}

uint32_t dac_sys_hz(void) { return clock_get_hz(clk_sys); }

void relay_set(unsigned k, bool on) {
    if (k >= 1 && k <= RELAY_COUNT) gpio_put(RELAY_PIN_K1 + k - 1, on);
}

bool relay_get(unsigned k) {
    return k >= 1 && k <= RELAY_COUNT && gpio_get_out_level(RELAY_PIN_K1 + k - 1);
}
