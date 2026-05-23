#include "tft.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "ff.h"
#include "tjpgd.h"
#include <string.h>
#include <stdio.h>

/* SPI1 pins — clear of SID bus (GP0-16), SD card (GP17-20), button (GP22)
 * GP23/24/25 are not on the header.
 * RES and BL are wired directly to 3.3V — no GPIOs needed for them. */
#define TFT_SCK  26
#define TFT_SDA  27
#define TFT_CS   21
#define TFT_DC   28
#define TFT_SPI  spi1

/* ST7735 command bytes */
#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT  0x11
#define ST7735_NORON   0x13
#define ST7735_INVOFF  0x20
#define ST7735_DISPON  0x29
#define ST7735_CASET   0x2A
#define ST7735_RASET   0x2B
#define ST7735_RAMWR   0x2C
#define ST7735_MADCTL  0x36
#define ST7735_COLMOD  0x3A
#define ST7735_FRMCTR1 0xB1
#define ST7735_FRMCTR2 0xB2
#define ST7735_FRMCTR3 0xB3
#define ST7735_INVCTR  0xB4
#define ST7735_PWCTR1  0xC0
#define ST7735_PWCTR2  0xC1
#define ST7735_PWCTR3  0xC2
#define ST7735_PWCTR4  0xC3
#define ST7735_PWCTR5  0xC4
#define ST7735_VMCTR1  0xC5
#define ST7735_GMCTRP1 0xE0
#define ST7735_GMCTRN1 0xE1

static inline void cs_lo(void) { gpio_put(TFT_CS, 0); }
static inline void cs_hi(void) { gpio_put(TFT_CS, 1); }

/* Send a command byte followed by optional data bytes, CS held low throughout */
static void send_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    cs_lo();
    gpio_put(TFT_DC, 0);
    spi_write_blocking(TFT_SPI, &cmd, 1);
    if (len) {
        gpio_put(TFT_DC, 1);
        spi_write_blocking(TFT_SPI, data, len);
    }
    cs_hi();
}

static void set_addr_window(int x0, int y0, int x1, int y1)
{
    uint8_t caset[4] = {0, (uint8_t)x0, 0, (uint8_t)x1};
    uint8_t raset[4] = {0, (uint8_t)y0, 0, (uint8_t)y1};
    send_cmd(ST7735_CASET, caset, 4);
    send_cmd(ST7735_RASET, raset, 4);
    send_cmd(ST7735_RAMWR, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Standard Adafruit-compatible init for 1.8" 128×160 ST7735R display */

static void hw_init(void)
{
    sleep_ms(120); /* allow display to power up before first command */
    send_cmd(ST7735_SWRESET, NULL, 0); sleep_ms(150);
    send_cmd(ST7735_SLPOUT,  NULL, 0); sleep_ms(500);

    { static const uint8_t d[] = {0x01,0x2C,0x2D}; send_cmd(ST7735_FRMCTR1, d, 3); }
    { static const uint8_t d[] = {0x01,0x2C,0x2D}; send_cmd(ST7735_FRMCTR2, d, 3); }
    { static const uint8_t d[] = {0x01,0x2C,0x2D,0x01,0x2C,0x2D}; send_cmd(ST7735_FRMCTR3, d, 6); }
    { static const uint8_t d[] = {0x07}; send_cmd(ST7735_INVCTR, d, 1); }

    { static const uint8_t d[] = {0xA2,0x02,0x84}; send_cmd(ST7735_PWCTR1, d, 3); }
    { static const uint8_t d[] = {0xC5};            send_cmd(ST7735_PWCTR2, d, 1); }
    { static const uint8_t d[] = {0x0A,0x00};       send_cmd(ST7735_PWCTR3, d, 2); }
    { static const uint8_t d[] = {0x8A,0x2A};       send_cmd(ST7735_PWCTR4, d, 2); }
    { static const uint8_t d[] = {0x8A,0xEE};       send_cmd(ST7735_PWCTR5, d, 2); }
    { static const uint8_t d[] = {0x0E};            send_cmd(ST7735_VMCTR1, d, 1); }

    send_cmd(ST7735_INVOFF, NULL, 0);
    { static const uint8_t d[] = {0x68}; send_cmd(ST7735_MADCTL, d, 1); } /* MX+MV: 90° CW landscape, RGB */
    { static const uint8_t d[] = {0x05}; send_cmd(ST7735_COLMOD, d, 1); } /* 16-bit colour */

    {
        static const uint8_t d[] = {0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,
                                     0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10};
        send_cmd(ST7735_GMCTRP1, d, 16);
    }
    {
        static const uint8_t d[] = {0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,
                                     0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10};
        send_cmd(ST7735_GMCTRN1, d, 16);
    }

    send_cmd(ST7735_NORON,  NULL, 0); sleep_ms(10);
    send_cmd(ST7735_DISPON, NULL, 0); sleep_ms(100);
}

/* ------------------------------------------------------------------ */

void tft_init(void)
{
    spi_init(TFT_SPI, 10 * 1000 * 1000);
    gpio_set_function(TFT_SCK, GPIO_FUNC_SPI);
    gpio_set_function(TFT_SDA, GPIO_FUNC_SPI);

    gpio_init(TFT_CS); gpio_set_dir(TFT_CS, GPIO_OUT); gpio_put(TFT_CS, 1);
    gpio_init(TFT_DC); gpio_set_dir(TFT_DC, GPIO_OUT); gpio_put(TFT_DC, 1);

    hw_init();
}

/* ------------------------------------------------------------------ */

void tft_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    set_addr_window(x, y, x + w - 1, y + h - 1);

    uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)(color & 0xFF);
    uint8_t row[TFT_W * 2];
    for (int i = 0; i < w; i++) { row[i*2] = hi; row[i*2+1] = lo; }

    cs_lo();
    gpio_put(TFT_DC, 1);
    for (int i = 0; i < h; i++)
        spi_write_blocking(TFT_SPI, row, (size_t)(w * 2));
    cs_hi();
}

