/* ucontext.h — compile-only stub.
 *
 * The struct layout matches Linux roughly so dereferencing through it
 * is safe even though we provide no actual {get,make,set,swap}context
 * implementation. Lets code that *mentions* ucontext_t compile; any
 * call to one of the four context functions will link-fail (intended).
 *
 * Borrowed in spirit from slow-32/selfhost/stage07/include/ucontext.h. */
#ifndef _UCONTEXT_H
#define _UCONTEXT_H

#include <stddef.h>
#include <signal.h>

typedef struct {
    char _mcontext_blob[1024];   /* opaque, never accessed */
} mcontext_t;

typedef struct {
    void *ss_sp;
    int   ss_flags;
    size_t ss_size;
} stack_t;

typedef struct ucontext {
    unsigned long     uc_flags;
    struct ucontext  *uc_link;
    stack_t           uc_stack;
    mcontext_t        uc_mcontext;
    sigset_t          uc_sigmask;
} ucontext_t;

/* Declared but not implemented — link will fail if anyone calls them. */
int  getcontext(ucontext_t *ucp);
int  setcontext(const ucontext_t *ucp);
void makecontext(ucontext_t *ucp, void (*func)(void), int argc, ...);
int  swapcontext(ucontext_t *oucp, const ucontext_t *ucp);

#endif /* _UCONTEXT_H */
