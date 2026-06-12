#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "emulation/CPU/6502.h"
#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#include "sid_comms.h"
#include "uart_tx.pio.h"
#include "uart_rx.pio.h"

/* SID file header — all multi-byte fields are big-endian */
typedef struct __attribute__((packed)) {
    char     magic[4];
    uint16_t version;
    uint16_t data_offset;
    uint16_t load_address;
    uint16_t init_address;
    uint16_t play_address;
    uint16_t songs;
    uint16_t start_song;
    uint32_t speed;
    char     title[32];
    char     author[32];
    char     released[32];
    uint16_t flags;
    uint8_t  start_page;
    uint8_t  page_length;
    uint8_t  second_sid_addr;
    uint8_t  third_sid_addr;
} SIDHeader;

static inline uint16_t be16(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t be32(uint32_t v) { return __builtin_bswap32(v); }

/* SID chip GPIO wiring (Pico 2 side, before level shifters)
 * D0-D7  : GP0-GP7   data bus
 * A0-A4  : GP8-GP12  address bus (5-bit)
 * /CS    : GP13
 * R/W    : GP14      HIGH=read, LOW=write
 * /RES   : GP15
 * phi2   : GP16      PWM ~0.985 MHz
 * SID2   : GP26      SIDKick $D500-detect input (HIGH during 2nd-SID writes)
 * LS_OE  : tied to 3.3V (always enabled)
 */
#define SID_D0     0
#define SID_A0     8
#define SID_CS_N  13
#define SID_RW    14
#define SID_RES_N 15
#define SID_CLK   16
#define SID_D500  26

#define SID_DATA_MASK  0x000000FFu   /* GP0-GP7  */
#define SID_ADDR_MASK  0x00001F00u   /* GP8-GP12 */
#define SID_BUS_MASK   (SID_DATA_MASK | SID_ADDR_MASK)

static void sid_write(uint8_t reg, uint8_t val)
{
    gpio_put_masked(SID_BUS_MASK,
                    (uint32_t)val | ((uint32_t)(reg & 0x1F) << 8));
    gpio_put(SID_CS_N, 0);
    busy_wait_us_32(2);   /* hold >= 1 phi2 period (~1.015 us) */
    gpio_put(SID_CS_N, 1);
}

/* ---- Config bus (slow-PWM, phi2-counter synced) ----------------------------
 * Restored to the one approach that read the SIDKick config correctly.  During
 * config access phi2 is slowed to ~294 kHz (the SIDKick services config on its
 * slow ARM core); each access is one bus cycle synced to phi2 by polling the PWM
 * slice counter (wrap=1, level=1 → counter 0 = phi2 HIGH, 1 = phi2 LOW). */
static void sidkick_config_begin(void)
{
    pwm_set_clkdiv_int_frac(pwm_gpio_to_slice_num(SID_CLK), 255, 0); /* ~294 kHz */
    busy_wait_us_32(5000);                            /* let the new clock settle */
}

static void sidkick_config_end(void)
{
    pwm_set_clkdiv_int_frac(pwm_gpio_to_slice_num(SID_CLK), 76, 2);  /* PAL ~985 kHz */
}

static void cfg_write(uint8_t reg, uint8_t val)
{
    gpio_set_dir_out_masked(SID_DATA_MASK);
    gpio_put(SID_RW, 0);                              /* write */
    gpio_put_masked(SID_BUS_MASK, (uint32_t)val | ((uint32_t)(reg & 0x1F) << 8));

    uint slice = pwm_gpio_to_slice_num(SID_CLK);
    while (pwm_get_counter(slice) != 0) tight_loop_contents(); /* wait for HIGH */
    while (pwm_get_counter(slice) == 0) tight_loop_contents(); /* wait for LOW  */
    gpio_put(SID_CS_N, 0);                                     /* CS low, stable before rising */
    while (pwm_get_counter(slice) != 0) tight_loop_contents(); /* phi2 HIGH     */
    while (pwm_get_counter(slice) == 0) tight_loop_contents(); /* wait for LOW  */
    gpio_put(SID_CS_N, 1);                                     /* release CS during LOW */
}

static uint8_t cfg_read(uint8_t reg)
{
    gpio_put_masked(SID_ADDR_MASK, (uint32_t)(reg & 0x1F) << 8);
    gpio_put(SID_RW, 1);                              /* read */
    gpio_put_masked(SID_DATA_MASK, SID_DATA_MASK);    /* drive HIGH then release (TXB0108) */
    gpio_set_dir_in_masked(SID_DATA_MASK);

    uint slice = pwm_gpio_to_slice_num(SID_CLK);
    while (pwm_get_counter(slice) != 0) tight_loop_contents(); /* wait for HIGH */
    while (pwm_get_counter(slice) == 0) tight_loop_contents(); /* wait for LOW  */
    gpio_put(SID_CS_N, 0);                                     /* CS low, stable before rising */
    while (pwm_get_counter(slice) != 0) tight_loop_contents(); /* phi2 rises    */

    /* sample ~330 ns into the HIGH phase */
    __asm volatile (
        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
        "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\n"
    );
    uint8_t val = (uint8_t)(gpio_get_all() & SID_DATA_MASK);

    while (pwm_get_counter(slice) == 0) tight_loop_contents(); /* wait for LOW  */
    gpio_put(SID_CS_N, 1);                                     /* release CS during LOW */

    gpio_put(SID_RW, 0);
    gpio_set_dir_out_masked(SID_DATA_MASK);
    return val;
}

static void sid_init(void)
{
    for (int i = SID_D0; i < SID_D0 + 8; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
    }
    for (int i = SID_A0; i < SID_A0 + 5; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
    }
    gpio_init(SID_CS_N);  gpio_set_dir(SID_CS_N,  GPIO_OUT); gpio_put(SID_CS_N,  1);
    gpio_init(SID_RW);    gpio_set_dir(SID_RW,    GPIO_OUT); gpio_put(SID_RW,    0); /* LOW = write */
    gpio_init(SID_RES_N); gpio_set_dir(SID_RES_N, GPIO_OUT); gpio_put(SID_RES_N, 0);
    gpio_init(SID_D500);  gpio_set_dir(SID_D500,  GPIO_OUT); gpio_put(SID_D500,  0); /* 2nd-SID select, LOW = SID1 */
    /* phi2 clock on GP16 via PWM — default PAL: 150 MHz / (76.125 * 2) = 985 424 Hz */
    gpio_set_function(SID_CLK, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(SID_CLK);
    uint chan  = pwm_gpio_to_channel(SID_CLK);
    pwm_set_clkdiv_int_frac(slice, 76, 2); /* PAL: 76 + 2/16 = 76.125 */
    pwm_set_wrap(slice, 1);
    pwm_set_chan_level(slice, chan, 1);
    pwm_set_enabled(slice, true);

    sleep_ms(200);
    gpio_put(SID_RES_N, 1);
    sleep_ms(3000); /* wait for SID chip startup to complete */

    /* Start Voice 3 noise LFSR running from the beginning so $D41B is
     * non-deterministic by the time the user stops a tune. */
    sid_write(0x0E, 0xFF);
    sid_write(0x0F, 0xFF);
    sid_write(0x12, 0x80);
}

/* Adjust phi2 clock divider for PAL or NTSC.
 * PAL  985 248 Hz: divider 76.125  (int=76, frac=2)  → 985 424 Hz  (0.018% off)
 * NTSC 1 022 727 Hz: divider 73.3125 (int=73, frac=5) → 1 022 654 Hz (0.007% off) */
static void sid_set_clock(bool ntsc)
{
    uint slice = pwm_gpio_to_slice_num(SID_CLK);
    if (ntsc)
        pwm_set_clkdiv_int_frac(slice, 73, 5);
    else
        pwm_set_clkdiv_int_frac(slice, 76, 2);
}

/* ------------------------------------------------------------------ */
/* PIO UART — GP21 TX, GP22 RX at 115200 baud                        */

#define PIO_UART_TX_PIN  21
#define PIO_UART_RX_PIN  22

static PIO  pio_uart  = pio0;
static uint pio_tx_sm = 0;
static uint pio_rx_sm = 1;

static void pio_uart_init(void)
{
    uint tx_off = pio_add_program(pio_uart, &uart_tx_program);
    uint rx_off = pio_add_program(pio_uart, &uart_rx_program);
    uart_tx_program_init(pio_uart, pio_tx_sm, tx_off, PIO_UART_TX_PIN, SID_BAUD_RATE);
    uart_rx_program_init(pio_uart, pio_rx_sm, rx_off, PIO_UART_RX_PIN, SID_BAUD_RATE);
}

static void send_meta(const SidMeta *m)
{
    uint8_t len = sizeof(SidMeta);
    uint8_t chk = PKT_TYPE_META ^ len;
    const uint8_t *p = (const uint8_t *)m;
    for (uint8_t i = 0; i < len; i++) chk ^= p[i];

    uart_tx_program_putc(pio_uart, pio_tx_sm, PKT_HDR0);
    uart_tx_program_putc(pio_uart, pio_tx_sm, PKT_HDR1);
    uart_tx_program_putc(pio_uart, pio_tx_sm, PKT_TYPE_META);
    uart_tx_program_putc(pio_uart, pio_tx_sm, len);
    for (uint8_t i = 0; i < len; i++)
        uart_tx_program_putc(pio_uart, pio_tx_sm, p[i]);
    uart_tx_program_putc(pio_uart, pio_tx_sm, chk);
}

/* Returns the CMD_* payload byte when a valid command packet arrives,
 * or 0 if the RX FIFO has no complete packet yet. */
static uint8_t poll_cmd(void)
{
    typedef enum { H0, H1, TP, LN, PL, CK } State;
    static State   state   = H0;
    static uint8_t type, len, chk, payload;

    while (!pio_sm_is_rx_fifo_empty(pio_uart, pio_rx_sm)) {
        uint8_t b = (uint8_t)uart_rx_program_getc(pio_uart, pio_rx_sm);
        switch (state) {
            case H0: if (b == PKT_HDR0) state = H1; break;
            case H1: state = (b == PKT_HDR1) ? TP : H0; break;
            case TP: type = b; chk = b; state = LN; break;
            case LN: len = b; chk ^= b; state = (len == 1) ? PL : H0; break;
            case PL: payload = b; chk ^= b; state = CK; break;
            case CK:
                state = H0;
                if (b == chk && type == PKT_TYPE_CMD) return payload;
                break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */

static bool     rsid_irq_active = false;  /* true only during RSID CIA IRQ playback */
static M6502   *current_cpu     = NULL;   /* valid only inside play_sid_file */
static uint8_t  memory[65536];
static uint32_t dbg_sid_writes  = 0;

/* CIA1 ($DC00-$DCFF) minimal emulation — tracks timer A only */
static struct {
    uint8_t timer_lo;   /* $DC04 */
    uint8_t timer_hi;   /* $DC05 */
    uint8_t cra;        /* $DC0E control register A */
    bool    active;     /* timer A started (CRA bit 0 set) */
} cia1;

/* RSID re-entry detection: save/patch play-routine entry bytes to stop
 * tunes that loop via direct JMP back to their own entry point. */
static uint16_t rsid_play_entry   = 0;
static uint8_t  rsid_play_orig[3] = {0, 0, 0};
static uint32_t rsid_frame_writes = 0;
static uint8_t  rsid_frame_s      = 0;

static zuint8 cpu_read(void *ctx, zuint16 addr)
{
    (void)ctx;
    /* During RSID IRQ playback, reading $DC0D acknowledges the CIA1 timer A
     * interrupt.  Real CIA hardware deasserts the IRQ line at this moment —
     * emulate that so the play routine doesn't re-trigger on every m6502_run
     * cycle after RTI restores I=0.  PSID tunes never enter this branch. */
    if (addr == 0xDC0D && rsid_irq_active) {
        if (current_cpu) m6502_irq(current_cpu, TRUE);  /* line HIGH = deassert */
        return 0x81;
    }
    /* Re-entry into the play routine: redirect to idle loop.
     * current_cpu->state.pc == addr distinguishes opcode fetches (re-entry via
     * JMP) from data reads at the same address (false positives with write count).
     * rsid_frame_writes > 0 skips the very first entry this frame. */
    if (rsid_irq_active && rsid_play_entry && addr == rsid_play_entry
        && current_cpu && current_cpu->state.pc == (zuint16)rsid_play_entry
        && rsid_frame_writes > 0) {
        memory[rsid_play_entry]     = 0x4C;
        memory[rsid_play_entry + 1] = 0x00;
        memory[rsid_play_entry + 2] = 0xE1;  /* JMP $E100 */
        return 0x4C;
    }
    return memory[addr];
}

static void cpu_write(void *ctx, zuint16 addr, zuint8 val)
{
    (void)ctx;
    memory[addr] = val;
    /* First SID:  $D400-$D41C.
     * Second SID: common 2SID base addresses ($D420, $D500, $DE00, $DF00).
     * For a second-SID write we raise the SIDKick $D500-detect line so the
     * SIDKick routes it to SID2; (addr & $1F) is the register offset in all cases. */
    bool first_sid  = (addr >= 0xD400 && addr <= 0xD41C);
    bool second_sid = (addr >= 0xD420 && addr <= 0xD43F) ||
                      (addr >= 0xD500 && addr <= 0xD51F) ||
                      (addr >= 0xDE00 && addr <= 0xDE1F) ||
                      (addr >= 0xDF00 && addr <= 0xDF1F);
    if (first_sid || second_sid) {
        dbg_sid_writes++;
        rsid_frame_writes++;
        if (second_sid) {
            gpio_put(SID_D500, 1);
            sid_write((uint8_t)(addr & 0x1F), val);
            gpio_put(SID_D500, 0);
        } else {
            sid_write((uint8_t)(addr & 0x1F), val);
        }
    } else if (addr >= 0xDC00 && addr <= 0xDCFF) {
        switch (addr & 0xFF) {
            case 0x04: cia1.timer_lo = val; break;
            case 0x05: cia1.timer_hi = val; break;
            case 0x0E:
                cia1.cra = val;
                if (val & 0x01) cia1.active = true;
                break;
        }
    }
}

/* Call a 6502 subroutine by pushing a sentinel return address onto the
 * stack, setting registers and PC, then running until the routine's RTS
 * brings execution back to the sentinel.
 *
 * Sentinel: $02FF (filled with RTS so over-returning is harmless).
 * KERNAL stubs: $E000-$FFFD pre-filled with RTS so any JSR into the
 * KERNAL ROM area returns gracefully instead of executing zero-bytes. */
#define SENTINEL 0x02FFu

static void call_routine(M6502 *cpu, uint16_t addr, uint8_t a, uint8_t x, uint8_t y)
{
    cpu->state.a = a;
    cpu->state.x = x;
    cpu->state.y = y;
    /* push sentinel-1: when the routine's RTS executes it pops this,
       adds 1, and lands at SENTINEL */
    uint8_t saved_s = cpu->state.s;
    memory[0x0100 + cpu->state.s] = (uint8_t)((SENTINEL - 1) >> 8);
    cpu->state.s--;
    memory[0x0100 + cpu->state.s] = (uint8_t)((SENTINEL - 1) & 0xFF);
    cpu->state.s--;
    cpu->state.pc = addr;
    /* run until PC reaches sentinel, or 2M-cycle safety cap */
    uint32_t limit = 2000000u;
    while (cpu->state.pc != SENTINEL && limit > 0) {
        uint32_t ran = (uint32_t)m6502_run(cpu, 1);
        limit -= (ran < limit) ? ran : limit;
    }
    /* if we hit the limit the routine never returned — restore SP so the
       leaked sentinel bytes don't permanently corrupt the 6502 stack */
    if (cpu->state.pc != SENTINEL)
        cpu->state.s = saved_s;
}

/* ------------------------------------------------------------------ */

static bool is_sid_file(const char *name)
{
    size_t n = strlen(name);
    if (n >= 5) {
        const char *e = name + n - 4;
        if (e[0] == '.' &&
            (e[1] == 's' || e[1] == 'S') &&
            (e[2] == 'i' || e[2] == 'I') &&
            (e[3] == 'd' || e[3] == 'D'))
            return true;
    }
    return false;
}

/* True if the filename contains "2SID" (case-insensitive) → stereo tune. */
static bool is_2sid_file(const char *name)
{
    for (const char *p = name; *p; p++) {
        if ((p[0] == '2') &&
            (p[1] == 's' || p[1] == 'S') &&
            (p[2] == 'i' || p[2] == 'I') &&
            (p[3] == 'd' || p[3] == 'D'))
            return true;
    }
    return false;
}

/* Build a SidMeta from an already-parsed header and send it to the Display Pico.
 * song_num is 1-based; pass 0 to use the file's default start song. */
static void send_meta_from_header(const SIDHeader *h, const char *path, uint8_t song_num)
{
    SidMeta meta = {0};
    meta.song_num   = song_num ? song_num : (uint8_t)be16(h->start_song);
    meta.song_count = (uint8_t)be16(h->songs);
    memcpy(meta.title,    h->title,    SID_TITLE_LEN - 1);
    memcpy(meta.author,   h->author,   SID_AUTHOR_LEN - 1);
    memcpy(meta.released, h->released, SID_RELEASED_LEN - 1);

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(meta.filename, base, SID_FILENAME_LEN - 1);
    char *dot = strrchr(meta.filename, '.');
    if (dot) *dot = '\0';

    send_meta(&meta);
}

/* Read and validate a SID file header into hdr[0x7C].
 * Returns true if the file opens and is a valid PSID/RSID. */
static bool read_sid_header(const char *path, uint8_t hdr[0x7C])
{
    FIL fil; UINT br;
    if (f_open(&fil, path, FA_READ) != FR_OK) return false;
    bool ok = (f_read(&fil, hdr, 0x7C, &br) == FR_OK && br >= 0x76);
    f_close(&fil);
    if (!ok) return false;
    const SIDHeader *h = (const SIDHeader *)hdr;
    return memcmp(h->magic, "PSID", 4) == 0 || memcmp(h->magic, "RSID", 4) == 0;
}

/* Defined below, after the SD helpers — applies the mono/stereo SIDKick profile. */
static void sidkick_apply_profile(bool stereo, bool ntsc);

/* Load a SID file from the SD card, run INIT, then call PLAY at the correct rate
 * until CMD_NEXT, CMD_PREV, or CMD_STOP is received.  CMD_NEXT_TUNE/PREV_TUNE
 * change subtunes live without leaving the file.
 * *song_sel (0-based) selects the starting subtune and is updated to the
 * last-played subtune on return, so a later replay resumes where you left off.
 * Returns the command that stopped playback (CMD_NEXT/PREV/STOP), or 0 on error.
 * PSID (play_addr != 0): direct polling.
 * RSID (play_addr == 0): CIA1 timer A drives IRQ-based playback. */
static uint8_t play_sid_file(const char *path, uint8_t *song_sel)
{
    FIL    fil;
    UINT   br;
    FRESULT fr;

    fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK) {
        printf("Cannot open %s (%d)\r\n", path, fr);
        return 0;
    }

    /* Read header — v2 is 0x7C bytes; v1 is 0x76.  Read the larger size;
     * for v1 files the extra bytes are unused padding. */
    uint8_t hdr[0x7C];
    fr = f_read(&fil, hdr, sizeof(hdr), &br);
    if (fr != FR_OK || br < 0x76) {
        printf("Header read failed\r\n");
        f_close(&fil);
        return 0;
    }

    const SIDHeader *h = (const SIDHeader *)hdr;
    if (memcmp(h->magic, "PSID", 4) != 0 && memcmp(h->magic, "RSID", 4) != 0) {
        printf("Not a SID file: %.4s\r\n", h->magic);
        f_close(&fil);
        return 0;
    }

    uint16_t version     = be16(h->version);
    uint16_t data_offset = be16(h->data_offset);
    uint16_t load_addr   = be16(h->load_address);
    uint16_t init_addr   = be16(h->init_address);
    uint16_t play_addr   = be16(h->play_address);
    uint16_t start_song  = be16(h->start_song);
    uint32_t speed       = be32(h->speed);

    /* Determine clock (PAL / NTSC) from the header flags — per file. */
    bool ntsc = false;
    if (version >= 2 && data_offset >= 0x7C) {
        uint8_t clk = (be16(h->flags) >> 2) & 0x3;
        ntsc = (clk == 2);  /* 1=PAL, 2=NTSC, 3=both → prefer PAL */
    }
    uint16_t songs    = be16(h->songs); if (songs == 0) songs = 1;
    uint8_t  song_idx = *song_sel;          /* caller-selected starting subtune */
    if (song_idx >= songs) song_idx = 0;

    /* Print file info (once) */
    char title[33], author[33], released[33];
    memcpy(title,    h->title,    32); title[32]    = '\0';
    memcpy(author,   h->author,   32); author[32]   = '\0';
    memcpy(released, h->released, 32); released[32] = '\0';
    printf("\r\n=== %s ===\r\n", path);
    printf("Title   : %s\r\n", title);
    printf("Author  : %s\r\n", author);
    printf("Released: %s\r\n", released);
    printf("Songs   : %u  (default %u)\r\n", songs, start_song);
    printf("Clock   : %s\r\n", ntsc ? "NTSC" : "PAL");

    /* Configure the SIDKick for this file (per file, not per subtune): stereo if
     * the filename contains "2SID", otherwise mono.  Clock field tracks PAL/NTSC.
     * Done before sid_set_clock() because the config write temporarily slows
     * phi2 and restores it to PAL. */
    bool stereo = is_2sid_file(path);
    printf("\r\n%s tune detected\r\n", stereo ? "2SID stereo" : "Mono");
    sidkick_apply_profile(stereo, ntsc);   /* prints what it actually did */
    sid_set_clock(ntsc);

    /* Resolve the load address and where the binary data starts — once. */
    uint32_t data_start = data_offset;
    if (load_addr == 0) {
        uint8_t ab[2];
        if (f_lseek(&fil, data_offset) != FR_OK ||
            f_read(&fil, ab, 2, &br) != FR_OK || br < 2) {
            f_close(&fil); return 0;
        }
        load_addr  = ab[0] | ((uint16_t)ab[1] << 8);
        data_start = data_offset + 2;
    }
    if (init_addr == 0)             /* PSID spec: init==0 → use load address */
        init_addr = load_addr;
    printf("Load    : $%04X  Init: $%04X  Play: $%04X\r\n",
           load_addr, init_addr, play_addr);

    uint8_t  stop_cmd = 0;
    uint32_t play_hz  = ntsc ? 60u : 50u;   /* (re)assigned per subtune below */

    /* ===== Subtune (re)start point.  CMD_NEXT_TUNE / CMD_PREV_TUNE change the
     * subtune and jump back here; file-level commands fall through to exit. ===== */
play_subtune: ;
    /* Default the poll rate to the video standard (PAL 50 Hz / NTSC 60 Hz).  The
     * speed-flag bit means the song is CIA-timer-timed, NOT that it runs at
     * 60 Hz — for those we refine the rate from the timer the tune programs (see
     * the PSID branch below). */
    bool cia_timed = (song_idx < 32) ? (bool)((speed >> song_idx) & 1) : true;
    play_hz = ntsc ? 60u : 50u;
    printf("Subtune : %u/%u\r\n", (unsigned)(song_idx + 1), (unsigned)songs);

    /* Tell the Display which song is now playing (1-based), so it shows the
     * current subtune.  Sent on initial play and on every subtune change. */
    send_meta_from_header(h, path, (uint8_t)(song_idx + 1));

    rsid_irq_active = false;

    /* Reset CIA1 state before (re)loading */
    memset(&cia1, 0, sizeof(cia1));

    /* Prepare 6502 memory: clear, fill KERNAL stubs, place sentinel */
    memset(memory, 0x00, sizeof(memory));
    memset(&memory[0xE000], 0x60, 0x1FFE);  /* RTS stubs for KERNAL area */
    memory[0xE000] = 0x40;                  /* RTI — NMI handler */

    /* IRQ dispatch at $FF48: bare JMP ($0314) — no push/pop.
     *
     * We intentionally do NOT replicate the real KERNAL's PHA/TXA/PHA/TYA/PHA
     * sequence.  RSID play routines fall into two families:
     *   A) Self-contained: push A/X/Y themselves, end with RTI.
     *      With our bare dispatch the interrupt frame (P + PC) is the only
     *      thing on the stack; their own push/RTI sequence is balanced. ✓
     *   B) KERNAL-integrated: assume nothing was pushed, end with JMP $EA31.
     *      Our $EA31 = RTI, which cleanly pops the interrupt frame. ✓
     *
     * A full KERNAL push breaks family (A): their RTI would pop the KERNAL's
     * Y/X/A bytes as flags+PC and jump to garbage. */
    /* $FF48 dispatches via $0316 (secondary vector we control).
     * We copy $0314 → $0316 after INIT, then reset $0314 → RTI.
     * Tunes that loop via JMP ($0314) hit RTI instead of re-entering. */
    memory[0xFF48] = 0x6C; memory[0xFF49] = 0x16; memory[0xFF4A] = 0x03; /* JMP ($0316) */

    /* Common KERNAL IRQ exit addresses — all become RTI. */
    memory[0xEA31] = 0x40;
    memory[0xEA81] = 0x40;
    memory[0xFE66] = 0x40;

    /* Default vectors: $0314/$0316 → $E000 (RTI) until INIT installs a handler */
    memory[0x0314] = 0x00; memory[0x0315] = 0xE0;
    memory[0x0316] = 0x00; memory[0x0317] = 0xE0;

    memory[0xFFFA] = 0x00; memory[0xFFFB] = 0xE0;  /* NMI vector → $E000 */
    memory[0xFFFE] = 0x48; memory[0xFFFF] = 0xFF;  /* IRQ vector → $FF48 */
    memory[SENTINEL] = 0x60;                        /* RTS at sentinel */

    /* (Re)load the binary for this subtune into 6502 address space.  The file is
     * kept open across subtunes; we just re-seek to the resolved data start. */
    if (f_lseek(&fil, data_start) != FR_OK ||
        f_read(&fil, &memory[load_addr], 0x10000u - load_addr, &br) != FR_OK) {
        printf("Data read failed\r\n");
        f_close(&fil);
        return 0;
    }

    /* Initialise 6502 and call INIT for this subtune */
    M6502 cpu = {0};
    cpu.read  = cpu_read;
    cpu.write = cpu_write;
    m6502_power(&cpu, TRUE);
    call_routine(&cpu, init_addr, song_idx, 0, 0);

    if (play_addr != 0) {
        /* CIA-timed PSID: if INIT programmed CIA1 timer A, derive the real poll
         * rate from it (e.g. $4CC7 → 50 Hz on PAL) instead of leaving the
         * video-standard default — many "speed flag set" PAL tunes are 50 Hz. */
        if (cia_timed) {
            uint32_t cpu_hz = ntsc ? 1022727u : 985248u;
            uint32_t tv = ((uint32_t)cia1.timer_hi << 8) | cia1.timer_lo;
            if (tv) {
                uint32_t hz = (cpu_hz + (tv + 1) / 2) / (tv + 1);  /* rounded */
                if (hz >= 20 && hz <= 300) play_hz = hz;
            }
        }
        printf("PSID poll rate: %u Hz\r\n", (unsigned)play_hz);

        /* PSID: poll the play routine at the declared rate */
        const uint64_t frame_us  = 1000000ULL / play_hz;
        uint64_t       next_frame = time_us_64() + frame_us;
        uint64_t dbg_t0 = time_us_64(); uint32_t dbg_n = 0;
        dbg_sid_writes = 0;
        for (;;) {
            call_routine(&cpu, play_addr, 0, 0, 0);
            if (++dbg_n == 50) {
                uint32_t dt = (uint32_t)(time_us_64() - dbg_t0);
                printf("50 frames: %u us (%u Hz)  SID/frame: %u\r\n",
                       dt, 50000000u / dt, dbg_sid_writes / 50);
                dbg_t0 = time_us_64(); dbg_n = 0;
                dbg_sid_writes = 0;
            }
            stop_cmd = poll_cmd();
            if (stop_cmd == CMD_REQUEST_META) {  /* resend current song; keep playing */
                send_meta_from_header(h, path, (uint8_t)(song_idx + 1));
                stop_cmd = 0;
            }
            if (stop_cmd == CMD_NEXT || stop_cmd == CMD_PREV || stop_cmd == CMD_STOP ||
                stop_cmd == CMD_NEXT_TUNE || stop_cmd == CMD_PREV_TUNE)
                break;
            if (stop_cmd == CMD_REQUEST_META)
                send_meta_from_header(h, path, (uint8_t)(song_idx + 1));
            while (time_us_64() < next_frame)
                ;
            next_frame += frame_us;
        }
    } else {
        /* RSID: CIA1 timer A drives IRQ-based playback.
         * After INIT the tune has written its play routine into $0314-$0315
         * and programmed the CIA1 timer.  We fire m6502_irq() at that rate
         * and let the KERNAL trampoline at $FF48 dispatch to the tune. */
        uint32_t cpu_hz_val = ntsc ? 1022727u : 985248u;
        uint32_t timer_val  = ((uint32_t)cia1.timer_hi << 8) | cia1.timer_lo;
        if (timer_val == 0 || !cia1.active)
            timer_val = ntsc ? 0x4295u : 0x4CC5u;  /* fallback ~60 / ~50 Hz */
        uint32_t rsid_hz = cpu_hz_val / (timer_val + 1);
        if (rsid_hz < 20 || rsid_hz > 300)
            rsid_hz = ntsc ? 60u : 50u;
        /* Copy INIT's play-routine vector from $0314 to $0316, then reset
         * $0314 → RTI.  Tunes that end via JMP ($0314) (self-re-entry loop)
         * now hit RTI and exit cleanly instead of looping within the budget.
         *
         * Fallback: some tunes (e.g. Martin Galway) bypass the KERNAL vector
         * table and write directly to the hardware IRQ vector $FFFE/$FFFF.
         * If $0314 is still at our default ($E000) after INIT, check whether
         * INIT changed $FFFE/$FFFF away from our $FF48 stub and use that. */
        memory[0x0316] = memory[0x0314];
        memory[0x0317] = memory[0x0315];
        memory[0x0314] = 0x00; memory[0x0315] = 0xE0;  /* $0314 → $E000 = RTI */
        {
            uint16_t via_0316 = (uint16_t)memory[0x0316] | ((uint16_t)memory[0x0317] << 8);
            uint16_t via_fffe = (uint16_t)memory[0xFFFE] | ((uint16_t)memory[0xFFFF] << 8);
            if (via_0316 == 0xE000 && via_fffe != 0xFF48) {
                memory[0x0316] = memory[0xFFFE];
                memory[0x0317] = memory[0xFFFF];
                memory[0xFFFE] = 0x48; memory[0xFFFF] = 0xFF;  /* restore our stub */
            }
        }

        rsid_play_entry   = (uint16_t)memory[0x0316] | ((uint16_t)memory[0x0317] << 8);
        rsid_play_orig[0] = memory[rsid_play_entry];
        rsid_play_orig[1] = memory[rsid_play_entry + 1];
        rsid_play_orig[2] = memory[rsid_play_entry + 2];

        {
            uint16_t play_vec = rsid_play_entry;
            printf("CIA1 timer $%04X -> %u Hz\r\n", timer_val, (unsigned)rsid_hz);
            printf("$0316 -> $%04X  S=$%02X  first bytes: %02X %02X %02X %02X\r\n",
                   (unsigned)play_vec, (unsigned)cpu.state.s,
                   memory[play_vec], memory[play_vec+1],
                   memory[play_vec+2], memory[play_vec+3]);
        }

        /* Idle loop at $E100 (deep in KERNAL stub area — tune cannot reach it).
         * Using $0200 is unsafe: C64 music drivers routinely use $0200-$02FF
         * as work RAM, so RTI returning to $0200 could land on a BRK ($00)
         * that re-fires the play routine inside the same m6502_run budget. */
        memory[0xE100] = 0x4C; memory[0xE101] = 0x00; memory[0xE102] = 0xE1;
        cpu.state.pc = 0xE100;
        cpu.state.p &= ~0x04u;  /* I=0: matches real C64 (KERNAL did CLI) */

        const uint64_t frame_us        = 1000000ULL / rsid_hz;
        uint64_t       next_frame      = time_us_64() + frame_us;
        zusize         cycles_per_frame = (zusize)(cpu_hz_val / rsid_hz);

        current_cpu     = &cpu;
        rsid_irq_active = true;

        uint64_t dbg_t0  = time_us_64();
        uint32_t dbg_n   = 0;
        dbg_sid_writes   = 0;

        for (;;) {
            /* Per-frame setup: restore original bytes at play entry (undoes any
             * redirect patch from the previous frame) and reset the write counter. */
            rsid_frame_s      = cpu.state.s;
            rsid_frame_writes = 0;
            if (rsid_play_entry) {
                memory[rsid_play_entry]     = rsid_play_orig[0];
                memory[rsid_play_entry + 1] = rsid_play_orig[1];
                memory[rsid_play_entry + 2] = rsid_play_orig[2];
            }

            /* Manually inject the hardware interrupt sequence: push PCH, PCL,
             * P (with B=0) then set I=1 and dispatch via the $FF48 trampoline.
             * The play routine ends with RTI which pops this frame and returns
             * to the idle loop at $E100 for the remainder of the budget.
             * If the tune loops via direct JMP back to its entry, cpu_read patches
             * JMP $E100 there on second entry; we then restore S so the leaked
             * interrupt frame bytes don't accumulate on the stack. */
            memory[0x0100 + cpu.state.s] = 0xE1;                /* PCH of $E100 */
            cpu.state.s--;
            memory[0x0100 + cpu.state.s] = 0x00;                /* PCL of $E100 */
            cpu.state.s--;
            memory[0x0100 + cpu.state.s] = cpu.state.p & ~0x10u; /* P with B=0 */
            cpu.state.s--;
            cpu.state.p |= 0x04u;   /* I=1 during handler */
            cpu.state.pc  = 0xFF48; /* JMP ($0316) trampoline */

            m6502_run(&cpu, cycles_per_frame);

            /* Restore stack pointer and I flag regardless of whether the routine
             * exited via RTI (normal) or was redirected to $E100 (looping tune). */
            cpu.state.s  = rsid_frame_s;
            cpu.state.p &= ~0x04u;  /* I=0 for next frame */

            if (++dbg_n == 50) {
                uint32_t dt = (uint32_t)(time_us_64() - dbg_t0);
                printf("50 frames: %u us (%u Hz)  SID/frame: %u\r\n",
                       dt, 50000000u / dt, dbg_sid_writes / 50);
                dbg_t0         = time_us_64();
                dbg_n          = 0;
                dbg_sid_writes = 0;
            }

            stop_cmd = poll_cmd();
            if (stop_cmd == CMD_REQUEST_META) {  /* resend current song; keep playing */
                send_meta_from_header(h, path, (uint8_t)(song_idx + 1));
                stop_cmd = 0;
            }
            if (stop_cmd == CMD_NEXT || stop_cmd == CMD_PREV || stop_cmd == CMD_STOP ||
                stop_cmd == CMD_NEXT_TUNE || stop_cmd == CMD_PREV_TUNE)
                break;
            if (stop_cmd == CMD_REQUEST_META)
                send_meta_from_header(h, path, (uint8_t)(song_idx + 1));
            while (time_us_64() < next_frame)
                ;
            next_frame += frame_us;
        }
        rsid_irq_active = false;
        current_cpu     = NULL;
    }

    /* Subtune navigation: change the song and restart playback in this same
     * file (no reload of the SIDKick profile / clock).  File-level commands
     * (NEXT/PREV/STOP) fall through to exit. */
    if (stop_cmd == CMD_NEXT_TUNE || stop_cmd == CMD_PREV_TUNE) {
        if (stop_cmd == CMD_NEXT_TUNE)
            song_idx = (uint8_t)((song_idx + 1) % songs);
        else
            song_idx = (song_idx == 0) ? (uint8_t)(songs - 1) : (uint8_t)(song_idx - 1);
        goto play_subtune;   /* play_subtune sends the updated song to the Display */
    }

    *song_sel = song_idx;    /* report the last-played subtune back to the caller */
    f_close(&fil);

    /* Silence both SIDs before the next tune.  SID1 directly; SID2 with the
     * $D500-detect line high so the SIDKick routes the zero-writes to it
     * (harmless if SID2 is disabled, e.g. a mono tune). */
    for (uint8_t r = 0; r < 0x19; r++)
        sid_write(r, 0x00);
    gpio_put(SID_D500, 1);
    for (uint8_t r = 0; r < 0x19; r++)
        sid_write(r, 0x00);
    gpio_put(SID_D500, 0);
    sleep_ms(300);
    return stop_cmd;
}

/* Count .sid files in the root of drive */
static uint16_t count_sid_files(const char *drive)
{
    DIR dir; FILINFO fno; uint16_t n = 0;
    if (f_opendir(&dir, drive) != FR_OK) return 0;
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0])
        if (!(fno.fattrib & AM_DIR) && is_sid_file(fno.fname)) n++;
    f_closedir(&dir);
    return n;
}

/* Fill buf with the path to the idx-th .sid file (0-based).
 * Returns false if idx is out of range or the directory can't be opened. */
static bool get_sid_path(const char *drive, uint16_t idx,
                          char *buf, size_t buflen)
{
    DIR dir; FILINFO fno; uint16_t n = 0; bool found = false;
    if (f_opendir(&dir, drive) != FR_OK) return false;
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if (fno.fattrib & AM_DIR) continue;
        if (!is_sid_file(fno.fname)) continue;
        if (n == idx) {
            snprintf(buf, buflen, "%s/%s", drive, fno.fname);
            found = true;
            break;
        }
        n++;
    }
    f_closedir(&dir);
    return found;
}

/* Read all 64 SIDKick config bytes.  Returns true on a valid read.  Self-contained.
 *
 * On a cold SIDKick the first config session often doesn't engage — $D41D then
 * returns the 0x4C launch stub (or floating 0xFF, or all 0x00).  Two layers of
 * robustness, the same that made writes reliable:
 *   - establish config mode IN-SESSION (re-enter on a stable slow clock until a
 *     probe read returns a real config byte) rather than cycling the clock; and
 *   - validate the whole 64-byte result and retry the session if it's garbage.
 * A valid read has cfg[0] = a real SID type (0-3) and is not a single repeated
 * byte (every failure mode is all 0x4C / 0xFF / 0x00). */
static bool sidkick_read_config(uint8_t cfg[64])
{
    for (int attempt = 0; attempt < 4; attempt++) {
        sidkick_config_begin();

        for (int k = 0; k < 16; k++) {  /* establish config mode in-session */
            cfg_write(0x1F, 0xFF);      /* enter config mode */
            (void)cfg_read(0x1D);       /* absorb 0x4C stub  */
            cfg_write(0x1E, 0x00);      /* reset pointer     */
            if (cfg_read(0x1D) <= 3)    /* probe config[0]: engaged? */
                break;
        }

        cfg_write(0x1E, 0x00);          /* stateConfigRegisterAccess = 0 */
        for (int i = 0; i < 64; i++)
            cfg[i] = cfg_read(0x1D);    /* each read returns config[i], auto-increments */

        sidkick_config_end();

        /* Validate: a real config has these firm invariants.  This also rejects
         * a partially-garbage read (stray 0xFF bytes) which, if written back,
         * could put a command value (0xFB reset / 0xFE / 0xFF) on $D41D and hang
         * the SIDKick. */
        bool all_same = true;
        for (int i = 1; i < 64; i++)
            if (cfg[i] != cfg[0]) { all_same = false; break; }
        if (!all_same &&
            cfg[0] <= 3 &&              /* SID1 type   */
            cfg[8] <= 3 &&              /* SID2 type   */
            cfg[2] <= 1 &&              /* register-read flag (0/1) */
            cfg[3] <= 15 &&             /* SID1 volume (0-14) */
            cfg[11] <= 15)              /* SID2 volume (0-14) */
            return true;
    }
    return false;                       /* never engaged — caller must not apply garbage */
}

/* Write all 64 config bytes then apply them live (0xFE = apply without writing
 * flash, so changes are temporary and revert on power cycle).  Self-contained.
 *
 * Cold-start writes only take once config mode is genuinely engaged, so first
 * establish it with an in-session read probe (re-enter until cfg[0] is a valid
 * SID type), then write in that SAME session without dropping out. */
static void sidkick_write_config(const uint8_t cfg[64])
{
    sidkick_config_begin();

    for (int k = 0; k < 8; k++) {
        cfg_write(0x1F, 0xFF);      /* enter config mode */
        (void)cfg_read(0x1D);       /* absorb 0x4C stub  */
        cfg_write(0x1E, 0x00);      /* reset pointer     */
        if (cfg_read(0x1D) <= 3)    /* probe config[0]: engaged? */
            break;
    }

    cfg_write(0x1E, 0x00);          /* stateConfigRegisterAccess = 0 */
    for (int i = 0; i < 64; i++) {
        /* On $D41D a write of 0xFB is "reset", 0xFE "apply", 0xFF "save to
         * flash" — commands, not data.  A real config byte is never that high
         * (max ~0xAA), so clamp to 0xFA: a stray/garbage byte can never reset or
         * hang the SIDKick. */
        uint8_t v = cfg[i] > 0xFA ? 0xFA : cfg[i];
        cfg_write(0x1D, v);         /* config[i] = v, auto-increments */
    }

    cfg_write(0x1D, 0xFE);          /* apply live (no flash write)   */

    sidkick_config_end();
}

static void sidkick_print_config_decoded(const uint8_t *cfg)
{
    static const char * const sid_type[] = {
        "6581", "8580", "8580+digiboost", "disabled"
    };

    printf("Raw config bytes:\r\n");
    for (int i = 0; i < 64; i++)
        printf("[%02d]=0x%02X%s", i, cfg[i], (i % 8 == 7) ? "\r\n" : " ");

    printf("\r\n--- SIDKick Configuration ---\r\n");
    printf("SID1 type        : %u (%s)\r\n",  cfg[0],  cfg[0]  <= 3 ? sid_type[cfg[0]]  : "?");
    printf("SID1 digiboost   : %u\r\n",        cfg[1]);
    printf("SID1 volume      : %u\r\n",        cfg[3]);
    printf("Register reads   : %u (%s)\r\n",  cfg[2],  cfg[2]  ? "enabled" : "disabled");
    printf("SID2 type        : %u (%s)\r\n",  cfg[8],  cfg[8]  <= 3 ? sid_type[cfg[8]]  : "?");
    printf("SID2 digiboost   : %u\r\n",        cfg[9]);
    printf("SID2 address idx : %u\r\n",        cfg[10]);
    printf("SID2 volume      : %u\r\n",        cfg[11]);
    printf("SID panning      : %u\r\n",        cfg[12]);
    printf("SID balance      : %u (7=centre)\r\n", cfg[58]);
    printf("Clock speed      : %u (%s)\r\n",  cfg[59], cfg[59] == 0 ? "PAL" : "NTSC");
    printf("Digi detect      : %u (%s)\r\n",  cfg[61], cfg[61] ? "on" : "off");
    printf("Pot filter       : %u\r\n",        cfg[60]);
    printf("Paddle offset    : %u\r\n",        cfg[39]);
    printf("-----------------------------\r\n\r\n");
}

static void sidkick_print_config(void)
{
    uint8_t cfg[64];
    sidkick_read_config(cfg);
    sidkick_print_config_decoded(cfg);
}

/* Apply the mono or stereo-centered profile.  Overlays only the relevant indices
 * onto the current config (filter calibration/checksums preserved) and writes
 * back only if something changed.  stereo=false → mono, stereo=true → stereo.
 *
 * Crucially: touch the config bus ONLY when the profile actually changes.  Every
 * config read/write switches phi2 to 294 kHz and enters config mode; doing that
 * per-tune eventually leaves the SIDKick deaf to SID writes (recoverable only by
 * a 5 V power cycle — not /RES).  So a run of same-profile tunes does ZERO config
 * access. */
static void sidkick_apply_profile(bool stereo, bool ntsc)
{
    static int last_profile = -1;   /* (stereo<<1)|ntsc of the last applied profile */
    int profile = (stereo ? 2 : 0) | (ntsc ? 1 : 0);
    if (profile == last_profile) {
        printf("SIDKick profile unchanged (%s%s) — no config access\r\n",
               stereo ? "stereo" : "mono", ntsc ? ", NTSC" : "");
        return;
    }

    uint8_t cfg[64];
    if (!sidkick_read_config(cfg)) {
        /* Read never engaged — do NOT write/apply garbage (that misconfigures
         * the SID and kills playback).  Leave the SIDKick config untouched and
         * do NOT cache the profile, so we retry on the next tune. */
        printf("SIDKick config read failed — leaving config unchanged\r\n");
        return;
    }

    uint8_t want[64];
    memcpy(want, cfg, 64);

    /* Common to both profiles */
    want[0]  = 1;             /* SID1 = 8580            */
    want[2]  = 1;             /* register reads enabled */
    want[3]  = 14;            /* SID1 volume max        */
    want[58] = 7;             /* balance centre         */
    want[59] = ntsc ? 1 : 0;  /* clock tracks SID header: 0=PAL, 1=NTSC */

    if (stereo) {
        want[8]  = 1;    /* SID2 = 8580 (enabled) */
        want[10] = 2;    /* SID2 address = $D500  */
        want[11] = 14;   /* SID2 volume max       */
        want[12] = 7;    /* panning centred       */
    } else {
        want[1]  = 12;   /* SID1 digiboost     */
        want[8]  = 3;    /* SID2 disabled (mono) */
        want[60] = 16;   /* pot filter default */
        want[61] = 0;    /* digi detect off    */
    }

    /* Only write/apply if something actually changed.  Re-applying (the 0xFE
     * apply makes the SIDKick reinitialize reSID) on every tune — even when the
     * config already matches — can leave it deaf to SID writes.  So skip the
     * write entirely when the SIDKick is already configured the way we want. */
    if (memcmp(want, cfg, 64) == 0) {
        printf("SIDKick already configured for %s — no change\r\n",
               stereo ? "stereo" : "mono");
        last_profile = profile;     /* cache: skip the bus next same-profile tune */
        return;
    }

    sidkick_write_config(want);
    last_profile = profile;
    printf("\r\n>>> Applied %s profile <<<\r\n", stereo ? "STEREO CENTRED" : "MONO");
}

int main(void)
{
    stdio_init_all();
    pio_uart_init();
    sid_init();
    sidkick_print_config();   /* show current SIDKick config at boot */

    sd_card_t *sd = sd_get_by_num(0);
    FRESULT fr = f_mount(&sd->fatfs, sd->pcName, 1);
    if (fr != FR_OK) {
        printf("SD mount failed (%d)\r\n", fr);
        for (;;) tight_loop_contents();
    }

    uint16_t total = count_sid_files(sd->pcName);
    if (total == 0) {
        printf("No .sid files found\r\n");
        for (;;) tight_loop_contents();
    }
    printf("%u .sid files found\r\n", total);

    uint16_t idx        = 0;
    char     path[64];
    uint8_t  song_sel   = 0;      /* selected subtune (0-based) for the current file */
    bool     reset_song = true;   /* reset selection to the file default on file change */

    for (;;) {
        /* Load the current file's header */
        if (!get_sid_path(sd->pcName, idx, path, sizeof(path))) {
            idx = (uint16_t)((idx + 1) % total);
            continue;
        }
        uint8_t hdr[0x7C];
        if (!read_sid_header(path, hdr)) {
            idx = (uint16_t)((idx + 1) % total);
            continue;
        }
        const SIDHeader *h = (const SIDHeader *)hdr;
        uint16_t songs = be16(h->songs); if (songs == 0) songs = 1;
        if (reset_song) {
            uint16_t start = be16(h->start_song);
            song_sel   = (start >= 1) ? (uint8_t)(start - 1) : 0;
            reset_song = false;
        }
        if (song_sel >= songs) song_sel = 0;

        /* Show the (selected) song on the Display */
        send_meta_from_header(h, path, (uint8_t)(song_sel + 1));

        /* Wait for an actionable command.  While idle, NEXT_TUNE/PREV_TUNE select
         * the subtune to play (within this file); REQUEST_META resends. */
        uint8_t cmd = 0;
        for (;;) {
            cmd = poll_cmd();
            if (cmd == CMD_PLAY || cmd == CMD_NEXT || cmd == CMD_PREV)
                break;
            if (cmd == CMD_NEXT_TUNE)
                song_sel = (uint8_t)((song_sel + 1) % songs);
            else if (cmd == CMD_PREV_TUNE)
                song_sel = (song_sel == 0) ? (uint8_t)(songs - 1) : (uint8_t)(song_sel - 1);
            else if (cmd != CMD_REQUEST_META)
                continue;   /* CMD_STOP / 0: nothing to do, keep waiting */
            send_meta_from_header(h, path, (uint8_t)(song_sel + 1));  /* reflect selection */
        }

        if (cmd == CMD_PLAY)
            cmd = play_sid_file(path, &song_sel);  /* plays from song_sel, updates it */

        if (cmd == CMD_NEXT) {
            idx = (uint16_t)((idx + 1) % total);
            reset_song = true;
        } else if (cmd == CMD_PREV) {
            idx = (idx > 0) ? (uint16_t)(idx - 1) : (uint16_t)(total - 1);
            reset_song = true;
        }
        /* CMD_STOP or 0: same file; keep song_sel (last-played subtune) */
    }
}