void tft_fill(uint16_t color)
{
    tft_fill_rect(0, 0, TFT_W, TFT_H, color);
}

/* ------------------------------------------------------------------ */
/* 5×7 font, one byte per column, bit 0 = top row, 95 printable chars */

static const uint8_t font5x7[][5] = {
  {0x00,0x00,0x00,0x00,0x00}, /* 0x20   */
  {0x00,0x00,0x5F,0x00,0x00}, /* 0x21 ! */
  {0x00,0x07,0x00,0x07,0x00}, /* 0x22 " */
  {0x14,0x7F,0x14,0x7F,0x14}, /* 0x23 # */
  {0x24,0x2A,0x7F,0x2A,0x12}, /* 0x24 $ */
  {0x23,0x13,0x08,0x64,0x62}, /* 0x25 % */
  {0x36,0x49,0x55,0x22,0x50}, /* 0x26 & */
  {0x00,0x05,0x03,0x00,0x00}, /* 0x27 ' */
  {0x00,0x1C,0x22,0x41,0x00}, /* 0x28 ( */
  {0x00,0x41,0x22,0x1C,0x00}, /* 0x29 ) */
  {0x14,0x08,0x3E,0x08,0x14}, /* 0x2A * */
  {0x08,0x08,0x3E,0x08,0x08}, /* 0x2B + */
  {0x00,0x50,0x30,0x00,0x00}, /* 0x2C , */
  {0x08,0x08,0x08,0x08,0x08}, /* 0x2D - */
  {0x00,0x60,0x60,0x00,0x00}, /* 0x2E . */
  {0x20,0x10,0x08,0x04,0x02}, /* 0x2F / */
  {0x3E,0x51,0x49,0x45,0x3E}, /* 0x30 0 */
  {0x00,0x42,0x7F,0x40,0x00}, /* 0x31 1 */
  {0x42,0x61,0x51,0x49,0x46}, /* 0x32 2 */
  {0x21,0x41,0x45,0x4B,0x31}, /* 0x33 3 */
  {0x18,0x14,0x12,0x7F,0x10}, /* 0x34 4 */
  {0x27,0x45,0x45,0x45,0x39}, /* 0x35 5 */
  {0x3C,0x4A,0x49,0x49,0x30}, /* 0x36 6 */
  {0x01,0x71,0x09,0x05,0x03}, /* 0x37 7 */
  {0x36,0x49,0x49,0x49,0x36}, /* 0x38 8 */
  {0x06,0x49,0x49,0x29,0x1E}, /* 0x39 9 */
  {0x00,0x36,0x36,0x00,0x00}, /* 0x3A : */
  {0x00,0x56,0x36,0x00,0x00}, /* 0x3B ; */
  {0x08,0x14,0x22,0x41,0x00}, /* 0x3C < */
  {0x14,0x14,0x14,0x14,0x14}, /* 0x3D = */
  {0x00,0x41,0x22,0x14,0x08}, /* 0x3E > */
  {0x02,0x01,0x51,0x09,0x06}, /* 0x3F ? */
  {0x32,0x49,0x79,0x41,0x3E}, /* 0x40 @ */
  {0x7E,0x11,0x11,0x11,0x7E}, /* 0x41 A */
  {0x7F,0x49,0x49,0x49,0x36}, /* 0x42 B */
  {0x3E,0x41,0x41,0x41,0x22}, /* 0x43 C */
  {0x7F,0x41,0x41,0x22,0x1C}, /* 0x44 D */
  {0x7F,0x49,0x49,0x49,0x41}, /* 0x45 E */
  {0x7F,0x09,0x09,0x09,0x01}, /* 0x46 F */
  {0x3E,0x41,0x49,0x49,0x7A}, /* 0x47 G */
  {0x7F,0x08,0x08,0x08,0x7F}, /* 0x48 H */
  {0x00,0x41,0x7F,0x41,0x00}, /* 0x49 I */
  {0x20,0x40,0x41,0x3F,0x01}, /* 0x4A J */
  {0x7F,0x08,0x14,0x22,0x41}, /* 0x4B K */
  {0x7F,0x40,0x40,0x40,0x40}, /* 0x4C L */
  {0x7F,0x02,0x0C,0x02,0x7F}, /* 0x4D M */
  {0x7F,0x04,0x08,0x10,0x7F}, /* 0x4E N */
  {0x3E,0x41,0x41,0x41,0x3E}, /* 0x4F O */
  {0x7F,0x09,0x09,0x09,0x06}, /* 0x50 P */
  {0x3E,0x41,0x51,0x21,0x5E}, /* 0x51 Q */
  {0x7F,0x09,0x19,0x29,0x46}, /* 0x52 R */
  {0x46,0x49,0x49,0x49,0x31}, /* 0x53 S */
  {0x01,0x01,0x7F,0x01,0x01}, /* 0x54 T */
  {0x3F,0x40,0x40,0x40,0x3F}, /* 0x55 U */
  {0x1F,0x20,0x40,0x20,0x1F}, /* 0x56 V */
  {0x3F,0x40,0x38,0x40,0x3F}, /* 0x57 W */
  {0x63,0x14,0x08,0x14,0x63}, /* 0x58 X */
  {0x07,0x08,0x70,0x08,0x07}, /* 0x59 Y */
  {0x61,0x51,0x49,0x45,0x43}, /* 0x5A Z */
  {0x00,0x7F,0x41,0x41,0x00}, /* 0x5B [ */
  {0x02,0x04,0x08,0x10,0x20}, /* 0x5C \ */
  {0x00,0x41,0x41,0x7F,0x00}, /* 0x5D ] */
  {0x04,0x02,0x01,0x02,0x04}, /* 0x5E ^ */
  {0x40,0x40,0x40,0x40,0x40}, /* 0x5F _ */
  {0x00,0x01,0x02,0x04,0x00}, /* 0x60 ` */
  {0x20,0x54,0x54,0x54,0x78}, /* 0x61 a */
  {0x7F,0x48,0x44,0x44,0x38}, /* 0x62 b */
  {0x38,0x44,0x44,0x44,0x20}, /* 0x63 c */
  {0x38,0x44,0x44,0x48,0x7F}, /* 0x64 d */
  {0x38,0x54,0x54,0x54,0x18}, /* 0x65 e */
  {0x08,0x7E,0x09,0x01,0x02}, /* 0x66 f */
  {0x0C,0x52,0x52,0x52,0x3E}, /* 0x67 g */
  {0x7F,0x08,0x04,0x04,0x78}, /* 0x68 h */
  {0x00,0x44,0x7D,0x40,0x00}, /* 0x69 i */
  {0x20,0x40,0x44,0x3D,0x00}, /* 0x6A j */
  {0x7F,0x10,0x28,0x44,0x00}, /* 0x6B k */
  {0x00,0x41,0x7F,0x40,0x00}, /* 0x6C l */
  {0x7C,0x04,0x18,0x04,0x78}, /* 0x6D m */
  {0x7C,0x08,0x04,0x04,0x78}, /* 0x6E n */
  {0x38,0x44,0x44,0x44,0x38}, /* 0x6F o */
  {0x7C,0x14,0x14,0x14,0x08}, /* 0x70 p */
  {0x08,0x14,0x14,0x18,0x7C}, /* 0x71 q */
  {0x7C,0x08,0x04,0x04,0x08}, /* 0x72 r */
  {0x48,0x54,0x54,0x54,0x20}, /* 0x73 s */
  {0x04,0x3F,0x44,0x40,0x20}, /* 0x74 t */
  {0x3C,0x40,0x40,0x40,0x3C}, /* 0x75 u */
  {0x1C,0x20,0x40,0x20,0x1C}, /* 0x76 v */
  {0x3C,0x40,0x30,0x40,0x3C}, /* 0x77 w */
  {0x44,0x28,0x10,0x28,0x44}, /* 0x78 x */
  {0x0C,0x50,0x50,0x50,0x3E}, /* 0x79 y */
  {0x44,0x64,0x54,0x4C,0x44}, /* 0x7A z */
  {0x00,0x08,0x36,0x41,0x00}, /* 0x7B { */
  {0x00,0x00,0x7F,0x00,0x00}, /* 0x7C | */
  {0x00,0x41,0x36,0x08,0x00}, /* 0x7D } */
  {0x10,0x08,0x08,0x10,0x08}, /* 0x7E ~ */
};

