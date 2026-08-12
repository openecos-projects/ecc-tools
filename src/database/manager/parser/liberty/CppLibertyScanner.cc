#include "CppLibertyScanner.hh"
#include "CppLibertyDriver.hh"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace liberty {

LibertyScanner::LibertyScanner()
    : _in(nullptr), _out(nullptr), 
      _buffer_cursor(nullptr), _buffer_end(nullptr),
      _line_no(1), _column(1), _last_char(0), _has_last_char(false),
      _has_unget_token(false), _unget_token(0)
{
}

LibertyScanner::~LibertyScanner()
{
}

void LibertyScanner::setInputBuffer(const char* data, std::size_t size)
{
    _in = nullptr;
    _buffer_cursor = data;
    _buffer_end = data ? data + size : nullptr;
}

int LibertyScanner::getChar()
{
    if (_has_last_char) {
        _has_last_char = false;
        return _last_char;
    }

    if (_buffer_cursor) {
        if (_buffer_cursor >= _buffer_end) {
            return EOF;
        }

        const unsigned char c = static_cast<unsigned char>(*_buffer_cursor++);
        if (c == '\n') {
            _line_no++;
            _column = 1;
        } else {
            _column++;
        }
        return c;
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

int LibertyScanner::peekChar() const
{
    if (_has_last_char) {
        return _last_char;
    }

    if (_buffer_cursor) {
        if (_buffer_cursor >= _buffer_end) {
            return EOF;
        }
        return static_cast<unsigned char>(*_buffer_cursor);
    }

    return _in ? _in->peek() : EOF;
}

void LibertyScanner::ungetChar(int c)
{
    if (c == EOF) {
        return;
    }
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

char* LibertyScanner::duplicateSpan(const char* begin, const char* end) const
{
    const auto size = static_cast<std::size_t>(end - begin);
    auto* result = static_cast<char*>(std::malloc(size + 1));
    if (!result) {
        return nullptr;
    }
    std::memcpy(result, begin, size);
    result[size] = '\0';
    return result;
}

char* LibertyScanner::readBufferedString()
{
    if (!_buffer_cursor || _has_last_char) {
        return nullptr;
    }

    const char* const begin = _buffer_cursor;
    const char* cursor = _buffer_cursor;
    while (cursor < _buffer_end) {
        const char c = *cursor;
        if (c == '"') {
            auto* result = duplicateSpan(begin, cursor);
            if (!result) {
                return nullptr;
            }
            _column += static_cast<int>(cursor - begin) + 1;
            _buffer_cursor = cursor + 1;
            return result;
        }
        if (c == '\\' || c == '\n') {
            return nullptr;
        }
        ++cursor;
    }

    return nullptr;
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

bool LibertyScanner::readBufferedNumber(int first_char, double& value)
{
    if (!_buffer_cursor || _has_last_char) {
        return false;
    }

    char small_buffer[128];
    std::string large_buffer;
    std::size_t small_size = 0;

    auto append = [&](char c) {
        if (large_buffer.empty() && small_size < sizeof(small_buffer) - 1) {
            small_buffer[small_size++] = c;
        } else {
            if (large_buffer.empty()) {
                large_buffer.assign(small_buffer, small_size);
            }
            large_buffer += c;
        }
    };

    const char* cursor = _buffer_cursor;
    auto has_char = [&]() { return cursor < _buffer_end; };
    auto peek = [&]() -> int {
        return has_char() ? static_cast<unsigned char>(*cursor) : EOF;
    };
    auto take = [&]() -> int {
        return has_char() ? static_cast<unsigned char>(*(cursor++)) : EOF;
    };

    int c = first_char;
    append(static_cast<char>(c == '_' ? '.' : c));

    if (c == '+' || c == '-') {
        c = take();
        if (c == EOF) {
            return false;
        }
        append(static_cast<char>(c == '_' ? '.' : c));
    }

    while (std::isdigit(peek())) {
        append(static_cast<char>(take()));
    }

    if (peek() == '.' || peek() == '_') {
        append('.');
        take();
        while (std::isdigit(peek())) {
            append(static_cast<char>(take()));
        }
    }

    if (peek() == 'k' || peek() == 'K') {
        append('e');
        append('3');
        take();
    } else if (peek() == 'e' || peek() == 'E') {
        append(static_cast<char>(take()));
        if (peek() == '+' || peek() == '-') {
            append(static_cast<char>(take()));
        }
        while (std::isdigit(peek())) {
            append(static_cast<char>(take()));
        }
    }

    const char* number_string = nullptr;
    if (large_buffer.empty()) {
        small_buffer[small_size] = '\0';
        number_string = small_buffer;
    } else {
        number_string = large_buffer.c_str();
    }

    if (std::strcmp(number_string, "+") == 0 ||
        std::strcmp(number_string, "-") == 0 ||
        std::strcmp(number_string, ".") == 0 ||
        std::strcmp(number_string, "+.") == 0 ||
        std::strcmp(number_string, "-.") == 0) {
        return false;
    }

    value = std::strtod(number_string, nullptr);
    _column += static_cast<int>(cursor - _buffer_cursor);
    _buffer_cursor = cursor;
    return true;
}

bool LibertyScanner::isNumberStart(int c) const
{
    if (std::isdigit(c)) {
        return true;
    }
    if (!_in && !_buffer_cursor && !_has_last_char) {
        return false;
    }
    int next = peekChar();
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

char* LibertyScanner::readBufferedIdentifier()
{
    if (!_buffer_cursor || _has_last_char) {
        return nullptr;
    }

    const char* const begin = _buffer_cursor - 1;
    const char* cursor = _buffer_cursor;
    int bracket_depth = 0;

    while (cursor < _buffer_end) {
        const unsigned char c = static_cast<unsigned char>(*cursor);
        if (c == '[') {
            ++bracket_depth;
        } else if (c == ']') {
            if (bracket_depth > 0) {
                --bracket_depth;
            }
        } else if (!(std::isalnum(c) || c == '_' || c == '.' || c == '-' ||
                     (c == ':' && bracket_depth > 0))) {
            break;
        }
        ++cursor;
    }

    auto* result = duplicateSpan(begin, cursor);
    if (!result) {
        return nullptr;
    }
    _column += static_cast<int>(cursor - _buffer_cursor);
    _buffer_cursor = cursor;
    return result;
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
            if (_buffer_cursor && !_has_last_char) {
                if (char* str = readBufferedString()) {
                    yylval->string = str;
                    return STRING;
                }
            }
            std::string str;
            if (readString(str)) {
                yylval->string = strdup(str.c_str());
                return STRING;
            }
            return 0;
        }
        
        if (isNumberStart(c)) {
            if (_buffer_cursor && !_has_last_char) {
                if (readBufferedNumber(c, yylval->number)) {
                    return FLOAT;
                }
            }
            ungetChar(c);
            std::string num;
            if (readNumber(num)) {
                yylval->number = std::strtod(num.c_str(), nullptr);
                return FLOAT;
            }
            return c;
        }
        
        if (std::isalpha(c) || c == '_') {
            if (_buffer_cursor && !_has_last_char) {
                if (char* id = readBufferedIdentifier()) {
                    yylval->string = id;
                    return KEYWORD;
                }
            }
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
