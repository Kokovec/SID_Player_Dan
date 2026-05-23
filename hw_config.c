/* hw_config.c — SD card hardware configuration for carlk3/no-OS-FatFS-SD-SPI-RPi-Pico
 *
 * SD card wiring (SPI0, free GPIOs that don't conflict with SID bus):
 *   GP17 — SPI0 CSn  (SD card /CS)
 *   GP18 — SPI0 SCK  (SD card CLK)
 *   GP19 — SPI0 TX   (SD card MOSI / DI)
 *   GP20 — SPI0 RX   (SD card MISO / DO)
 */

#include "hw_config.h"

static spi_t spis[] = {
    {
        .hw_inst     = spi0,
        .miso_gpio   = 20,        /* GP20 — SPI0 RX  */
        .mosi_gpio   = 19,        /* GP19 — SPI0 TX  */
        .sck_gpio    = 18,        /* GP18 — SPI0 SCK */
        .baud_rate   = 12500000,  /* 12.5 MHz        */
        .DMA_IRQ_num = DMA_IRQ_0,
    }
};

static sd_card_t sd_cards[] = {
    {
        .pcName  = "0:",
        .spi     = &spis[0],
        .ss_gpio = 17,            /* GP17 — SPI0 CSn */
    }
};

/* Required callbacks */
size_t   spi_get_num()             { return count_of(spis);    }
spi_t   *spi_get_by_num(size_t n)  { return n < count_of(spis)      ? &spis[n]     : NULL; }
size_t   sd_get_num()              { return count_of(sd_cards); }
sd_card_t *sd_get_by_num(size_t n) { return n < count_of(sd_cards)  ? &sd_cards[n] : NULL; }
