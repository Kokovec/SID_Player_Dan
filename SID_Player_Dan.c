#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "emulation/CPU/6502.h"
#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#include "tft.h"

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
 * R/W    : tied to GND (write-only)
 * /RES   : GP15
 * phi2   : GP16      PWM ~0.985 MHz
 */
#define SID_D0     0
#define SID_A0     8
#define SID_CS_N  13
#define SID_RES_N 15
#define SID_CLK   16

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
    gpio_init(SID_RES_N); gpio_set_dir(SID_RES_N, GPIO_OUT); gpio_put(SID_RES_N, 0);

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
/* Next-track button — GP22, active low, internal pull-up             */

#define BTN_GPIO 22

static void btn_init(void)
{
    gpio_init(BTN_GPIO);
    gpio_set_dir(BTN_GPIO, GPIO_IN);
    gpio_pull_up(BTN_GPIO);
}

/* Returns true once when a clean press+release is detected.
 * Debounces by requiring the line to stay low for 20 ms, then waits
 * for release before returning so callers never see the same press twice. */
static bool btn_check(void)
{
    if (gpio_get(BTN_GPIO)) return false;   /* not pressed */
    sleep_ms(20);                            /* debounce    */
    if (gpio_get(BTN_GPIO)) return false;   /* was a bounce */
    while (!gpio_get(BTN_GPIO)) tight_loop_contents();  /* wait for release */
    return true;
}

/* ------------------------------------------------------------------ */

static uint8_t memory[65536];

static zuint8 cpu_read(void *ctx, zuint16 addr)
{
    (void)ctx;
    return memory[addr];
}

static void cpu_write(void *ctx, zuint16 addr, zuint8 val)
{
    (void)ctx;
    memory[addr] = val;
    if (addr >= 0xD400 && addr <= 0xD41C)
        sid_write((uint8_t)(addr & 0x1F), val);
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
    if (n < 5) return false;
    const char *e = name + n - 4;
    return e[0] == '.' &&
           (e[1] == 's' || e[1] == 'S') &&
           (e[2] == 'i' || e[2] == 'I') &&
           (e[3] == 'd' || e[3] == 'D');
}

/* Load a SID file from the SD card, run INIT, then call PLAY at the
 * correct rate for PLAY_SECONDS.  Returns false if the file is invalid
 * or unplayable (RSID / missing play address). */
