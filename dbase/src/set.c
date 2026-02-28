#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "set.h"
#include "util.h"

void set_init(set_options_t *opts) {
    opts->talk = 1;
    opts->deleted = 0;    /* OFF = show deleted records */
    opts->exact = 0;      /* OFF = prefix matching */
    opts->heading = 1;
    opts->confirm = 0;
    opts->bell = 1;
    opts->safety = 1;
    opts->console = 1;
    opts->decimals = 2;
    opts->date_format = DATE_AMERICAN;
    opts->device = 0;  /* SCREEN */
    opts->century = 0;
    opts->escape = 1;
    opts->echo = 0;
    opts->margin = 0;
    opts->message_row = -1;  /* disabled by default */
    opts->wrap = 0;
    opts->softseek = 0;
    opts->unique = 0;
    opts->memowidth = 50;
    opts->epoch = 0;
    opts->mark = '\0';
    opts->alternate_fp = NULL;
    opts->alternate_on = 0;
    opts->path[0] = '\0';
}

static int parse_on_off(const char *p) {
    p = skip_ws(p);
    if (str_imatch(p, "ON")) return 1;
    if (str_imatch(p, "OFF")) return 0;
    return -1;
}

static const char *format_name(date_format_t f) {
    switch (f) {
    case DATE_AMERICAN: return "AMERICAN";
    case DATE_ANSI:     return "ANSI";
    case DATE_BRITISH:  return "BRITISH";
    case DATE_FRENCH:   return "FRENCH";
    case DATE_GERMAN:   return "GERMAN";
    case DATE_ITALIAN:  return "ITALIAN";
    case DATE_JAPAN:    return "JAPAN";
    }
    return "AMERICAN";
}

void set_display(const set_options_t *opts) {
    printf("TALK      = %s\n", opts->talk ? "ON" : "OFF");
    printf("DELETED   = %s\n", opts->deleted ? "ON" : "OFF");
    printf("EXACT     = %s\n", opts->exact ? "ON" : "OFF");
    printf("HEADING   = %s\n", opts->heading ? "ON" : "OFF");
    printf("CONFIRM   = %s\n", opts->confirm ? "ON" : "OFF");
    printf("BELL      = %s\n", opts->bell ? "ON" : "OFF");
    printf("SAFETY    = %s\n", opts->safety ? "ON" : "OFF");
    printf("CONSOLE   = %s\n", opts->console ? "ON" : "OFF");
    printf("DECIMALS  = %d\n", opts->decimals);
    printf("DATE      = %s\n", format_name(opts->date_format));
    printf("DEVICE    = %s\n", opts->device ? "PRINT" : "SCREEN");
    printf("CENTURY   = %s\n", opts->century ? "ON" : "OFF");
    printf("ESCAPE    = %s\n", opts->escape ? "ON" : "OFF");
    printf("ECHO      = %s\n", opts->echo ? "ON" : "OFF");
    printf("MARGIN    = %d\n", opts->margin);
    if (opts->message_row >= 0)
        printf("MESSAGE   = %d\n", opts->message_row);
    else
        printf("MESSAGE   = (off)\n");
    printf("WRAP      = %s\n", opts->wrap ? "ON" : "OFF");
    printf("MEMOWIDTH = %d\n", opts->memowidth);
    printf("EPOCH     = %d\n", opts->epoch);
    if (opts->mark)
        printf("MARK      = %c\n", opts->mark);
    else
        printf("MARK      = (default)\n");
    printf("SOFTSEEK  = %s\n", opts->softseek ? "ON" : "OFF");
    printf("UNIQUE    = %s\n", opts->unique ? "ON" : "OFF");
    printf("ALTERNATE = %s\n", opts->alternate_on ? "ON" : "OFF");
    if (opts->path[0])
        printf("PATH      = %s\n", opts->path);
    else
        printf("PATH      = (none)\n");
}

