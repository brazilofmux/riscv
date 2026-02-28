#include "error.h"
#include <stdio.h>

static const char *error_messages[ERR_COUNT] = {
    "No error",
    "Syntax error",
    "Type mismatch",
    "Undefined variable",
    "Division by zero",
    "Overflow",
    "Out of memory",
    "String too long",
    "Illegal function call",
    "NEXT without FOR",
    "FOR without NEXT",
    "WEND without WHILE",
    "WHILE without WEND",
    "Expected THEN",
    "Expected END IF",
    "Unexpected token",
    "Unterminated string",
    "Input past end",
    "Break",
    "Exit",
    "EXIT FOR",
    "EXIT WHILE",
    "EXIT DO",
    "EXIT SUB",
    "EXIT FUNCTION",
    "GOTO",
    "GOSUB",
    "RETURN",
    "RETURN without GOSUB",
    "Undefined label",
    "Undefined SUB/FUNCTION",
    "Duplicate label",
    "LOOP without DO",
    "DO without LOOP",
    "Expected END SELECT",
    "Cannot reassign CONST",
    "Subscript out of range",
    "Array already dimensioned",
    "Out of DATA",
    "File not found",
    "File already open",
    "File not open",
    "Bad file number",
    "Undefined TYPE",
    "Duplicate field name",
    "Undefined field",
    "RESUME without error",
    "ON ERROR GOTO",
    "Stack overflow",
    "Internal error",
};

const char *error_message(error_t err) {
    if (err >= 0 && err < ERR_COUNT)
        return error_messages[err];
    return "Unknown error";
}

void error_print(error_t err, int line) {
    if (err == ERR_NONE || err == ERR_EXIT ||
        (err >= ERR_EXIT_FOR && err <= ERR_RETURN))
        return;
    if (line > 0)
        printf("Error in line %d: %s\n", line, error_message(err));
    else
        printf("Error: %s\n", error_message(err));
}
