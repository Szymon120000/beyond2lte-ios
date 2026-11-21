/* SPDX-License-Identifier: BSD-2-Clause */
/* Exynos 9820 always-on RTC (samsung,exynos8-rtc at 0x15950000).
 *
 * Register layout and BCD encoding follow the Samsung rtc-s3c driver:
 *   0x40 RTCCON (bit 0 = RTCEN), 0x70 SEC, 0x74 MIN, 0x78 HOUR,
 *   0x7c DATE, 0x84 MON, 0x88 YEAR (16-bit, two-digit BCD, 2000..2099).
 * The kernel reads the calendar once at boot; XNU then advances it from the
 * monotonic clock. Nothing is written to the RTC.
 */
#ifndef B2L_RTC_H
#define B2L_RTC_H
#include <stdint.h>

#define B2L_RTC_BASE 0x15950000u
#define B2L_RTC_BYTES 0x100u
#define B2L_RTC_REG_SEC 0x70u
#define B2L_RTC_REG_MIN 0x74u
#define B2L_RTC_REG_HOUR 0x78u
#define B2L_RTC_REG_DATE 0x7Cu
#define B2L_RTC_REG_MON 0x84u
#define B2L_RTC_REG_YEAR 0x88u
#define B2L_RTC_REG_CON 0x40u
#define B2L_RTC_CON_EN 0x01u

/* 2020-01-01T00:00:00Z: anything older means an unset/reset RTC. */
#define B2L_RTC_MIN_EPOCH 1577836800ULL
/* 2100-01-01T00:00:00Z: two-digit years cannot express more. */
#define B2L_RTC_MAX_EPOCH 4102444800ULL

static inline int
b2l_rtc_bcd(const uint8_t v, unsigned *out)
{
	if ((v & 0x0f) > 9 || (v >> 4) > 9) return 0;
	*out = (unsigned)(v >> 4) * 10u + (unsigned)(v & 0x0f);
	return 1;
}

static inline int64_t
b2l_rtc_days_from_civil(int y, unsigned m, unsigned d)
{
	int64_t era;
	unsigned yoe, doy, doe;
	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = (unsigned)(y - era * 400);
	doy = (153u * (m + (m > 2 ? (unsigned)-3 : 9u)) + 2u) / 5u + d - 1u;
	doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

/* raw = {sec, min, hour, date, mon, year_lo, year_hi}. Returns Unix seconds
 * (UTC) or 0 when the register contents are not a plausible date. */
static inline uint64_t
b2l_rtc_decode(const uint8_t raw[7])
{
	unsigned sec, min, hour, date, mon, year;
	if (!b2l_rtc_bcd(raw[0], &sec) || !b2l_rtc_bcd(raw[1], &min) ||
	    !b2l_rtc_bcd(raw[2], &hour) || !b2l_rtc_bcd(raw[3], &date) ||
	    !b2l_rtc_bcd(raw[4], &mon) || !b2l_rtc_bcd(raw[5], &year) || raw[6] != 0)
		return 0;
	if (sec > 59 || min > 59 || hour > 23 || date < 1 || date > 31 ||
	    mon < 1 || mon > 12 || year > 99)
		return 0;
	uint64_t epoch = (uint64_t)(b2l_rtc_days_from_civil((int)(2000 + year), mon, date) *
	    86400 + (int64_t)hour * 3600 + (int64_t)min * 60 + (int64_t)sec);
	if (epoch < B2L_RTC_MIN_EPOCH || epoch > B2L_RTC_MAX_EPOCH) return 0;
	return epoch;
}
#endif /* B2L_RTC_H */
