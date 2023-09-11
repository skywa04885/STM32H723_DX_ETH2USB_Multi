/*
 * logging.c
 *
 *  Created on: Sep 8, 2023
 *      Author: luke
 */

#include "logging.h"

#include <stm32h7xx_hal.h>
#include <stm32h723xx.h>
#include <stdio.h>
#include <stdarg.h>

extern RTC_HandleTypeDef hrtc;

void _mlog(const char *function, const int line, const char *fmt, ...) {
	RTC_TimeTypeDef current_time;
	RTC_DateTypeDef current_date;

	HAL_RTC_GetTime(&hrtc, &current_time, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &current_date, RTC_FORMAT_BIN);

	printf("%04u-%02u-%02u %02u:%02u:%02u %s %d : ", current_date.Year,
			current_date.Month, current_date.Date, current_time.Hours,
			current_time.Minutes, current_time.Seconds, function, line);

	va_list args;

	va_start(args, fmt);

	vprintf(fmt, args);

	va_end(args);

	printf("\r\n");
}
