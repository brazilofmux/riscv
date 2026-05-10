/* Stubs and host-mediated impls for POSIX surface beyond the core
 * stdio/string/etc. set. opendir/readdir/closedir use ECALLs 90-92
 * to keep a host DIR* table; the rest stay stubbed because either
 * the syscall doesn't exist or the microcontroller profile rejects
 * it on purpose (mmap, real signals). */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>

/* ---- filesystem ---- */

char *getcwd(char *buf, size_t size) {
    if (!buf || size < 2) return NULL;
    buf[0] = '/';
    buf[1] = '\0';
    return buf;
}

int chdir(const char *path) { (void)path; return -1; }
int rmdir(const char *path) { (void)path; return -1; }
int access(const char *pathname, int mode) { (void)pathname; (void)mode; return -1; }
int isatty(int fd) { return (fd >= 0 && fd <= 2) ? 1 : 0; }

extern int _opendir(const char *path);
extern int _readdir(int handle, struct dirent *buf);
extern int _closedir(int handle);

/* readdir is non-reentrant — single static buffer per the POSIX spec. */
static struct dirent _readdir_buf;

DIR *opendir(const char *name) {
    if (!name) return NULL;
    int h = _opendir(name);
    if (h < 0) return NULL;
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) { _closedir(h); return NULL; }
    d->handle = h;
    return d;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp) return NULL;
    if (_readdir(dirp->handle, &_readdir_buf) != 0) return NULL;
    return &_readdir_buf;
}

int closedir(DIR *dirp) {
    if (!dirp) return -1;
    int rc = _closedir(dirp->handle);
    free(dirp);
    return rc;
}

int  dirfd(DIR *dirp)     { return dirp ? dirp->handle : -1; }
void rewinddir(DIR *dirp) { (void)dirp; /* not supported by the ECALL */ }

/* ---- sleep ---- */

unsigned int sleep(unsigned int seconds) { (void)seconds; return 0; }
int          usleep(unsigned int usec)   { (void)usec; return 0; }

/* ---- signals (no real delivery — handlers never fire) ---- */

sighandler_t signal(int signum, sighandler_t handler) {
    (void)signum;
    (void)handler;
    return SIG_DFL;
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    (void)signum;
    (void)act;
    if (oldact) {
        oldact->sa_handler = SIG_DFL;
        oldact->sa_sigaction = NULL;
        oldact->sa_mask = 0;
        oldact->sa_flags = 0;
        oldact->sa_restorer = NULL;
    }
    return 0;
}

int sigemptyset(sigset_t *set)        { if (set) *set = 0; return 0; }
int sigfillset(sigset_t *set)         { if (set) *set = (sigset_t)-1; return 0; }
int sigaddset(sigset_t *set, int sig) { if (set) *set |=  (1UL << (sig & 31)); return 0; }
int sigdelset(sigset_t *set, int sig) { if (set) *set &= ~(1UL << (sig & 31)); return 0; }
int sigismember(const sigset_t *set, int sig) {
    if (!set) return 0;
    return (*set & (1UL << (sig & 31))) ? 1 : 0;
}
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    (void)how; (void)set;
    if (oldset) *oldset = 0;
    return 0;
}
int kill(int pid, int sig)  { (void)pid; (void)sig; return -1; }
int raise(int sig)          { (void)sig; return -1; }

/* ---- mmap (no real implementation) ---- */

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset) {
    (void)addr; (void)length; (void)prot; (void)flags; (void)fd; (void)offset;
    return MAP_FAILED;
}
int munmap(void *addr, size_t length) { (void)addr; (void)length; return -1; }
int mprotect(void *addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot; return -1;
}

/* ---- itimer (no host signal plumbing) ---- */

int setitimer(int which, const struct itimerval *new_value,
              struct itimerval *old_value) {
    (void)which; (void)new_value;
    if (old_value) {
        old_value->it_interval.tv_sec  = 0;
        old_value->it_interval.tv_usec = 0;
        old_value->it_value.tv_sec     = 0;
        old_value->it_value.tv_usec    = 0;
    }
    return 0;
}
int getitimer(int which, struct itimerval *cur) {
    (void)which;
    if (cur) {
        cur->it_interval.tv_sec  = 0;
        cur->it_interval.tv_usec = 0;
        cur->it_value.tv_sec     = 0;
        cur->it_value.tv_usec    = 0;
    }
    return 0;
}
