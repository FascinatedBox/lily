#ifndef LILY_KEYWORD_TABLE_H
# define LILY_KEYWORD_TABLE_H

typedef struct {
	const char *name;
	uint64_t shorthash;
} keyword_entry;

keyword_entry keywords[] = {
    {"if",           26217},
    {"do",           28516},
    {"var",          7496054},
    {"for",          7499622},
    {"try",          7959156},
    {"case",         1702060387},
    {"else",         1702063205},
    {"true",         1702195828},
    {"elif",         1718185061},
    {"self",         1718379891},
    {"enum",         1836412517},
    {"while",        435610544247},
    {"raise",        435727982962},
    {"false",        435728179558},
    {"match",        448345170285},
    {"break",        461195539042},
    {"class",        495857003619},
    {"define",       111524889126244},
    {"return",       121437875889522},
    {"except",       128026086176869},
    {"import",       128034844732777},
    {"private",      28556934595048048},
    {"__file__",     6872323072689856351},
    {"__line__",     6872323081280184159},
    {"protected",    7310577382525465200},
    {"continue",     7310870969309884259},
    {"__function__", 7598807797348065119},
};

# define KEY_IF            0
# define KEY_DO            1
# define KEY_VAR           2
# define KEY_FOR           3
# define KEY_TRY           4
# define KEY_CASE          5
# define KEY_ELSE          6
# define KEY_TRUE          7
# define KEY_ELIF          8
# define KEY_SELF          9
# define KEY_ENUM         10
# define KEY_WHILE        11
# define KEY_RAISE        12
# define KEY_FALSE        13
# define KEY_MATCH        14
# define KEY_BREAK        15
# define KEY_CLASS        16
# define KEY_DEFINE       17
# define KEY_RETURN       18
# define KEY_EXCEPT       19
# define KEY_IMPORT       20
# define KEY_PRIVATE      21
# define KEY__FILE__      22
# define KEY__LINE__      23
# define KEY_PROTECTED    24
# define KEY_CONTINUE     25
# define KEY__FUNCTION__  26
# define KEY_LAST_ID      26

#endif
