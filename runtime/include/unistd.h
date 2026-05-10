#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* Access modes for access(). */
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

/* lseek() whence values. */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int open(const char *pathname, int flags, ...);
int close(int fd);
int read(int fd, void *buf, size_t count);
int write(int fd, const void *buf, size_t count);
int lseek(int fd, int offset, int whence);
int ftruncate(int fd, int length);
int access(const char *pathname, int mode);
int unlink(const char *pathname);
int isatty(int fd);

char         *getcwd(char *buf, size_t size);
int           chdir(const char *path);
int           mkdir(const char *path, int mode);
int           rmdir(const char *path);
unsigned int  sleep(unsigned int seconds);
int           usleep(unsigned int usec);

#endif
