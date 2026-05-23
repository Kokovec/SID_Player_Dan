/* hardware/rtc.h — RP2350 stub
 * The RP2350 has no hardware RTC peripheral. This stub satisfies
 * carlk3/no-OS-FatFS-SD-SPI-RPi-Pico's rtc.c so the library compiles
 * without modification. get_fattime() will return 0 (no timestamp), which
 * is harmless for a SID player.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t year;
    int8_t  month;
    int8_t  day;
    int8_t  dotw;
    int8_t  hour;
    int8_t  min;
    int8_t  sec;
} datetime_t;

static inline void rtc_init(void) {}
static inline bool rtc_get_datetime(datetime_t *t) { (void)t; return false; }
static inline void rtc_set_datetime(const datetime_t *t) { (void)t; }
