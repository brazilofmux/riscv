/*
** RV32IM Lua REPL entry point
** Minimal standalone interpreter: reads stdin, executes Lua code.
**
** Modes:
**   - If stdin is a pipe/file: read all input, execute as one chunk
**   - Single-line expressions are printed (like interactive Lua)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#define INITIAL_BUF 4096

static int report(lua_State *L, int status) {
    if (status != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        if (msg == NULL) msg = "(error object is not a string)";
        fprintf(stderr, "%s\n", msg);
        lua_pop(L, 1);
    }
    return status;
}

/*
** Read all of stdin into a dynamically allocated buffer.
*/
static char *read_all_stdin(size_t *out_len) {
    size_t cap = INITIAL_BUF;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    int c;
    while ((c = getchar()) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *newbuf = (char *)realloc(buf, cap);
            if (!newbuf) { free(buf); return NULL; }
            buf = newbuf;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    if (out_len) *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    lua_State *L;
    int status = LUA_OK;

    (void)argc; (void)argv;

    L = luaL_newstate();
    if (L == NULL) {
        fprintf(stderr, "cannot create Lua state: not enough memory\n");
        return EXIT_FAILURE;
    }
    /* Read input BEFORE opening libs (io lib may interfere with stdin) */
    size_t input_len = 0;
    char *input = read_all_stdin(&input_len);

    luaL_openlibs(L);
    if (!input) {
        fprintf(stderr, "out of memory reading input\n");
        lua_close(L);
        return EXIT_FAILURE;
    }

    if (input_len > 0) {
        int is_single_line = (strchr(input, '\n') == NULL ||
            (strchr(input, '\n') == input + input_len - 1));

        /* For single lines, try as expression first */
        if (is_single_line) {
            char *expr_buf = (char *)malloc(input_len + 16);
            if (expr_buf) {
                size_t slen = input_len;
                while (slen > 0 && (input[slen-1] == '\n' || input[slen-1] == '\r'))
                    slen--;
                snprintf(expr_buf, input_len + 16, "return %.*s", (int)slen, input);
                status = luaL_loadstring(L, expr_buf);
                if (status == LUA_OK) {
                    status = lua_pcall(L, 0, LUA_MULTRET, 0);
                    if (status == LUA_OK) {
                        int n = lua_gettop(L);
                        if (n > 0) {
                            luaL_checkstack(L, LUA_MINSTACK, "too many results");
                            lua_getglobal(L, "print");
                            lua_insert(L, 1);
                            status = lua_pcall(L, n, 0, 0);
                            report(L, status);
                        }
                        free(expr_buf);
                        free(input);
                        fflush(stdout);
                        lua_close(L);
                        return (status == LUA_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
                    }
                }
                free(expr_buf);
                lua_settop(L, 0);
            }
        }

        /* Execute as a full chunk (or single-line statement) */
        status = luaL_loadbuffer(L, input, input_len, "=stdin");
        if (status == LUA_OK) {
            status = lua_pcall(L, 0, 0, 0);
        }
        report(L, status);
    }

    free(input);
    fflush(stdout);
    lua_close(L);
    return (status == LUA_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}