/* Draw one character; char_w = (5+1)*scale, char_h = 7*scale */
static void draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) c = '?';
    const uint8_t *g = font5x7[(uint8_t)c - 0x20];

    int cw = 5 * scale;
    int ch = 7 * scale;

    /* Build pixel buffer for the 5-wide × 7-tall (scaled) glyph */
    uint8_t buf[5 * 3 * 7 * 3 * 2]; /* enough for scale 3 */
    int idx = 0;
    for (int row = 0; row < 7; row++) {
        for (int sr = 0; sr < scale; sr++) {
            for (int col = 0; col < 5; col++) {
                uint16_t px = (g[col] & (1u << row)) ? fg : bg;
                uint8_t  hi = (uint8_t)(px >> 8), lo = (uint8_t)(px & 0xFF);
                for (int sc = 0; sc < scale; sc++) {
                    buf[idx++] = hi;
                    buf[idx++] = lo;
                }
            }
        }
    }

    set_addr_window(x, y, x + cw - 1, y + ch - 1);
    cs_lo();
    gpio_put(TFT_DC, 1);
    spi_write_blocking(TFT_SPI, buf, (size_t)idx);
    cs_hi();

    /* 1-pixel gap column (scaled) between characters, filled with background */
    tft_fill_rect(x + cw, y, scale, ch, bg);
}

