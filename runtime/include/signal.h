/* signal.h — POSIX signal API.
 *
 * The microcontroller profile has no real signal delivery, so handlers
 * that aren't SIG_DFL/SIG_IGN never fire. The interface is here so code
 * compiles; the bodies of signal()/sigaction() etc. are stubs in
 * src/stubs.c that just remember/ignore the request. */
#ifndef _SIGNAL_H
#define _SIGNAL_H

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef int          sig_atomic_t;
typedef unsigned long sigset_t;

/* Linux RV32 signal numbers (same as common Unix). */
#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19

/* sigaction flags. */
#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

/* siginfo_t — only the size matters for the stubs; the layout matches
 * Linux closely so dereferencing through it is safe. */
typedef struct {
    int   si_signo;
    int   si_errno;
    int   si_code;
    int   _pad0;
    void *si_addr;
    char  _pad1[112];
} siginfo_t;

struct sigaction {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t *, void *);
    sigset_t sa_mask;
    int      sa_flags;
    void   (*sa_restorer)(void);
};

sighandler_t signal(int signum, sighandler_t handler);
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signum);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int kill(int pid, int signum);
int raise(int signum);

#endif /* _SIGNAL_H */
