#ifndef SBASIC_ERROR_H
#define SBASIC_ERROR_H

typedef enum {
    ERR_NONE = 0,
    ERR_SYNTAX,
    ERR_TYPE_MISMATCH,
    ERR_UNDEFINED_VAR,
    ERR_DIVISION_BY_ZERO,
    ERR_OVERFLOW,
    ERR_OUT_OF_MEMORY,
    ERR_STRING_TOO_LONG,
    ERR_ILLEGAL_FUNCTION_CALL,
    ERR_NEXT_WITHOUT_FOR,
    ERR_FOR_WITHOUT_NEXT,
    ERR_WEND_WITHOUT_WHILE,
    ERR_WHILE_WITHOUT_WEND,
    ERR_EXPECTED_THEN,
    ERR_EXPECTED_END_IF,
    ERR_UNEXPECTED_TOKEN,
    ERR_UNTERMINATED_STRING,
    ERR_INPUT_PAST_END,
    ERR_BREAK,
    ERR_EXIT,
    /* Flow control (not real errors — used to unwind the call stack) */
    ERR_EXIT_FOR,
    ERR_EXIT_WHILE,
    ERR_EXIT_DO,
    ERR_EXIT_SUB,
    ERR_EXIT_FUNCTION,
    ERR_GOTO,
    ERR_GOSUB,
    ERR_RETURN,
    ERR_RETURN_WITHOUT_GOSUB,
    ERR_UNDEFINED_LABEL,
    ERR_UNDEFINED_PROC,
    ERR_DUPLICATE_LABEL,
    ERR_LOOP_WITHOUT_DO,
    ERR_DO_WITHOUT_LOOP,
    ERR_END_SELECT_EXPECTED,
    ERR_CONST_REASSIGN,
    ERR_SUBSCRIPT_OUT_OF_RANGE,
    ERR_DUPLICATE_DIM,
    ERR_OUT_OF_DATA,
    ERR_FILE_NOT_FOUND,
    ERR_FILE_ALREADY_OPEN,
    ERR_FILE_NOT_OPEN,
    ERR_BAD_FILE_NUMBER,
    ERR_UNDEFINED_TYPE,
    ERR_DUPLICATE_FIELD,
    ERR_UNDEFINED_FIELD,
    ERR_RESUME_WITHOUT_ERROR,
    ERR_ON_ERROR_GOTO,       /* flow control: error trapped */
    ERR_STACK_OVERFLOW,
    ERR_INTERNAL,
    ERR_COUNT
} error_t;

/* Return the message string for an error code */
const char *error_message(error_t err);

/* Print an error with line number context */
void error_print(error_t err, int line);

/* Macro to check eval result and propagate errors */
#define EVAL_CHECK(expr) do { \
    error_t _err = (expr); \
    if (_err != ERR_NONE) return _err; \
} while (0)

#endif
