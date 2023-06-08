/*
 * Copyright (C) 2021,2023 Vincent Sallaberry
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
#include "version.h"

#ifndef __APPLE__
# define ASLPIPE_SYSLOG
#else
//# define ASLPIPE_OSLOG
//# define ASLPIPE_SYSLOG
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <stdarg.h>
#ifdef ASLPIPE_OSLOG
#include <os/log.h>
#elif defined(ASLPIPE_SYSLOG)
#include <syslog.h>
#else
#include <asl.h>
#endif



#define safe_snprintf(ret, s, sz, ...) \
    (((ret) = snprintf(s, sz, __VA_ARGS__)) > 0 ? ((ret) >= (sz) ? (sz) - 1 : (ret)) : 0)

#ifdef ASLPIPE_OSLOG
int vlog(os_log_t log, const char * facility, const char * category, const char * message_key, int level, void * msg, const char *message) {
    os_log_with_type(log, level, "%s", message);
    return 0;
}
#elif defined ASLPIPE_SYSLOG
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

static pid_t    fork_and_exec(const char * const * argv, FILE ** infile);
static int      check_child(pid_t child, FILE * infile, int * ret);

static int usage(int ret) {
    FILE * out = ret == 0 ? stdout : stderr;
    fprintf(out, "%s v%s git-%s - Copyright (c) 2021,2023 Vincent Sallaberry under GNU GPL.\n",
            BUILD_APPNAME, APP_VERSION, BUILD_GITREV);
    fprintf(out, "Usage: aslpipe [-h] [-V] [-F facility] [-l level] [-C category] [-S sender]\n"
                 "               [-K message_key] [-k key value[ -k ...]] [-m message] [command[ args]]\n"
                 "   stdin is used if message and command not given (-m)\n");

    return ret;
}
struct keyarg_s { const char * key; const char * value; struct keyarg_s * next; };
int main(int argc, const char*const* argv) {
#ifdef ASLPIPE_OSLOG
    os_log_t        log;
    void *          msg         = NULL;
    int             level       = OS_LOG_TYPE_INFO;
#elif defined(ASLPIPE_SYSLOG)
    void *          log         = NULL, msg = NULL;;
    int             level       = LOG_NOTICE;
#else
    //asl.h: Emergency:0, Alert:1, Critical:2, Error:3, Warning:4, Notice:5, Info:6, Debug:7
    //       ASL_{STRING,LEVEL}_{EMERG,ALERT,CRIT,ERR,WARNING,NOTICE,INFO,DEBUG}
    asl_object_t    log;
    asl_object_t    msg;
    int             level       = ASL_LEVEL_NOTICE;
#endif
    FILE *          infile      = stdin;
    struct keyarg_s*keys        = NULL;
    const char *    facility    = "local2";
    const char *    category    = "local2";
    const char *    message_key = "Message";
    const char *    sender      = NULL;
    const char *    message     = NULL;
    int             ret         = 0;
    int             command_idx = argc;

    for (int i=1; i < argc; ++i) {
        if (*argv[i] == '-') {
            switch (argv[i][1]) {
                case 'h':
                    return usage(0);
                    break ;
                case 'V':
                    fprintf(stdout, "%s %s git-%s\n", BUILD_APPNAME, APP_VERSION, BUILD_GITREV);
                    return 0;
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
                case 'k': {
                    if (i + 2 >= argc) return usage(1);
                    struct keyarg_s * keyarg = calloc(1, sizeof(*keyarg));
                    if (keyarg == NULL) { perror("malloc"); break ; }
                    if (keys == NULL) keys = keyarg;
                    else {
                        struct keyarg_s * elt; for (elt = keys; elt->next != NULL; elt = elt->next) ;/* nothing */
                        elt->next = keyarg;
                    }
                    keyarg->next = NULL;
                    keyarg->key = argv[++i];
                    keyarg->value = argv[++i];
                    break ;
                }
                default:
                    fprintf(stderr, "error: unknown option '-%c'.\n", argv[i][1]);
                    return usage(1);
            }
        } else {
            command_idx = i;
            break ;
        }
    }
