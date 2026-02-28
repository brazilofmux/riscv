#include "print.h"
#include "heap.h"
#include <stdio.h>

static void print_val(val_t v, int depth) {
    if (depth > 1000) {
        printf("...");
        return;
    }
    if (IS_NIL(v)) {
        printf("()");
        return;
    }
    if (IS_FIXNUM(v)) {
        printf("%d", UNFIXNUM(v));
        return;
    }
    object_t *o = AS_OBJ(v);
    switch (o->type) {
    case OBJ_SYMBOL:
        printf("%s", o->symbol.name);
        break;
    case OBJ_STRING:
        putchar('"');
        {
            int i;
            for (i = 0; i < o->string.len; i++) {
                char c = o->string.data[i];
                if (c == '"') printf("\\\"");
                else if (c == '\\') printf("\\\\");
                else if (c == '\n') printf("\\n");
                else putchar(c);
            }
        }
        putchar('"');
        break;
    case OBJ_CONS:
        putchar('(');
        print_val(o->cons.car, depth + 1);
        {
            val_t rest = o->cons.cdr;
            while (IS_PTR(rest) && AS_OBJ(rest)->type == OBJ_CONS) {
                putchar(' ');
                print_val(CAR(rest), depth + 1);
                rest = CDR(rest);
            }
            if (!IS_NIL(rest)) {
                printf(" . ");
                print_val(rest, depth + 1);
            }
        }
        putchar(')');
        break;
    case OBJ_LAMBDA:
        printf("#<lambda>");
        break;
    case OBJ_BUILTIN:
        printf("#<builtin %s>", o->builtin.name);
        break;
    default:
        printf("#<unknown>");
        break;
    }
}

void lisp_print(val_t v) {
    print_val(v, 0);
}
