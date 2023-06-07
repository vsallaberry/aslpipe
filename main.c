/*
 * Copyright (C) 2021 Vincent Sallaberry
 * vsyswatch <https://github.com/vsallaberry/aslpipe>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 ****************************************************************************
 * aslpipe: read message from stdin and log send them to asl log system.
 ****************************************************************************/
//#define OSLOG
//#define SYSLOG
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#ifdef OSLOG
#include <os/log.h>
#elif defined(SYSLOG)
#include <syslog.h>
#else
#include <asl.h>
#endif

#ifdef OSLOG
int vlog(os_log_t log, const char * facility, const char * category, const char * message_key, int level, void * msg, const char *message) {
    os_log_with_type(log, level, "%s", message);
    return 0;
}
#elif defined SYSLOG
int vlog(void * log, const char * facility, const char * category, const char * message_key, int level, void * msg, const char *message) {
    syslog(level, "%s", message);
    return 0;
}
#else
int vlog(asl_object_t log, const char * facility, const char * category, const char * message_key, int level, asl_object_t msg, const char *message) {
    (void) facility;
    (void) category;
    (void) level;
    asl_set(msg, message_key, message);
    asl_send(log, msg);
    return 0;
}
#endif

static int usage(int ret) {
    FILE * out = ret == 0 ? stdout : stderr;
    fprintf(out, "usage: aslpipe [-h] [-F facility] [-l level] [-C category] [-S sender] [-K message_key] [-m message]\n"
                 "   stdin is used if message not given (-m)\n");

    return ret;
}
int main(int argc, const char*const* argv) {
#ifdef OSLOG
    os_log_t        log;
    void *          msg         = NULL;
#elif defined(SYSLOG)
    void *          log = NULL, msg = NULL;;
#else
    asl_object_t    log;
    asl_object_t    msg;
#endif
    const char *    facility    = "local2";
    const char *    category    = "local2";
    const char *    message_key = "Message";
    const char *    sender      = NULL;
    const char *    message     = NULL;
#ifdef OSLOG
    int             level       = OS_LOG_TYPE_INFO;
#elif defined(SYSLOG)
    int             level       = LOG_NOTICE;
#else
    int             level       = ASL_LEVEL_NOTICE;
#endif
    int             ret         = 0;

    for (int i=1; i < argc; ++i) {
        if (*argv[i] == '-') {
            switch (argv[i][1]) {
                case 'h':
                    return usage(0);
                    break ;
                case 'F':
                    if (++i == argc) return usage(1);
                    facility = argv[i];
                    break ;
                case 'C':
                    if (++i == argc) return usage(1);
                    category = argv[i];
                    break ;
                case 'l':
                    if (++i == argc) return usage(1);
                    level = atoi(argv[i]);
                    break ;
                case 'K':
                    if (++i == argc) return usage(1);
                    message_key = argv[i];
                    break ;
                case 'S':
                    if (++i == argc) return usage(1);
                    sender = argv[i];
                    break ;
                case 'm':
                    if (++i == argc) return usage(1);
                    message = argv[i];
                    break ;
                default:
                    fprintf(stderr, "error: unknown option '-%c'.\n", argv[i][1]);
                    return usage(1);
            }
        }
    }
#ifdef OSLOG
    if ((log = os_log_create(facility, category)) == NULL) {
#elif defined(SYSLOG)
    openlog("pf", 0, LOG_LOCAL2); if (0) {
#else
    if ((msg = asl_new(ASL_TYPE_MSG)) != NULL) {
        //msg is the asl dictionnary, must fill it with Message, Level, Facility, ...
        char slevel[3];

        snprintf(slevel, sizeof(slevel) / sizeof(*slevel), "%d", level);
        if (sender != NULL) {
            asl_set(msg, ASL_KEY_SENDER, sender);
        }
        asl_set(msg, ASL_KEY_LEVEL, slevel);
        asl_set(msg, ASL_KEY_FACILITY, facility);
        log = NULL; // log = asl_open("pf", "local2", 0);
    } else {
#endif
        fprintf(stderr, "error: cannot initialize log: %s\n", strerror(errno));
        exit(1);
    }
    if (message != NULL) {
        ret = vlog(log, facility, category, message_key, level, msg, message);
    } else {
        size_t line_cap = 1024;
        char * line = malloc(line_cap);
        ssize_t n;

        while(line != NULL && (n = getline(&line, &line_cap, stdin)) >= 0) {
            if (n > 0) {
                if (line[n-1] == '\n') {
                    --n;
                }
                line[n] = 0;
                vlog(log, facility, category, message_key, level, msg, line);
            }
        }
        if (line != NULL) {
            free(line);
        }
    }
#if !defined(SYSLOG) && !defined(OSLOG)
    asl_free(msg);
#endif
    return ret;
}

