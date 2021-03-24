#ifndef LILY_LEXER_DATA_H
# define LILY_LEXER_DATA_H

/* Generated by scripts/token.lily. */

# define CC_AT 63
# define CC_B 64
# define CC_CASH 65
# define CC_DIGIT 66
# define CC_NEWLINE 67
# define CC_QUESTION 68
# define CC_SHARP 69

static const uint8_t ch_table[256] = {
    58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 67, 58, 58, 58, 58, 58,
    58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58,
    58,  9, 40, 69, 65, 11, 48, 42, 32,  0, 13, 17,  1, 20, 47, 15,
    66, 66, 66, 66, 66, 66, 66, 66, 66, 66,  5, 58, 22, 30, 26, 68,
    63, 38, 64, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
    38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38,  4, 58, 36,  7, 38,
    58, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
    38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38,  2, 51,  3,  6, 58,
    58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58,
    58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58,
    58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58,
    58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58,
    58, 58, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
    38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
    38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38, 38,
    38, 38, 38, 38, 38, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58, 58,
};

# define IS_IDENT_START(x) (ident_table[x] == 1)

static const uint8_t ident_table[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const char *token_name_table[62] = {
    ")", ",", "{", "}", "[", ":", "~", "^", "^=", "!", "!=", "%", "%=", "*",
    "*=", "/", "/=", "+", "++", "+=", "-", "-=", "<", "<=", "<<", "<<=", ">",
    ">=", ">>", ">>=", "=", "==", "(", "a lambda", "<[", "]>", "]", "=>",
    "a label", "a property name", "a string", "a bytestring", "a byte",
    "an integer", "a double", "a docblock", "a named argument", ".", "&", "&=",
    "&&", "|", "|=", "||", "@(", "...", "|>", "$1", "invalid token",
    "end of lambda", "?>", "end of file",
};

static const uint8_t priority_table[62] = {
    0, 0, 0, 0, 0, 0, 0, 7, 1, 0, 4, 10, 1, 10, 1, 10,
    1, 9, 5, 1, 9, 1, 4, 4, 8, 1, 4, 4, 8, 1, 1, 4,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    7, 1, 3, 7, 1, 2, 0, 0, 6, 0, 0, 0, 0, 0,
};

#endif
