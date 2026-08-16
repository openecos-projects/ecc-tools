#pragma once

#include <cstring>
#include <string>
#include "CppLibertyAST.hh"

namespace liberty {

struct YYLTYPE {
    int first_line;
    int first_column;
    int last_line;
    int last_column;
    const std::string* filename;
};

struct YYSTYPE {
    char* string;
    double number;
    char ch;
    LibValue* attr_value;
    LibValueList* attr_values;
    LibGroup* group;
    LibNode* node;
};

enum LibertyTokenType {
    FLOAT = 258,
    STRING = 259,
    KEYWORD = 260
};

#define YYLTYPE_IS_TRIVIAL 0
#define YYLLOC_DEFAULT(Current, Rhs, N) \
    do { \
        if (N) { \
            (Current).first_line = YYRHSLOC(Rhs, 1).first_line; \
            (Current).first_column = YYRHSLOC(Rhs, 1).first_column; \
            (Current).last_line = YYRHSLOC(Rhs, N).last_line; \
            (Current).last_column = YYRHSLOC(Rhs, N).last_column; \
            (Current).filename = YYRHSLOC(Rhs, 1).filename; \
        } else { \
            (Current).first_line = (Current).last_line = YYRHSLOC(Rhs, 0).last_line; \
            (Current).first_column = (Current).last_column = YYRHSLOC(Rhs, 0).last_column; \
            (Current).filename = YYRHSLOC(Rhs, 0).filename; \
        } \
    } while (0)

}
