#ifndef RADIO_HAL_DEBUG_H
#define RADIO_HAL_DEBUG_H

#include <stdio.h>
#include <string.h>
#include <time.h>

/* to enable(1) or disable(0) debug traces. */
/* sub radio systems can be selected with debug_mask */
#define DEBUG 1

/* if new enum items are added, please add also to hal_debug_to_string() */
enum HAL_DEBUG {
	HAL_DBG_WIFI = 0x00000001,
	HAL_DBG_MODEM = 0x00000002,
	HAL_DBG_BT = 0x00000004,
	HAL_DBG_COMMON = 0x00000008,
	HAL_DBG_ANY = 0xffffffff
};

__attribute__((unused))  static const char *hal_debug_to_string(enum HAL_DEBUG hal_debug) {
	switch (hal_debug) {
		case HAL_DBG_WIFI:
			return "WIFI";
		case HAL_DBG_MODEM:
			return "MODEM";
		case HAL_DBG_BT:
			return "BT";
		case HAL_DBG_COMMON:
			return "COMMON";
		default:
			return "ANY";
	}
}

__attribute__((unused))  static unsigned int debug_radio_mask = HAL_DBG_ANY;
#define ERROR_PREFIX (const char*)"ERROR"
#define WARN_PREFIX (const char*)"WARN"
#define DEBUG_PREFIX (const char*)"DBG"
#define INFO_PREFIX (const char*)"INFO"

inline static char *_timestamp() {
	time_t mytime = time(NULL);
	char *time_str = ctime(&mytime);
	time_str[strcspn(time_str, "\n")] = 0;
	return time_str;
}

// for modem/at - codes from RIL reference
#define __unused __attribute__((unused))

#define hal_print(sub, prefix, fmt, args...) \
	do {                                     \
		if (debug_radio_mask & sub) \
			fprintf(stdout, "%-25s:%-5s:%-6s: " fmt, _timestamp(), prefix, hal_debug_to_string(sub), ##args); \
	} while (0)

#define hal_print_critical(sub, prefix, fmt, args...) \
	do {        \
		fprintf(stderr, "%-25s:%-5s:%-6s: %s:%d:%s(): " fmt, _timestamp(), prefix, hal_debug_to_string(sub), \
			__FILE__, __LINE__, __func__, ##args); \
	} while (0)

#define RLOGE(fmt, args...) \
	do {        \
			fprintf(stdout, "%-25s:ERROR:MODEM : AT error:" fmt, _timestamp(), ##args); \
	} while (0)

#if (defined DEBUG) && DEBUG == 1
#define hal_print_debug(sub, prefix, fmt, args...) \
	do {                                     \
		if (debug_radio_mask & sub) \
			fprintf(stdout, "%-25s:%-5s:%-6s: " fmt, _timestamp(),prefix, hal_debug_to_string(sub), ##args); \
	} while (0)

#define RLOGD(fmt, args...) \
	do {        \
			fprintf(stdout, "%-25s:INFO :MODEM : AT debug:" fmt,  _timestamp(), ##args); \
	} while (0)
#else // #if (defined DEBUG) && DEBUG == 1

#define hal_print_debug(sub, prefix, fmt, args...) \
   do {} while (0)

#define RLOGD(fmt, args...) \
	do {} while (0)

#endif // #if (defined DEBUG) && DEBUG == 1

#define hal_err(sub, fmt, ...) \
	hal_print_critical(sub, ERROR_PREFIX, fmt, ##__VA_ARGS__)
#define hal_warn(sub, fmt, ...) \
	hal_print(sub, WARN_PREFIX, fmt, ##__VA_ARGS__)
#define hal_debug(sub, fmt, ...) \
	hal_print_debug(sub, DEBUG_PREFIX, fmt, ##__VA_ARGS__)
#define hal_info(sub, fmt, ...) \
	hal_print(sub, INFO_PREFIX, fmt, ##__VA_ARGS__)

#endif  //RADIO_HAL_DEBUG_H