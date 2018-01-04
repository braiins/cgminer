#ifndef __LOGGING_H__
#define __LOGGING_H__

#include "config.h"
#include <stdbool.h>
#include <stdarg.h>

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#else
enum {
	LOG_ERR,
	LOG_WARNING,
	LOG_NOTICE,
	LOG_INFO,
	LOG_DEBUG,
};
#endif

enum {
	SOURCE_UNDEFINED,
	SOURCE_MINING,
	SOURCE_POOL,
	SOURCE_SYSTEM,
	SOURCE_HW,
	SOURCE_FATAL,
	SOURCE_UNKNOWN,
};


/* debug flags */
extern bool opt_debug;
extern bool opt_decode;
extern bool opt_log_output;
extern bool opt_realquiet;
extern bool want_per_device_stats;

/* global log_level, messages with lower or equal prio are logged */
extern int opt_log_level;

#define LOGBUFSIZ 256

extern void _applog(int prio, int source, const char *str, bool force);
extern void _simplelog(int prio, const char *str, bool force);

#define IN_FMT_FFL " in %s %s():%d"

#define applog_source(prio, source, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
		if (use_syslog || opt_log_output || prio <= opt_log_level) { \
			char tmp42[LOGBUFSIZ]; \
			snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
			_applog(prio, source, tmp42, false); \
		} \
	} \
} while (0)


#define applog(prio, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
		if (use_syslog || opt_log_output || prio <= opt_log_level) { \
			char tmp42[LOGBUFSIZ]; \
			snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
			_applog(prio, SOURCE_UNDEFINED, tmp42, false); \
		} \
	} \
} while (0)

#define simplelog(prio, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
		if (use_syslog || opt_log_output || prio <= opt_log_level) { \
			char tmp42[LOGBUFSIZ]; \
			snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
			_simplelog(prio, tmp42, false); \
		} \
	} \
} while (0)

#define applogsiz(prio, _SIZ, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
		if (use_syslog || opt_log_output || prio <= opt_log_level) { \
			char tmp42[_SIZ]; \
			snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
			_applog(prio, SOURCE_UNDEFINED, tmp42, false); \
		} \
	} \
} while (0)

#define forcelog(prio, fmt, ...) do { \
	if (opt_debug || prio != LOG_DEBUG) { \
		if (use_syslog || opt_log_output || prio <= opt_log_level) { \
			char tmp42[LOGBUFSIZ]; \
			snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
			_applog(prio, SOURCE_UNDEFINED, tmp42, true); \
		} \
	} \
} while (0)

#define quit(status, fmt, ...) do { \
	if (fmt) { \
		char tmp42[LOGBUFSIZ]; \
		snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
		_applog(LOG_ERR, SOURCE_FATAL, tmp42, true); \
	} \
	_quit(status); \
} while (0)

#define early_quit(status, fmt, ...) do { \
	if (fmt) { \
		char tmp42[LOGBUFSIZ]; \
		snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
		_applog(LOG_ERR, SOURCE_FATAL, tmp42, true); \
	} \
	__quit(status, false); \
} while (0)

#define quithere(status, fmt, ...) do { \
	if (fmt) { \
		char tmp42[LOGBUFSIZ]; \
		snprintf(tmp42, sizeof(tmp42), fmt IN_FMT_FFL, \
				##__VA_ARGS__, __FILE__, __func__, __LINE__); \
		_applog(LOG_ERR, SOURCE_FATAL, tmp42, true); \
	} \
	_quit(status); \
} while (0)

#define quitfrom(status, _file, _func, _line, fmt, ...) do { \
	if (fmt) { \
		char tmp42[LOGBUFSIZ]; \
		snprintf(tmp42, sizeof(tmp42), fmt IN_FMT_FFL, \
				##__VA_ARGS__, _file, _func, _line); \
		_applog(LOG_ERR, SOURCE_FATAL, tmp42, true); \
	} \
	_quit(status); \
} while (0)

#ifdef HAVE_CURSES

#define wlog(fmt, ...) do { \
	char tmp42[LOGBUFSIZ]; \
	snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
	_wlog(tmp42); \
} while (0)

#define wlogprint(fmt, ...) do { \
	char tmp42[LOGBUFSIZ]; \
	snprintf(tmp42, sizeof(tmp42), fmt, ##__VA_ARGS__); \
	_wlogprint(tmp42); \
} while (0)

#endif

#define applog_pool(prio, fmt, ...) applog_source(prio, SOURCE_POOL, fmt, ##__VA_ARGS__)
#define applog_mining(prio, fmt, ...) applog_source(prio, SOURCE_MINING, fmt, ##__VA_ARGS__)
#define applog_hw(prio, fmt, ...) applog_source(prio, SOURCE_HW, fmt, ##__VA_ARGS__)
#define applog_hw_chip(prio, chain_id, chip_id, fmt, ...) \
	applog_source(prio, SOURCE_HW, "CHIP[%d,%d]: " fmt, chain_id, chip_id, ##__VA_ARGS__)
#define applog_hw_chain(prio, chain_id, fmt, ...) \
	applog_source(prio, SOURCE_HW, "CHAIN[%d]: " fmt, chain_id, ##__VA_ARGS__)
#define applog_system(prio, fmt, ...) applog_source(prio, SOURCE_SYSTEM, fmt, ##__VA_ARGS__)

#endif /* __LOGGING_H__ */