void set_execute(set_options_t *opts, const char *arg) {
    const char *p = skip_ws(arg);
    int val;

    if (*p == '\0') {
        set_display(opts);
        return;
    }

    if (str_imatch(p, "TALK")) {
        val = parse_on_off(p + 4);
        if (val >= 0) opts->talk = val;
        else printf("Syntax: SET TALK ON/OFF\n");
        return;
    }
    if (str_imatch(p, "DELETED")) {
        val = parse_on_off(p + 7);
        if (val >= 0) opts->deleted = val;
        else printf("Syntax: SET DELETED ON/OFF\n");
        return;
    }
    if (str_imatch(p, "EXACT")) {
        val = parse_on_off(p + 5);
        if (val >= 0) opts->exact = val;
        else printf("Syntax: SET EXACT ON/OFF\n");
        return;
    }
    if (str_imatch(p, "HEADING")) {
        val = parse_on_off(p + 7);
        if (val >= 0) opts->heading = val;
        else printf("Syntax: SET HEADING ON/OFF\n");
        return;
    }
    if (str_imatch(p, "CONFIRM")) {
        val = parse_on_off(p + 7);
        if (val >= 0) opts->confirm = val;
        else printf("Syntax: SET CONFIRM ON/OFF\n");
        return;
    }
    if (str_imatch(p, "BELL")) {
        val = parse_on_off(p + 4);
        if (val >= 0) opts->bell = val;
        else printf("Syntax: SET BELL ON/OFF\n");
        return;
    }
    if (str_imatch(p, "SAFETY")) {
        val = parse_on_off(p + 6);
        if (val >= 0) opts->safety = val;
        else printf("Syntax: SET SAFETY ON/OFF\n");
        return;
    }
    if (str_imatch(p, "CONSOLE")) {
        val = parse_on_off(p + 7);
        if (val >= 0) opts->console = val;
        else printf("Syntax: SET CONSOLE ON/OFF\n");
        return;
    }
    if (str_imatch(p, "DECIMALS")) {
        p = skip_ws(p + 8);
        if (str_imatch(p, "TO")) p = skip_ws(p + 2);
        val = atoi(p);
        if (val >= 0 && val <= 18)
            opts->decimals = val;
        else
            printf("DECIMALS must be 0-18.\n");
        return;
    }
    if (str_imatch(p, "DATE")) {
        p = skip_ws(p + 4);
        if (str_imatch(p, "TO")) p = skip_ws(p + 2);
        if (str_imatch(p, "AMERICAN"))  opts->date_format = DATE_AMERICAN;
        else if (str_imatch(p, "ANSI")) opts->date_format = DATE_ANSI;
        else if (str_imatch(p, "BRITISH")) opts->date_format = DATE_BRITISH;
        else if (str_imatch(p, "FRENCH"))  opts->date_format = DATE_FRENCH;
        else if (str_imatch(p, "GERMAN"))  opts->date_format = DATE_GERMAN;
        else if (str_imatch(p, "ITALIAN")) opts->date_format = DATE_ITALIAN;
        else if (str_imatch(p, "JAPAN"))   opts->date_format = DATE_JAPAN;
        else printf("Unknown date format.\n");
        return;
    }

    /* No-op SET options: accepted silently for compatibility */
    if (str_imatch(p, "ECHO")) {
        val = parse_on_off(p + 4);
        if (val >= 0) opts->echo = val;
        return;
    }
    if (str_imatch(p, "MENU")) {
        /* SET MENU or SET MENUS */
        const char *q = p + 4;
        if (*q == 'S' || *q == 's') q++;
        parse_on_off(q);
        return;
    }
    if (str_imatch(p, "STATUS")) {
        parse_on_off(p + 6);
        return;
    }
    if (str_imatch(p, "SCOREBOARD")) {
        parse_on_off(p + 10);
        return;
    }
    if (str_imatch(p, "ESCAPE")) {
        val = parse_on_off(p + 6);
        if (val >= 0) opts->escape = val;
        return;
    }
    if (str_imatch(p, "INTENSITY")) {
        parse_on_off(p + 9);
        return;
    }
    if (str_imatch(p, "UNIQUE")) {
        val = parse_on_off(p + 6);
        if (val >= 0) opts->unique = val;
        return;
    }
    if (str_imatch(p, "SOFTSEEK")) {
        val = parse_on_off(p + 8);
        if (val >= 0) opts->softseek = val;
        return;
    }
    if (str_imatch(p, "NEAR")) {
        val = parse_on_off(p + 4);
        if (val >= 0) opts->softseek = val;
        return;
    }
    if (str_imatch(p, "FUNCTION")) {
        /* SET FUNCTION <n> TO <string> — just skip */
        return;
    }
    if (str_imatch(p, "PRINT")) {
        parse_on_off(p + 5);
        return;
    }
    if (str_imatch(p, "HELP")) {
        parse_on_off(p + 4);
        return;
    }
    if (str_imatch(p, "CENTURY")) {
        val = parse_on_off(p + 7);
        if (val >= 0) opts->century = val;
        return;
    }
    if (str_imatch(p, "PATH")) {
        p = skip_ws(p + 4);
        if (str_imatch(p, "TO")) p = skip_ws(p + 2);
        str_copy(opts->path, p, sizeof(opts->path));
        trim_right(opts->path);
        return;
    }
    if (str_imatch(p, "ALTERNATE")) {
        p = skip_ws(p + 9);
        if (str_imatch(p, "TO")) {
            /* SET ALTERNATE TO <file> — handled in command.c */
            return;
        }
        val = parse_on_off(p);
        if (val >= 0) opts->alternate_on = val;
        else printf("Syntax: SET ALTERNATE ON/OFF or SET ALTERNATE TO <file>\n");
        return;
    }
    if (str_imatch(p, "MARGIN")) {
        p = skip_ws(p + 6);
        if (str_imatch(p, "TO")) p = skip_ws(p + 2);
        val = atoi(p);
        if (val >= 0 && val <= 254)
            opts->margin = val;
        else
            printf("MARGIN must be 0-254.\n");
        return;
    }
    if (str_imatch(p, "MESSAGE")) {
        /* SET MESSAGE TO <row> or SET MESSAGE TO (disable) */
        p = skip_ws(p + 7);
        if (str_imatch(p, "TO")) p = skip_ws(p + 2);
        if (*p >= '0' && *p <= '9') {
            val = atoi(p);
            if (val >= 0 && val <= 24)
                opts->message_row = val;
            else
                printf("MESSAGE row must be 0-24.\n");
        } else {
            opts->message_row = -1;  /* disable */
        }
        return;
    }
    if (str_imatch(p, "MEMOWIDTH")) {
        p = skip_ws(p + 9);
        if (str_imatch(p, "TO")) p = skip_ws(p + 2);
        val = atoi(p);
        if (val >= 8 && val <= 256)
            opts->memowidth = val;
        else
            printf("MEMOWIDTH must be 8-256.\n");
        return;
    }
    if (str_imatch(p, "EPOCH")) {
        p = skip_ws(p + 5);
        if (str_imatch(p, "TO")) p = skip_ws(p + 2);
        val = atoi(p);
        if (val == 0 || (val >= 1900 && val <= 2900))
            opts->epoch = val;
        else
            printf("EPOCH must be 0 or 1900-2900.\n");
        return;
    }
    if (str_imatch(p, "MARK")) {
        p = skip_ws(p + 4);
        if (str_imatch(p, "TO")) p = skip_ws(p + 2);
        if (*p)
            opts->mark = *p;
        else
            opts->mark = '\0';
        return;
    }
    if (str_imatch(p, "WRAP")) {
        val = parse_on_off(p + 4);
        if (val >= 0) opts->wrap = val;
        else printf("Syntax: SET WRAP ON/OFF\n");
        return;
    }
    if (str_imatch(p, "DEVICE")) {
        /* SET DEVICE TO SCREEN/PRINT */
        p = skip_ws(p + 6);
        if (str_imatch(p, "TO")) p = skip_ws(p + 2);
        if (str_imatch(p, "SCREEN"))
            opts->device = 0;
        else if (str_imatch(p, "PRINT"))
            opts->device = 1;
        return;
    }

    printf("Unknown SET option.\n");
}