#ifdef ASLPIPE_OSLOG
    if ((log = os_log_create(facility, category)) == NULL) {
#elif defined(ASLPIPE_SYSLOG)
    openlog("pf", 0, LOG_LOCAL2); if (0) {
#else
    if ((msg = asl_new(ASL_TYPE_MSG)) != NULL) {
        //msg is the asl dictionnary, must fill it with Message, Level, Facility, ...
        char slevel[3];

        snprintf(slevel, sizeof(slevel) / sizeof(*slevel), "%d", level);
        asl_set(msg, ASL_KEY_LEVEL, slevel);
        asl_set(msg, ASL_KEY_FACILITY, facility);
        if (command_idx < argc) {
            asl_set(msg, ASL_KEY_SENDER, argv[command_idx]);
            char * slash = strrchr(argv[command_idx], '/');
            asl_set(msg, "ShortSender", slash ? slash + 1 : argv[command_idx]);
        }
        if (sender != NULL) {
            asl_set(msg, ASL_KEY_SENDER, sender);
            asl_set(msg, "ShortSender", sender);
        }
        for (struct keyarg_s * elt = keys; elt != NULL; ) {
            struct keyarg_s * cur = elt;
            //fprintf(stderr, "aslpipe: key <%s> = '%s'\n", elt->key, elt->value);
            asl_set(msg, elt->key, elt->value);
            elt = elt->next;
            free(cur);
        }
        keys = NULL;
        log = NULL; // log = asl_open("pf", "local2", 0);
    } else {
#endif
        fprintf(stderr, "error: cannot initialize log: %s\n", strerror(errno));
        exit(1);
    }
    if (message != NULL) {
        ret = vlog(log, facility, category, message_key, level, msg, message);
    } else {
        size_t line_init_cap = 1024, line_cap = line_init_cap;
        char * line = malloc(line_cap);
        pid_t child = (pid_t) -1;
        ssize_t n;

        if (line && command_idx < argc) {
            size_t len;
            n = safe_snprintf(len, line, line_cap, "launching from %s v%s git-%s: [%s",
                              argv[0], APP_VERSION, BUILD_GITREV, argv[command_idx]);
            for (int i = command_idx + 1; i < argc; ++i)
                n += safe_snprintf(len, line + n, line_cap - n, " %s", argv[i]);
            n += safe_snprintf(len, line + n, line_cap - n, "] uid:%d gid:%d...", getuid(), getgid());
            vlog(log, facility, category, message_key, level, msg, line);
            if ((child = fork_and_exec(argv + command_idx, /*&infile*/NULL)) < 0) {
                ret = (int) child;
            }
        }

        while(ret == 0 && line != NULL && (n = getline(&line, &line_cap, infile)) >= 0) {
            if (n > 0) {
                if (line[n-1] == '\n') {
                    --n;
                }
                line[n] = 0;
                vlog(log, facility, category, message_key, level, msg, line);
                if (line_cap > line_init_cap)
                    line = realloc(line, (line_cap = line_init_cap));
            }
        }
        if (line != NULL) {
            free(line);
        }

        check_child(child, infile, &ret);
    }
#if !defined(ASLPIPE_SYSLOG) && !defined(ASLPIPE_OSLOG)
    asl_free(msg);
#endif
    return ret;
}

//static int fderr = -1;;
/* signal handler for fork_and_exec(), ignoring and forwarding signals to child */
static void sig_handler(int sig, siginfo_t * si, void * context) {
    static volatile sig_atomic_t pid = 0;
    if (pid == 0 && si == NULL && context == NULL) {
        //dprintf(fderr, "(%d) the pid is %d\n", getpid(), sig);
        pid = (pid_t) sig;
        return ;
    }
    //dprintf(fderr, "(%d) signal %d received from %d\n", getpid(), sig, si?si->si_pid:-1);
    if (sig != SIGCHLD && sig != SIGPIPE) {
        if (pid != 0) {
            kill(pid, sig);
            if (sig == SIGTSTP)
                kill(getpid(), SIGSTOP);
        }
    }
    //fsync(fderr);
}

static pid_t fork_and_exec(const char*const*argv, FILE ** infile) {
    pid_t               pid;
    struct sigaction    sa = { .sa_sigaction = sig_handler, .sa_flags = SA_RESTART | SA_SIGINFO};
    int                 pipefd[2];

    int sigs[] = { SIGINT, SIGHUP, SIGTERM, SIGQUIT, SIGUSR1, SIGUSR2, SIGCONT, SIGTSTP, SIGPIPE, SIGCHLD }; // SIGSTOP }; //, SIGPIPE };

    /* install signal handler and give to him the program pid */
    sigemptyset(&sa.sa_mask);
    for (unsigned int i = 0; i < sizeof(sigs) / sizeof(*sigs); i++) {
        if (sigaction(sigs[i], &sa, NULL) < 0)
            fprintf(stderr, "forward sigaction(%s): %s\n", strsignal(sigs[i]), strerror(errno));
    }

    if (pipe(pipefd) < 0) {
        fprintf(stderr, "pipe(): %s\n", strerror(errno));
        return -1;
    }

    if ((pid = fork()) < 0) {
        perror("fork");
        return (pid_t) -1;
    } else if (pid == 0) {
        /* son */
        int fderr_save = dup(STDERR_FILENO);
        //fderr=fderr_save;

        // ** dup2(dupfd, redirectedfd)
        if (dup2(pipefd[1], STDOUT_FILENO) < 0
        ||  dup2(pipefd[1], STDERR_FILENO) < 0) {
            fprintf(stderr, "dup2(): %s\n", strerror(errno));
            dprintf(fderr_save, "dup2(): %s\n", strerror(errno));
            return -1;
        }
        close(pipefd[0]);
        /* son : give to hand to father, and continue execution */
        sched_yield();

        if (execvp(*argv, (char*const*)argv) < 0) {
            dprintf(fderr_save, "error: cannot launch `%s`: %s\n", *argv, strerror(errno));
            fprintf(stderr, "error: cannot launch `%s`: %s\n", *argv, strerror(errno));
            exit(1);
        }
        /* not reachable */
        return -1;
    } else {
        //fderr=STDERR_FILENO;
        /* father */
        sig_handler(pid, NULL, NULL);
        close(pipefd[1]);

        if (infile)
            *infile = fdopen(pipefd[0], "r");
        else if (dup2(pipefd[0], STDIN_FILENO) < 0) {
            fprintf(stderr, "error: cannot dup2 STDIN: %s\n", strerror(errno));
        }

        return pid;
    }
}

static int check_child(pid_t child, FILE * infile, int * pret) {
    if (child == (pid_t) -1) {
        return 0;
    }
    int ret = 0;
    int status;
    pid_t wpid;

    /* wait for termination of program */
    if ((wpid = waitpid(child, &status, 0 /* options */)) <= 0)
        perror("waitpid");

    if (infile != stdin && infile != NULL)
        fclose(infile);
    /* Terminate with child status */
    if (WIFEXITED(status)) {
        ret = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "child terminated by signal %d\n", WTERMSIG(status));
        ret = -100-WTERMSIG(status);
    } else {
        fprintf(stderr, "child terminated by ?\n");
        ret = -100;
    }
    if (pret) *pret = ret;
    return ret;
}

