#include <stddef.h>
#include <dirent.h>
#include <unistd.h>

char *getcwd(char *buf, size_t size) {
    if (!buf || size < 2) return NULL;
    buf[0] = '/';
    buf[1] = '\0';
    return buf;
}

int chdir(const char *path) {
    (void)path;
    return -1;
}


int rmdir(const char *path) {
    (void)path;
    return -1;
}

DIR *opendir(const char *name) {
    (void)name;
    return NULL;
}

struct dirent *readdir(DIR *dirp) {
    (void)dirp;
    return NULL;
}

int closedir(DIR *dirp) {
    (void)dirp;
    return -1;
}

unsigned int sleep(unsigned int seconds) {
    (void)seconds;
    return 0;
}