void tft_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
    int stride = (5 + 1) * scale;
    while (*s) {
        draw_char(x, y, *s++, fg, bg, scale);
        x += stride;
    }
}

void tft_draw_string_centered(int y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
    int len   = (int)strlen(s);
    int total = len * (5 + 1) * scale;
    int x     = (TFT_W - total) / 2;
    if (x < 0) x = 0;
    tft_draw_string(x, y, s, fg, bg, scale);
}

/* ------------------------------------------------------------------ */
/* JPEG background loader                                              */

static uint8_t jpeg_work[TJPGD_WORKSPACE_SIZE];

/* TJpgDec input callback: read or skip bytes from an open FIL */
static size_t jpeg_in(JDEC *jd, uint8_t *buf, size_t len)
{
    FIL *fp = (FIL *)jd->device;
    UINT br;
    if (buf)
        return f_read(fp, buf, (UINT)len, &br) == FR_OK ? (size_t)br : 0;
    else
        return f_lseek(fp, f_tell(fp) + len) == FR_OK ? len : 0;
}

/* TJpgDec output callback: JD_FORMAT=1 gives RGB565 (little-endian uint16_t).
 * ST7735 SPI expects big-endian, so swap bytes when copying to the send buffer. */
static int jpeg_out(JDEC *jd, void *bitmap, JRECT *rect)
{
    (void)jd;
    uint16_t *pix = (uint16_t *)bitmap;
    int x0  = (int)rect->left,  y0  = (int)rect->top;
    int x1  = (int)rect->right, y1  = (int)rect->bottom;
    int x1c = (x1 < TFT_W - 1) ? x1 : TFT_W - 1;
    int y1c = (y1 < TFT_H - 1) ? y1 : TFT_H - 1;
    if (x0 >= TFT_W || y0 >= TFT_H) return 1;

    int tw = x1 - x0 + 1;           /* original (unclipped) tile width */
    int w  = x1c - x0 + 1;
    int h  = y1c - y0 + 1;

    set_addr_window(x0, y0, x1c, y1c);
    cs_lo();
    gpio_put(TFT_DC, 1);

    uint8_t buf[16 * 16 * 2];        /* fits the largest possible MCU tile */
    int idx = 0;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint16_t px = pix[row * tw + col];
            buf[idx++] = (uint8_t)(px >> 8);    /* high byte first (big-endian) */
            buf[idx++] = (uint8_t)(px & 0xFF);
        }
    }
    spi_write_blocking(TFT_SPI, buf, (size_t)idx);
    cs_hi();
    return 1; /* continue decoding */
}

