#ifndef _SIGNAL_H
#define _SIGNAL_H

typedef int sig_atomic_t;

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

#define SIGINT   2
#define SIGTERM  15

typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);

#endif