static bool play_sid_file(const char *path)
{
    FIL    fil;
    UINT   br;
    FRESULT fr;

    fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK) {
        printf("Cannot open %s (%d)\r\n", path, fr);
        return false;
    }

    /* Read header — v2 is 0x7C bytes; v1 is 0x76.  Read the larger size;
     * for v1 files the extra bytes are unused padding. */
    uint8_t hdr[0x7C];
    fr = f_read(&fil, hdr, sizeof(hdr), &br);
    if (fr != FR_OK || br < 0x76) {
        printf("Header read failed\r\n");
        f_close(&fil);
        return false;
    }

    const SIDHeader *h = (const SIDHeader *)hdr;
    if (memcmp(h->magic, "PSID", 4) != 0 && memcmp(h->magic, "RSID", 4) != 0) {
        printf("Not a SID file: %.4s\r\n", h->magic);
        f_close(&fil);
        return false;
    }

    uint16_t version     = be16(h->version);
    uint16_t data_offset = be16(h->data_offset);
    uint16_t load_addr   = be16(h->load_address);
    uint16_t init_addr   = be16(h->init_address);
    uint16_t play_addr   = be16(h->play_address);
    uint16_t start_song  = be16(h->start_song);
    uint32_t speed       = be32(h->speed);

    if (play_addr == 0) {
        /* RSID / NMI-driven tunes require CIA emulation — skip for now */
        printf("Skipping %s: play_addr=0 (interrupt-driven)\r\n", path);
        f_close(&fil);
        return false;
    }

    /* Determine clock (PAL / NTSC) and play rate from header flags */
    bool ntsc = false;
    if (version >= 2 && data_offset >= 0x7C) {
        uint8_t clk = (be16(h->flags) >> 2) & 0x3;
        ntsc = (clk == 2);  /* 1=PAL, 2=NTSC, 3=both → prefer PAL */
    }
    uint8_t  song_idx  = (start_song >= 1) ? (uint8_t)(start_song - 1) : 0;
    bool     cia_timer = (song_idx < 32) ? (bool)((speed >> song_idx) & 1) : true;
    uint32_t play_hz   = (ntsc || cia_timer) ? 60u : 50u;

    /* Print what we're about to play */
    char title[33], author[33], released[33];
    memcpy(title,    h->title,    32); title[32]    = '\0';
    memcpy(author,   h->author,   32); author[32]   = '\0';
    memcpy(released, h->released, 32); released[32] = '\0';
    printf("\r\n=== %s ===\r\n", path);
    printf("Title   : %s\r\n", title);
    printf("Author  : %s\r\n", author);
    printf("Released: %s\r\n", released);
    printf("Songs   : %u  (default %u)\r\n", be16(h->songs), start_song);
    printf("Clock   : %s   Play rate: %u Hz\r\n", ntsc ? "NTSC" : "PAL", (unsigned)play_hz);

    tft_show_track(path, title, author, released,
                   be16(h->songs), start_song, ntsc, play_hz);

    /* Set phi2 clock for this tune's region */
    sid_set_clock(ntsc);

    /* Prepare 6502 memory: clear, fill KERNAL stubs, place sentinel */
    memset(memory, 0x00, sizeof(memory));
    memset(&memory[0xE000], 0x60, 0x1FFE);  /* RTS stubs for KERNAL area */
    memory[0xE000] = 0x40;                  /* RTI — NMI/IRQ handler */
    memory[0xFFFA] = 0x00; memory[0xFFFB] = 0xE0;  /* NMI vector → $E000 */
    memory[0xFFFE] = 0x00; memory[0xFFFF] = 0xE0;  /* IRQ vector → $E000 */
    memory[SENTINEL] = 0x60;               /* RTS at sentinel */

    /* Seek to binary data, resolve embedded load address if needed */
    fr = f_lseek(&fil, data_offset);
    if (fr != FR_OK) { f_close(&fil); return false; }

    if (load_addr == 0) {
        uint8_t ab[2];
        if (f_read(&fil, ab, 2, &br) != FR_OK || br < 2) {
            f_close(&fil);
            return false;
        }
        load_addr = ab[0] | ((uint16_t)ab[1] << 8);
    }
    /* PSID spec: init_addr == 0 → use load address */
    if (init_addr == 0)
        init_addr = load_addr;
    printf("Load    : $%04X  Init: $%04X  Play: $%04X\r\n",
           load_addr, init_addr, play_addr);

    /* Read binary directly into 6502 address space */
    uint32_t space = 0x10000u - load_addr;
    fr = f_read(&fil, &memory[load_addr], space, &br);
    f_close(&fil);
    if (fr != FR_OK) { printf("Data read failed\r\n"); return false; }
    printf("Loaded  : %u bytes\r\n", (unsigned)br);

    /* Initialise 6502 and call INIT */
    M6502 cpu = {0};
    cpu.read  = cpu_read;
    cpu.write = cpu_write;
    m6502_power(&cpu, TRUE);
    call_routine(&cpu, init_addr, song_idx, 0, 0);

    /* Play loop — runs until the next-track button is pressed */
    const uint64_t frame_us  = 1000000ULL / play_hz;
    uint64_t       next_frame = time_us_64() + frame_us;

    for (;;) {
        call_routine(&cpu, play_addr, 0, 0, 0);
        if (btn_check()) break;
        while (time_us_64() < next_frame)
            ;
        next_frame += frame_us;
    }

    /* Silence SID chip before next tune */
    for (uint8_t r = 0; r < 0x19; r++)
        sid_write(r, 0x00);
    sleep_ms(300);

    return true;
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

int main(void)
{
    stdio_init_all();
    tft_init();
    sid_init();
    btn_init();

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

    uint16_t idx = 0;
    for (;;) {
        char path[64];
        if (get_sid_path(sd->pcName, idx, path, sizeof(path)))
            play_sid_file(path);
        idx = (uint16_t)((idx + 1) % total);
    }
}
