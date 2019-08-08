#include "hlog.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "htime.h"  // for get_datetime

static int      s_initialized = 0;
static hlog_handler s_logger = DEFAULT_LOGGER;
static char     s_logfile[256] = DEFAULT_LOG_FILE;
static int      s_loglevel = DEFAULT_LOG_LEVEL;
static bool     s_logcolor = false;
static bool     s_fflush   = true;
static int      s_remain_days = DEFAULT_LOG_REMAIN_DAYS;
static char     s_logbuf[LOG_BUFSIZE];

// for thread-safe
#include "hmutex.h"
static hmutex_t  s_mutex;

void hlog_set_logger(hlog_handler fn) {
    s_logger = fn;
}

void hlog_set_level(int level) {
    s_loglevel = level;
}

void hlog_set_remain_days(int days) {
    s_remain_days = days;
}

int hlog_printf(int level, const char* fmt, ...) {
    if (level < s_loglevel)
        return -10;

    datetime_t now = datetime_now();
    const char* pcolor = "";
    const char* plevel = "";
#define CASE_LOG(id, str, clr) \
    case id: plevel = str; pcolor = clr; break;

    switch (level) {
        FOREACH_LOG(CASE_LOG)
    }
#undef CASE_LOG

    if (!s_logcolor) {
        pcolor = "";
    }

    if (!s_initialized) {
        s_initialized = 1;
        hmutex_init(&s_mutex);
    }

    // lock s_logbuf, s_logger
    hmutex_lock(&s_mutex);
    int len = snprintf(s_logbuf, LOG_BUFSIZE, "%s[%04d-%02d-%02d %02d:%02d:%02d.%03d][%s]: ",
        pcolor,
        now.year, now.month, now.day, now.hour, now.min, now.sec, now.ms, plevel);

    va_list ap;
    va_start(ap, fmt);
    len += vsnprintf(s_logbuf + len, LOG_BUFSIZE - len, fmt, ap);
    va_end(ap);

    if (s_logcolor) {
        len += snprintf(s_logbuf + len, LOG_BUFSIZE - len, "%s", CL_CLR);
    }

    s_logger(s_logbuf, len);

    hmutex_unlock(&s_mutex);
    return len;
}

void hlog_enable_color(int on) {
    s_logcolor = on;
}

void stdout_logger(const char* buf, int len) {
    fprintf(stdout, "%s\n", buf);
}

void stderr_logger(const char* buf, int len) {
    fprintf(stderr, "%s\n", buf);
}

static void ts_logfile(time_t ts, char* buf, int len) {
    struct tm* tm = localtime(&ts);
    snprintf(buf, len, "%s-%04d-%02d-%02d.log",
            s_logfile,
            tm->tm_year+1900,
            tm->tm_mon+1,
            tm->tm_mday);
}

static FILE* shift_logfile() {
    static FILE*    s_logfp = NULL;
    static char     s_cur_logfile[256] = {0};
    static time_t   s_last_logfile_ts = 0;

    time_t ts_now = time(NULL);
    int interval_days = s_last_logfile_ts == 0 ? 0 : (ts_now / SECONDS_PER_DAY - s_last_logfile_ts / SECONDS_PER_DAY);;
    if (s_logfp == NULL || interval_days > 0) {
        // close old logfile
        if (s_logfp) {
            fclose(s_logfp);
            s_logfp = NULL;
        }
        else {
            interval_days = 30;
        }
        if (interval_days >= s_remain_days) {
            // remove [today-interval_days, today-s_remain_days] logfile
            char rm_logfile[256] = {0};
            for (int i = interval_days; i >= s_remain_days; --i) {
                time_t ts_rm  = ts_now - i * SECONDS_PER_DAY;
                ts_logfile(ts_rm, rm_logfile, sizeof(rm_logfile));
                remove(rm_logfile);
            }
        }
        else {
            // remove today-s_remain_days logfile
            char rm_logfile[256] = {0};
            time_t ts_rm  = ts_now - s_remain_days * SECONDS_PER_DAY;
            ts_logfile(ts_rm, rm_logfile, sizeof(rm_logfile));
            remove(rm_logfile);
        }
    }

    // open today logfile
    if (s_logfp == NULL) {
        ts_logfile(ts_now, s_cur_logfile, sizeof(s_cur_logfile));
        s_logfp = fopen(s_cur_logfile, "a"); // note: append-mode for multi-processes
        s_last_logfile_ts = ts_now;
    }

    // rewrite if too big
    if (s_logfp && ftell(s_logfp) > MAX_LOG_FILESIZE) {
        fclose(s_logfp);
        s_logfp = NULL;
        s_logfp = fopen(s_cur_logfile, "w");
    }

    return s_logfp;
}

void file_logger(const char* buf, int len) {
    FILE* fp = shift_logfile();
    if (fp == NULL) return;
    fprintf(fp, "%s\n", buf);
    if (s_fflush) {
        fflush(fp);
    }
}

int hlog_set_file(const char* logfile) {
    if (logfile == NULL || strlen(logfile) == 0)    return -10;

    strncpy(s_logfile, logfile, sizeof(s_logfile));
    // remove suffix .log
    char* suffix = strrchr(s_logfile, '.');
    if (suffix && strcmp(suffix, ".log") == 0) {
        *suffix = '\0';
    }

    return 0;
}

void hlog_set_fflush(int on) {
    s_fflush = on;
}

void hlog_fflush() {
    hmutex_lock(&s_mutex);
    FILE* fp = shift_logfile();
    if (fp) {
        fflush(fp);
    }
    hmutex_unlock(&s_mutex);
}
