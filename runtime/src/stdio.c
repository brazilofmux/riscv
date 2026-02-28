/* ECALL-based stdio for RV32IM runtime */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define FLAG_READ     0x01
#define FLAG_WRITE    0x02
#define FLAG_APPEND   0x04

#define STDIO_BUF_SIZE 4096

/* Syscall wrappers (defined in syscall.s) */
extern int _write(int fd, const void *buf, int len);
extern int _read(int fd, void *buf, int len);
extern int _openat(int dirfd, const char *pathname, int flags, int mode);
extern int _close(int fd);
extern int _lseek(int fd, int offset, int whence);
extern int _ftruncate(int fd, int length);

static FILE _stdin  = { .fd = 0, .flags = FLAG_READ, .mode = _IONBF, .ungetc_char = -1 };
static FILE _stdout = { .fd = 1, .flags = FLAG_WRITE, .mode = _IOLBF, .ungetc_char = -1 };
static FILE _stderr = { .fd = 2, .flags = FLAG_WRITE, .mode = _IONBF, .ungetc_char = -1 };

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

/* Flush write buffer to fd */
static int internal_flush(FILE *stream) {
    if (stream->buf_pos > 0 && stream->buf_len == 0) {
        /* Write mode: buf_pos = bytes to write, buf_len = 0 */
        size_t total = 0;
        unsigned char *p = (unsigned char *)stream->buffer;
        size_t len = stream->buf_pos;

        while (total < len) {
            int written = _write(stream->fd, p + total, len - total);
            if (written <= 0) {
                stream->error = 1;
                stream->buf_pos = 0;
                return EOF;
            }
            total += written;
        }
        stream->buf_pos = 0;
    }
    return 0;
}

int fflush(FILE *stream) {
    if (!stream) return 0;
    return internal_flush(stream);
}

int fclose(FILE *stream) {
    if (!stream) return EOF;

    fflush(stream);

    if (stream->buffer) free(stream->buffer);

    int result = _close(stream->fd);

    if (stream != stdin && stream != stdout && stream != stderr) {
        free(stream);
    }

    return (result < 0) ? EOF : 0;
}

FILE *fopen(const char *pathname, const char *mode) {
    FILE *f = calloc(1, sizeof(FILE));
    if (!f) return NULL;

    f->ungetc_char = -1;
    f->flags = 0;

    int oflags = 0;
    if (strchr(mode, 'r') && strchr(mode, '+')) {
        f->flags = FLAG_READ | FLAG_WRITE;
        oflags = O_RDWR;
    } else if (strchr(mode, 'r')) {
        f->flags = FLAG_READ;
        oflags = O_RDONLY;
    } else if (strchr(mode, 'w') && strchr(mode, '+')) {
        f->flags = FLAG_READ | FLAG_WRITE;
        oflags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (strchr(mode, 'w')) {
        f->flags = FLAG_WRITE;
        oflags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (strchr(mode, 'a') && strchr(mode, '+')) {
        f->flags = FLAG_READ | FLAG_WRITE | FLAG_APPEND;
        oflags = O_RDWR | O_CREAT | O_APPEND;
    } else if (strchr(mode, 'a')) {
        f->flags = FLAG_WRITE | FLAG_APPEND;
        oflags = O_WRONLY | O_CREAT | O_APPEND;
    } else {
        free(f);
        return NULL;
    }

    f->fd = _openat(AT_FDCWD, pathname, oflags, 0666);
    if (f->fd < 0) {
        free(f);
        return NULL;
    }

    f->mode = _IOFBF;
    f->buffer = malloc(STDIO_BUF_SIZE);
    f->buf_size = f->buffer ? STDIO_BUF_SIZE : 0;
    if (!f->buffer) f->mode = _IONBF;

    return f;
}

FILE *freopen(const char *pathname, const char *mode, FILE *stream) {
    if (stream && stream != stdin && stream != stdout && stream != stderr) {
        fclose(stream);
    }
    if (pathname == NULL) {
        return stream;
    }
    return fopen(pathname, mode);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || !ptr) return 0;
    size_t total_bytes = size * nmemb;
    if (total_bytes == 0) return 0;

    /* Lazy alloc for stdout */
    if (stream == stdout && !stream->buffer && stream->mode != _IONBF) {
        stream->buffer = malloc(STDIO_BUF_SIZE);
        stream->buf_size = stream->buffer ? STDIO_BUF_SIZE : 0;
        if (!stream->buffer) stream->mode = _IONBF;
    }

    if (stream->mode == _IONBF || !stream->buffer) {
        /* Unbuffered: write directly */
        size_t bytes_written = 0;
        const unsigned char *src = ptr;

        while (bytes_written < total_bytes) {
            int written = _write(stream->fd, src + bytes_written, total_bytes - bytes_written);
            if (written <= 0) {
                stream->error = 1;
                break;
            }
            bytes_written += written;
        }
        return bytes_written / size;
    }

    /* Buffered write */
    const unsigned char *src = ptr;
    size_t bytes_processed = 0;

    while (bytes_processed < total_bytes) {
        size_t avail = stream->buf_size - stream->buf_pos;
        size_t chunk = total_bytes - bytes_processed;

        if (chunk > avail) chunk = avail;

        memcpy(stream->buffer + stream->buf_pos, src + bytes_processed, chunk);
        stream->buf_pos += chunk;
        bytes_processed += chunk;

        if (stream->buf_pos == stream->buf_size) {
            if (internal_flush(stream) == EOF) break;
        }
    }

    if (stream->mode == _IOLBF) {
        if (memchr(ptr, '\n', total_bytes)) {
             fflush(stream);
        }
    }

    return bytes_processed / size;
}

static size_t fread_fill_buffer(FILE *stream) {
    if (!stream->buffer || stream->buf_size == 0) return 0;

    int bytes_read = _read(stream->fd, stream->buffer, stream->buf_size);

    if (bytes_read <= 0) {
        if (bytes_read == 0) stream->eof = 1;
        else stream->error = 1;
        return 0;
    }

    stream->buf_pos = 0;
    stream->buf_len = bytes_read;

    return bytes_read;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!stream || !ptr) return 0;
    size_t total = size * nmemb;
    if (total == 0) return 0;

    /* If we were writing, flush first */
    if (stream->buf_len == 0 && stream->buf_pos > 0) {
        fflush(stream);
    }

    unsigned char *dest = ptr;
    size_t remaining = total;
    size_t bytes_copied = 0;

    /* Handle ungetc */
    if (stream->ungetc_char >= 0 && remaining > 0) {
        *dest++ = (unsigned char)stream->ungetc_char;
        stream->ungetc_char = -1;
        stream->eof = 0;
        remaining--;
        bytes_copied++;
    }

    while (remaining > 0) {
        size_t avail = stream->buf_len - stream->buf_pos;

        if (avail > 0) {
            size_t to_copy = (avail < remaining) ? avail : remaining;
            memcpy(dest, stream->buffer + stream->buf_pos, to_copy);
            stream->buf_pos += to_copy;
            dest += to_copy;
            remaining -= to_copy;
            bytes_copied += to_copy;
        } else if (stream->buffer && stream->buf_size > 0 && remaining < stream->buf_size) {
            if (fread_fill_buffer(stream) == 0) break;
        } else {
            /* Direct read for large or unbuffered reads */
            int nread = _read(stream->fd, dest, remaining);
            if (nread <= 0) {
                if (nread == 0) stream->eof = 1;
                else stream->error = 1;
                break;
            }
            dest += nread;
            remaining -= nread;
            bytes_copied += nread;
        }
    }

    return bytes_copied / size;
}

