#ifndef __SETJMP_H__
#define __SETJMP_H__

typedef int jmp_buf[14];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif /* __SETJMP_H__ */
