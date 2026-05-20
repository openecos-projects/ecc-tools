#pragma once

#include <iostream>
#include <string>
#include "CppLibertyTokens.hh"

namespace liberty {

class LibertyDriver;

class LibertyScanner
{
public:
    LibertyScanner();
    virtual ~LibertyScanner();
    
    int yylex(YYSTYPE* yylval, YYLTYPE* yylloc);
    
    void ungetToken(int token, YYSTYPE& yylval);
    
    void setInput(std::istream* in) { _in = in; }
    void setOutput(std::ostream* out) { _out = out; }
    
private:
    std::istream* _in;
    std::ostream* _out;
    int _line_no;
    int _column;
    int _last_char;
    bool _has_last_char;
    
    bool _has_unget_token;
    int _unget_token;
    YYSTYPE _unget_yylval;
    
    int getChar();
    void ungetChar(int c);
    void skipWhitespace();
    void skipLineComment();
    bool skipBlockComment();
    bool readString(std::string& result);
    bool readNumber(std::string& result);
    bool isNumberStart(int c) const;
    bool readIdentifier(std::string& result);
};

}
