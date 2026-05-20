#include "CppLibertyScanner.hh"
#include "CppLibertyDriver.hh"
#include <cctype>
#include <cstdlib>
#include <iostream>

namespace liberty {

LibertyScanner::LibertyScanner()
    : _in(nullptr), _out(nullptr), 
      _line_no(1), _column(1), _last_char(0), _has_last_char(false),
      _has_unget_token(false), _unget_token(0)
{
}

LibertyScanner::~LibertyScanner()
{
}

int LibertyScanner::getChar()
{
    if (_has_last_char) {
        _has_last_char = false;
        return _last_char;
    }
    
    if (!_in || _in->eof()) {
        return EOF;
    }
    
    int c = _in->get();
    if (c == '\n') {
        _line_no++;
        _column = 1;
    } else if (c != EOF) {
        _column++;
    }
    return c;
}

void LibertyScanner::ungetChar(int c)
{
    _last_char = c;
    _has_last_char = true;
    if (c == '\n') {
        _line_no--;
    }
}

void LibertyScanner::skipWhitespace()
{
    int c;
    while ((c = getChar()) != EOF) {
        if (!std::isspace(c)) {
            ungetChar(c);
            break;
        }
    }
}

void LibertyScanner::skipLineComment()
{
    int c;
    while ((c = getChar()) != EOF && c != '\n') {
    }
}

bool LibertyScanner::skipBlockComment()
{
    int c;
    bool last_was_star = false;
    while ((c = getChar()) != EOF) {
        if (c == '/') {
            if (last_was_star) {
                return true;
            }
        }
        last_was_star = (c == '*');
    }
    return false;
}

bool LibertyScanner::readString(std::string& result)
{
    result.clear();
    int c;
    while ((c = getChar()) != EOF) {
        if (c == '"') {
            return true;
        } else if (c == '\\') {
            c = getChar();
            if (c == EOF) return false;
            if (c == '\n') continue;
            result += static_cast<char>(c);
        } else if (c == '\n') {
            return false;
        } else {
            result += static_cast<char>(c);
        }
    }
    return false;
}

bool LibertyScanner::readNumber(std::string& result)
{
    result.clear();
    int c;
    
    c = getChar();
    if (c == '+' || c == '-') {
        result += static_cast<char>(c);
        c = getChar();
    }
    
    while (c != EOF && std::isdigit(c)) {
        result += static_cast<char>(c);
        c = getChar();
    }
    
    if (c == '.' || c == '_') {
        result += (c == '_') ? '.' : static_cast<char>(c);
        c = getChar();
        while (c != EOF && std::isdigit(c)) {
            result += static_cast<char>(c);
            c = getChar();
        }
    }
    
    if (c == 'k' || c == 'K') {
        result += 'e';
        result += '3';
        c = getChar();
    } else if (c == 'e' || c == 'E') {
        result += static_cast<char>(c);
        c = getChar();
        if (c == '+' || c == '-') {
            result += static_cast<char>(c);
            c = getChar();
        }
        while (c != EOF && std::isdigit(c)) {
            result += static_cast<char>(c);
            c = getChar();
        }
    }
    
    ungetChar(c);
    return !result.empty() && result != "+" && result != "-" && result != "." &&
           result != "+." && result != "-.";
}

bool LibertyScanner::isNumberStart(int c) const
{
    if (std::isdigit(c)) {
        return true;
    }
    if (!_in) {
        return false;
    }
    int next = _in->peek();
    if (c == '.') {
        return std::isdigit(next);
    }
    if (c == '+' || c == '-') {
        return std::isdigit(next) || next == '.' || next == '_';
    }
    return false;
}

bool LibertyScanner::readIdentifier(std::string& result)
{
    result.clear();
    int c;
    int bracket_depth = 0;
    
    while ((c = getChar()) != EOF) {
        if (c == '[') {
            ++bracket_depth;
            result += static_cast<char>(c);
        } else if (c == ']') {
            if (bracket_depth > 0) {
                --bracket_depth;
            }
            result += static_cast<char>(c);
        } else if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || (c == ':' && bracket_depth > 0)) {
            result += static_cast<char>(c);
        } else {
            ungetChar(c);
            break;
        }
    }
    
    return !result.empty();
}

void LibertyScanner::ungetToken(int token, YYSTYPE& yylval)
{
    _has_unget_token = true;
    _unget_token = token;
    _unget_yylval = yylval;
}

int LibertyScanner::yylex(YYSTYPE* yylval, YYLTYPE* yylloc)
{
    if (_has_unget_token) {
        _has_unget_token = false;
        *yylval = _unget_yylval;
        yylloc->first_line = _line_no;
        yylloc->first_column = _column;
        yylloc->last_line = _line_no;
        yylloc->last_column = _column;
        return _unget_token;
    }

    while (true) {
        skipWhitespace();
        
        int c = getChar();
        if (c == EOF) {
            return 0;
        }
        
        yylloc->first_line = _line_no;
        yylloc->first_column = _column - 1;
        yylloc->last_line = _line_no;
        yylloc->last_column = _column;
        
        if (c == '\\') {
            int next = getChar();
            if (next == '\n') {
                continue;
            }
            ungetChar(next);
            return c;
        }
        
        if (c == '/') {
            c = getChar();
            if (c == '/') {
                skipLineComment();
                continue;
            } else if (c == '*') {
                if (!skipBlockComment()) {
                    std::cerr << "Error: Unterminated block comment at line " << yylloc->first_line << std::endl;
                    return 0;
                }
                continue;
            } else {
                ungetChar(c);
                return '/';
            }
        }
        
        if (c == '"') {
            std::string str;
            if (readString(str)) {
                yylval->string = strdup(str.c_str());
                return STRING;
            }
            return 0;
        }
        
        if (isNumberStart(c)) {
            ungetChar(c);
            std::string num;
            if (readNumber(num)) {
                yylval->number = std::strtod(num.c_str(), nullptr);
                return FLOAT;
            }
            return c;
        }
        
        if (std::isalpha(c) || c == '_') {
            ungetChar(c);
            std::string id;
            if (readIdentifier(id)) {
                yylval->string = strdup(id.c_str());
                return KEYWORD;
            }
            return c;
        }
        
        switch (c) {
            case '(':
            case ')':
            case '{':
            case '}':
            case ':':
            case ';':
            case ',':
            case '+':
            case '-':
            case '*':
            case '&':
            case '|':
            case '^':
            case '!':
            case '=':
            case '\'':
                return c;
            default:
                std::cerr << "Debug: Unknown character '" << (char)c << "' (code " << c << ") at line " << _line_no << std::endl;
                return c;
        }
    }
}

}
