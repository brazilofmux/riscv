#include "types.h"
#include "heap.h"
#include "reader.h"
#include "print.h"
#include "eval.h"
#include "env.h"
#include "builtin.h"
#include <stdio.h>

int main(void) {
    heap_init();
    reader_init();
    eval_init();

    global_env = env_create(NIL);
    PUSH_ROOT(global_env);

    builtins_register(global_env);

    /* REPL: read-eval-print loop */
    for (;;) {
        g_error = 0;

        int eof = 0;
        val_t expr = lisp_read(&eof);
        if (eof) break;
        if (g_error) {
            printf("Error: %s\n", g_errmsg);
            continue;
        }

        PUSH_ROOT(expr);
        val_t result = eval(expr, global_env);
        POP_ROOTS(1);

        if (g_error) {
            printf("Error: %s\n", g_errmsg);
            continue;
        }

        if (!IS_VOID(result)) {
            lisp_print(result);
            putchar('\n');
        }
    }

    return 0;
}
