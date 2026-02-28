#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int open(const char *pathname, int flags, ...);
int close(int fd);
int read(int fd, void *buf, size_t count);
int write(int fd, const void *buf, size_t count);
int lseek(int fd, int offset, int whence);

char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int mkdir(const char *path, int mode);
int rmdir(const char *path);
unsigned int sleep(unsigned int seconds);

#endif