static bool load_jpeg_bg(const char *path)
{
    FIL fp;
    if (f_open(&fp, path, FA_READ) != FR_OK) return false;
    JDEC jd;
    bool ok = false;
    if (jd_prepare(&jd, jpeg_in, jpeg_work, sizeof(jpeg_work), &fp) == JDR_OK) {
        /* choose smallest scale factor that fits the display */
        uint8_t scale = 0;
        while (scale < 3 &&
               ((jd.width  >> scale) > (uint16_t)TFT_W ||
                (jd.height >> scale) > (uint16_t)TFT_H))
            scale++;
        ok = (jd_decomp(&jd, jpeg_out, scale) == JDR_OK);
    }
    f_close(&fp);
    return ok;
}

/* ------------------------------------------------------------------ */

void tft_show_track(const char *path, const char *title, const char *author,
                    const char *released, uint16_t songs, uint16_t start_song,
                    bool ntsc, uint32_t play_hz)
{
    /* Build "0:/background.jpg" (or equivalent drive) from the SID file path */
    char bg_path[32];
    const char *sl = strchr(path, '/');
    int dl = sl ? (int)(sl - path) : (int)strlen(path);
    snprintf(bg_path, sizeof(bg_path), "%.*s/background.jpg", dl, path);

    /* Draw background; fall back to solid blue if file is missing */
    if (!load_jpeg_bg(bg_path))
        tft_fill(COLOR_BLUE);

    /* --- Title strip at top: scale 1, centered, no prefix, max 26 chars --- */
    tft_fill_rect(0, 0, TFT_W, 11, 0x0000);
    char ttl[27];
    snprintf(ttl, sizeof(ttl), "%s", title);
    tft_draw_string_centered(2, ttl, COLOR_WHITE, 0x0000, 1);

    /* --- Metadata strip at bottom: 4 rows at scale 1 --- */
    tft_fill_rect(0, TFT_H - 38, TFT_W, 38, 0x0000);
    char line[27];
    int  y      = TFT_H - 36;
    const int row_h = 9;

    snprintf(line, sizeof(line), "Author: %.18s", author);
    tft_draw_string(2, y, line, COLOR_WHITE, 0x0000, 1); y += row_h;

    snprintf(line, sizeof(line), "Released: %.16s", released);
    tft_draw_string(2, y, line, COLOR_WHITE, 0x0000, 1); y += row_h;

    snprintf(line, sizeof(line), "Songs: %u  Def: %u", songs, start_song);
    tft_draw_string(2, y, line, COLOR_WHITE, 0x0000, 1); y += row_h;

    snprintf(line, sizeof(line), "%s  %u Hz", ntsc ? "NTSC" : "PAL", (unsigned)play_hz);
    tft_draw_string(2, y, line, COLOR_WHITE, 0x0000, 1);
}