int fgetc(FILE *stream) {
    if (stream->ungetc_char >= 0) {
        int c = stream->ungetc_char;
        stream->ungetc_char = -1;
        stream->eof = 0;
        return c;
    }
    unsigned char c;
    if (fread(&c, 1, 1, stream) != 1) return EOF;
    return c;
}

int getc(FILE *stream) {
    return fgetc(stream);
}

int getchar(void) {
    return fgetc(stdin);
}

int fputc(int c, FILE *stream) {
    unsigned char ch = c;
    if (fwrite(&ch, 1, 1, stream) != 1) return EOF;
    return c;
}

int putc(int c, FILE *stream) {
    return fputc(c, stream);
}

int putchar(int c) {
    return fputc(c, stdout);
}

char *fgets(char *s, int size, FILE *stream) {
    if (!s || size <= 0) return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (i == 0) return NULL;
            break;
        }
        s[i++] = c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) {
    size_t len = strlen(s);
    if (fwrite(s, 1, len, stream) != len) return EOF;
    return 0;
}

int puts(const char *s) {
    if (fputs(s, stdout) == EOF) return EOF;
    if (putchar('\n') == EOF) return EOF;
    return 0;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) return -1;

    fflush(stream);
    stream->buf_pos = 0;
    stream->buf_len = 0;

    int result = _lseek(stream->fd, offset, whence);
    if (result < 0) {
        stream->error = 1;
        return -1;
    }

    stream->eof = 0;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) return -1L;

    int pos = _lseek(stream->fd, 0, SEEK_CUR);
    if (pos < 0) {
        stream->error = 1;
        return -1L;
    }

    /* Adjust for buffered data */
    if (stream->buf_len == 0 && stream->buf_pos > 0) {
        /* Write mode */
        pos += stream->buf_pos;
    } else if (stream->buf_len > 0) {
        /* Read mode */
        pos -= (stream->buf_len - stream->buf_pos);
    }

    return (long)pos;
}

void rewind(FILE *stream) {
    fseek(stream, 0, SEEK_SET);
    clearerr(stream);
}

int feof(FILE *stream) {
    return stream ? stream->eof : 0;
}

int ferror(FILE *stream) {
    return stream ? stream->error : 0;
}

void clearerr(FILE *stream) {
    if (stream) {
        stream->error = 0;
        stream->eof = 0;
    }
}

int fileno(FILE *stream) {
    if (!stream) return -1;
    return stream->fd;
}

void perror(const char *s) {
    if (s && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs("error\n", stderr);
}

int ungetc(int c, FILE *stream) {
    if (c == EOF || !stream) return EOF;
    stream->ungetc_char = (unsigned char)c;
    stream->eof = 0;
    return (unsigned char)c;
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size) {
    (void)stream; (void)buf; (void)mode; (void)size;
    return 0;
}

extern int _unlinkat(int dirfd, const char *pathname, int flags);

int remove(const char *pathname) {
    return _unlinkat(-100, pathname, 0);  /* AT_FDCWD = -100 */
}

int rename(const char *oldpath, const char *newpath) {
    (void)oldpath; (void)newpath;
    return -1;
}

FILE *tmpfile(void) {
    return NULL;  /* no temp file support on bare metal */
}

/* POSIX-layer wrappers for unistd.h functions */
int open(const char *pathname, int flags, ...) {
    return _openat(AT_FDCWD, pathname, flags, 0666);
}

int close(int fd) {
    return _close(fd);
}

int read(int fd, void *buf, size_t count) {
    return _read(fd, buf, count);
}

int write(int fd, const void *buf, size_t count) {
    return _write(fd, buf, count);
}

int lseek(int fd, int offset, int whence) {
    return _lseek(fd, offset, whence);
}

int ftruncate(int fd, int length) {
    return _ftruncate(fd, length);
}
