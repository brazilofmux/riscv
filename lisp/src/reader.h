#ifndef LISP_READER_H
#define LISP_READER_H

#include "types.h"

val_t lisp_read(int *eof);
void reader_init(void);

#endif
