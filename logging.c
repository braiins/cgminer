/*
 * Copyright 2011-2012 Con Kolivas
 * Copyright 2013 Andrew Smith
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <unistd.h>

#include "logging.h"
#include "miner.h"

bool opt_debug = false;
bool opt_log_output = true;

/* per default priorities higher than LOG_NOTICE are logged */
int opt_log_level = LOG_WARNING;

/**
 * Names of sources. Must match enum in logging.h
 */
static char *source_name[] = {
	"undef",
	"mining",
	"pool",
	"system",
	"hw",
	"fatal",
	"?",
};

/* Names of log levels */
static const char *log_level_name(int level)
{
	switch (level) {
		case LOG_ERR:
			return "error";
		case LOG_WARNING:
			return "warning";
		case LOG_NOTICE:
			return "notice";
		case LOG_INFO:
			return "info";
		case LOG_DEBUG:
			return "debug";
		default:
			return "unknown";
	}
}

static void my_log_curses(int prio, const char *datetime, const char *str, bool force)
{
	if (opt_quiet && prio != LOG_ERR)
		return;

	/* Mutex could be locked by dead thread on shutdown so forcelog will
	 * invalidate any console lock status. */
	if (force) {
		mutex_trylock(&console_lock);
		mutex_unlock(&console_lock);
	}
#ifdef HAVE_CURSES
	extern bool use_curses;
	if (use_curses && log_curses_only(prio, datetime, str))
		;
	else
#endif
	{
		mutex_lock(&console_lock);
		printf("%s%s%s", datetime, str, "                    \n");
		mutex_unlock(&console_lock);
	}
}

/* high-level logging function, based on global opt_log_level */

/*
 * log to pool
 */

void log_to_pool(int prio, int source, const char *str)
{
	struct telemetry *tele;

	tele = make_telemetry_log(log_level_name(prio), source_name[source], str);
	submit_telemetry(tele);
}

/**
 * recur_enter - increment recursion counter
 * @param count pointer to variable counting things in
 * @returns 1 if entrace was successful, 0 if entrace was blocked
 */

static int recur_enter(int *count)
{
	if (*count != 0)
		return 0;
	*count = 1;
	return 1;
}

/**
 * recur_leave - decrement recursion counter
 *
 * @param count pointer to variable counting things in
 */

static void recur_leave(int *count)
{
	*count = 0;
}

/**
 * We need to protect log_to_pool against double entry: if something bad
 * happened while trying to print to pool (we have been disconnected from pool
 * or similar), it's likely another applog will be called causing (probably)
 * infinite recursion.
 *
 * We use applog_entry_count to protect against doubnle entry to log_to_pool.
 * We are not concerned about other threads, this lock is per-thread (and
 * stored in TLS), so we don't have to bother with mutexes or atomic writes.
 */
static __thread int applog_entry_count;

/**
 * Decide whether to log remote
 *
 * @param prio priority of message
 * @param source source of message
 * @returns 1 if this message should be logged remote, 0 otherwise
 */
static int decide_to_log_remote(int prio, int source)
{
	if (source == SOURCE_POOL)
		return 0;
	if (source == SOURCE_FATAL || source == SOURCE_HW)
		return 1;
	if (prio == LOG_ERR || prio == LOG_WARNING)
		return 1;
	return 0;
}

/*
 * log function
 */
void _applog(int prio, int source, const char *str, bool force)
{
#ifdef HAVE_SYSLOG_H
	if (use_syslog) {
		syslog(LOG_LOCAL0 | prio, "%s", str);
	}
#else
	if (0) {}
#endif
	else {
#if 0
		char datetime[64];
		struct timeval tv = {0, 0};
		struct tm *tm;

		cgtime(&tv);

		const time_t tmp_time = tv.tv_sec;
		int ms = (int)(tv.tv_usec / 1000);
		tm = localtime(&tmp_time);

		snprintf(datetime, sizeof(datetime), " [%d-%02d-%02d %02d:%02d:%02d.%03d] ",
			tm->tm_year + 1900,
			tm->tm_mon + 1,
			tm->tm_mday,
			tm->tm_hour,
			tm->tm_min,
			tm->tm_sec, ms);

		/* Only output to stderr if it's not going to the screen as well */
		if (!isatty(fileno((FILE *)stderr))) {
			fprintf(stderr, "%s%s\n", datetime, str);	/* atomic write to stderr */
			fflush(stderr);
		}

		my_log_curses(prio, datetime, str, force);
#else
		fprintf(stderr, "[%s,%s] %s\n", log_level_name(prio), source_name[source], str);
		fflush(stderr);

		if (decide_to_log_remote(prio, source)) {
			if (recur_enter(&applog_entry_count)) {
				log_to_pool(prio, source, str);
				recur_leave(&applog_entry_count);
			} else {
				printf("Recursion!\n");
			}
		}
#endif
	}
}

void _simplelog(int prio, const char *str, bool force)
{
#ifdef HAVE_SYSLOG_H
	if (use_syslog) {
		syslog(LOG_LOCAL0 | prio, "%s", str);
	}
#else
	if (0) {}
#endif
	else {
		/* Only output to stderr if it's not going to the screen as well */
		if (!isatty(fileno((FILE *)stderr))) {
			fprintf(stderr, "%s\n", str);	/* atomic write to stderr */
			fflush(stderr);
		}

		my_log_curses(prio, "", str, force);
	}
}
