#pragma once

#include <cstddef>
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
    
    void setInput(std::istream* in) {
        _in = in;
        _buffer_cursor = nullptr;
        _buffer_end = nullptr;
    }
    void setInputBuffer(const char* data, std::size_t size);
    void setOutput(std::ostream* out) { _out = out; }
    
private:
    std::istream* _in;
    std::ostream* _out;
    const char* _buffer_cursor;
    const char* _buffer_end;
    int _line_no;
    int _column;
    int _last_char;
    bool _has_last_char;
    
    bool _has_unget_token;
    int _unget_token;
    YYSTYPE _unget_yylval;
    
    int getChar();
    int peekChar() const;
    void ungetChar(int c);
    void skipWhitespace();
    void skipLineComment();
    bool skipBlockComment();
    bool readString(std::string& result);
    bool readNumber(std::string& result);
    char* readBufferedString();
    bool readBufferedNumber(int first_char, double& value);
    char* readBufferedIdentifier();
    char* duplicateSpan(const char* begin, const char* end) const;
    bool isNumberStart(int c) const;
    bool readIdentifier(std::string& result);
};

}
