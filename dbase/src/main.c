#include <stdio.h>
#include <string.h>
#include "dbf.h"
#include "command.h"
#include "program.h"
#include "screen.h"
#include "util.h"

static dbf_t current_db;

int main(void) {
    char line[256];

    printf("dBASE III Clone for SLOW-32\n");
    printf("Stage 9: AST Expressions\n\n");

    dbf_init(&current_db);
    prog_init();

    for (;;) {
        printf(". ");
        if (read_line(line, sizeof(line)) < 0)
            break;
        trim_right(line);
        if (line[0] == '\0')
            continue;

        /* Preprocess for && comments and macros (interactive mode) */
        prog_preprocess(line, cmd_get_memvar_store());

        if (cmd_execute(&current_db, line))
            break;
    }

    cmd_close_all();
    screen_shutdown();

    return 0;
}
