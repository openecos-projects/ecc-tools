#include "CppLibertyDriver.hh"
#include "CppLibertyScanner.hh"
#include <cstdarg>
#include <cstdlib>
#include <sstream>

namespace liberty {
namespace {

std::string tokenToString(int token, YYSTYPE& yylval)
{
    if (token == FLOAT) {
        std::ostringstream os;
        os.precision(17);
        os << yylval.number;
        return os.str();
    }

    if (token == STRING || token == KEYWORD) {
        std::string value = yylval.string ? yylval.string : "";
        free((void*)yylval.string);
        return value;
    }

    if (token > 0 && token < 256) {
        return std::string(1, static_cast<char>(token));
    }

    return "";
}

bool isValueToken(int token)
{
    return token == FLOAT || token == STRING || token == KEYWORD;
}

std::unique_ptr<LibValue> makeValueFromToken(int token, YYSTYPE& yylval)
{
    if (token == FLOAT) {
        return std::make_unique<LibFloatValue>(yylval.number);
    }

    if (token == STRING || token == KEYWORD) {
        auto value = std::make_unique<LibStringValue>(yylval.string);
        free((void*)yylval.string);
        return value;
    }

    return nullptr;
}

std::string trimString(const std::string& str)
{
    const auto begin = str.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(begin, end - begin + 1);
}

}  // namespace

LibertyDriver::LibertyDriver()
    : _result(nullptr)
{
}

LibertyDriver::~LibertyDriver()
{
}

void LibertyDriver::reportError(const YYLTYPE& loc, const std::string& msg)
{
    std::cerr << *loc.filename << ":" << loc.first_line << ":" << loc.first_column << ": " << msg << std::endl;
}

void LibertyDriver::reportError(const std::string& msg)
{
    std::cerr << _filename << ": " << msg << std::endl;
}

void LibertyDriver::setParseResult(LibNode* node)
{
    if (node && node->isGroup()) {
        _result = static_cast<LibGroup*>(node);
    }
}

bool LibertyDriver::parse(const char* filename)
{
    _filename = filename;
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs) {
        reportError("cannot open file");
        return false;
    }

    ifs.seekg(0, std::ios::end);
    const auto file_size = ifs.tellg();
    if (file_size < 0) {
        reportError("cannot get file size");
        return false;
    }
    ifs.seekg(0, std::ios::beg);

    std::string buffer(static_cast<std::size_t>(file_size), '\0');
    if (!buffer.empty()) {
        ifs.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (ifs.gcount() != static_cast<std::streamsize>(buffer.size())) {
            reportError("cannot read file");
            return false;
        }
    }
    LibertyScanner scanner;
    scanner.setInputBuffer(buffer.data(), buffer.size());
    return parseScanner(scanner);
}

bool LibertyDriver::parse(std::istream& is, const char* filename)
{
    _filename = filename;
    
    LibertyScanner scanner;
    scanner.setInput(&is);
    return parseScanner(scanner);
}

bool LibertyDriver::parseScanner(LibertyScanner& scanner)
{
    _result = nullptr;
    
    YYSTYPE yylval;
    YYLTYPE yylloc;
    yylloc.filename = &_filename;
    
    int token = scanner.yylex(&yylval, &yylloc);
    if (token == 0) {
        reportError("empty file");
        return false;
    }
    
    if (token != KEYWORD && token != STRING) {
        reportError(yylloc, "expected group keyword");
        return false;
    }
    
    return parseGroupContent(scanner, token, yylval, yylloc);
}

bool LibertyDriver::parseGroupContent(LibertyScanner& scanner, int first_token, YYSTYPE& first_yylval, YYLTYPE& first_yylloc)
{
    const char* group_type = first_yylval.string;
    int line = first_yylloc.first_line;
    
    YYSTYPE yylval;
    YYLTYPE yylloc;
    yylloc.filename = &_filename;
    
    int token = scanner.yylex(&yylval, &yylloc);
    if (token != '(') {
        reportError(yylloc, "expected '(' after group type");
        free((void*)group_type);
        return false;
    }
    
    auto params = std::make_unique<LibValueList>();
    
    token = scanner.yylex(&yylval, &yylloc);
    if (token != ')') {
        while (true) {
            std::unique_ptr<LibValue> value = makeValueFromToken(token, yylval);
            if (!value) {
                reportError(yylloc, "expected attribute value in group parameters");
                free((void*)group_type);
                return false;
            }
            params->push_back(std::move(value));
            
            token = scanner.yylex(&yylval, &yylloc);
            if (token == ')') {
                break;
            }
            if (token == ',') {
                token = scanner.yylex(&yylval, &yylloc);
                continue;
            }
            if (isValueToken(token)) {
                continue;
            }
            {
                reportError(yylloc, "expected ',' or ')' in group parameters");
                free((void*)group_type);
                return false;
            }
        }
    }
    
    token = scanner.yylex(&yylval, &yylloc);
    if (token != '{') {
        reportError(yylloc, "expected '{' after group parameters");
        free((void*)group_type);
        return false;
    }
    
    beginGroup(group_type, params.release(), line, _filename.c_str());
    free((void*)group_type);
    
    if (!parseGroupBody(scanner)) {
        return false;
    }
    
    LibGroup* group = endGroup();
    setParseResult(group);
    
    token = scanner.yylex(&yylval, &yylloc);
    
    return true;
}

bool LibertyDriver::parseGroupBody(LibertyScanner& scanner)
{
    YYSTYPE yylval;
    YYLTYPE yylloc;
    yylloc.filename = &_filename;
    
    while (true) {
        int token = scanner.yylex(&yylval, &yylloc);
        if (token == 0 || token == '}') {
            return true;
        }
        
        if (token == KEYWORD || token == STRING) {
            if (!parseGroupMember(scanner, token, yylval, yylloc)) {
                return false;
            }
        } else {
            std::cerr << "Debug: Unexpected token " << token << " at line " << yylloc.first_line;
            if (token > 0 && token < 256) {
                std::cerr << " (char: '" << (char)token << "')";
            }
            std::cerr << std::endl;
            reportError(yylloc, "unexpected token in group body");
            return false;
        }
    }
    
    return true;
}

bool LibertyDriver::parseGroupMember(LibertyScanner& scanner, int first_token, YYSTYPE& first_yylval, YYLTYPE& first_yylloc)
{
    const char* name = first_yylval.string;
    int line = first_yylloc.first_line;
    
    YYSTYPE yylval;
    YYLTYPE yylloc;
    yylloc.filename = &_filename;
    
    int token = scanner.yylex(&yylval, &yylloc);
    
    if (token == ':') {
        std::unique_ptr<LibValue> value(parseValue(scanner));
        if (!value) {
            free((void*)name);
            return false;
        }
        
        createSimpleAttr(name, value.release(), line, _filename.c_str());
        free((void*)name);
        
        token = scanner.yylex(&yylval, &yylloc);
        if (token != ';') {
            scanner.ungetToken(token, yylval);
        }
        
        return true;
    }
    
    if (token == '(') {
        auto values = std::make_unique<LibValueList>();
        
        token = scanner.yylex(&yylval, &yylloc);
        
        if (token != ')') {
            while (true) {
                std::unique_ptr<LibValue> value = makeValueFromToken(token, yylval);
                if (!value) {
                    reportError(yylloc, "expected attribute value in complex attribute");
                    free((void*)name);
                    return false;
                }
                values->push_back(std::move(value));
                
                token = scanner.yylex(&yylval, &yylloc);
                if (token == ')') {
                    break;
                }
                if (token == ',') {
                    token = scanner.yylex(&yylval, &yylloc);
                    continue;
                }
                if (isValueToken(token)) {
                    continue;
                }
                {
                    reportError(yylloc, "expected ',' or ')' in complex attribute");
                    free((void*)name);
                    return false;
                }
            }
        }
        
        token = scanner.yylex(&yylval, &yylloc);
        if (token == '{') {
            beginGroup(name, values.release(), line, _filename.c_str());
            free((void*)name);
            if (!parseGroupBody(scanner)) {
                return false;
            }
            endGroup();
        } else {
            createComplexAttr(name, values.release(), line, _filename.c_str());
            free((void*)name);
            if (token != ';') {
                scanner.ungetToken(token, yylval);
            }
        }
        
        return true;
    }
    
    if (token == '=') {
        token = scanner.yylex(&yylval, &yylloc);
        if (token != FLOAT) {
            reportError(yylloc, "expected float value after '='");
            free((void*)name);
            return false;
        }
        
        createVarDecl(name, yylval.number, line, _filename.c_str());
        free((void*)name);
        
        token = scanner.yylex(&yylval, &yylloc);
        if (token != ';') {
            scanner.ungetToken(token, yylval);
        }
        
        return true;
    }
    
    reportError(yylloc, "expected ':', '(', or '=' after identifier");
    free((void*)name);
    return false;
}

LibValue* LibertyDriver::parseValue(LibertyScanner& scanner)
{
    YYSTYPE yylval;
    YYLTYPE yylloc;
    yylloc.filename = &_filename;
    
    int token = scanner.yylex(&yylval, &yylloc);

    if (token == 0 || token == ';' || token == '}') {
        if (token == '}') {
            scanner.ungetToken(token, yylval);
        }
        return nullptr;
    }

    if (token == FLOAT) {
        const double first_value = yylval.number;
        token = scanner.yylex(&yylval, &yylloc);
        if (token == ';') {
            return new LibFloatValue(first_value);
        }

        std::ostringstream value;
        value.precision(17);
        value << first_value;
        if (token != 0 && token != '}') {
            value << tokenToString(token, yylval);
        }
        while ((token = scanner.yylex(&yylval, &yylloc)) != 0) {
            if (token == ';') {
                break;
            }
            if (token == '}') {
                scanner.ungetToken(token, yylval);
                break;
            }
            value << tokenToString(token, yylval);
        }
        return new LibStringValue(trimString(value.str()).c_str());
    }

    std::string value = tokenToString(token, yylval);
    while ((token = scanner.yylex(&yylval, &yylloc)) != 0) {
        if (token == ';') {
            break;
        }
        if (token == '}') {
            scanner.ungetToken(token, yylval);
            break;
        }
        value += tokenToString(token, yylval);
    }

    return new LibStringValue(trimString(value).c_str());
}

void LibertyDriver::beginGroup(const char* type, LibValueList* params, int line, const char* filename)
{
    auto group = std::make_unique<LibGroup>(type, std::unique_ptr<LibValueList>(params), filename, line);
    _group_stack.push(group.release());
}

LibGroup* LibertyDriver::endGroup()
{
    if (_group_stack.empty()) {
        return nullptr;
    }
    
    LibGroup* group = _group_stack.top();
    _group_stack.pop();
    
    if (!_group_stack.empty()) {
        _group_stack.top()->addChildGroup(std::unique_ptr<LibGroup>(group));
    }
    
    return group;
}

LibNode* LibertyDriver::createSimpleAttr(const char* name, LibValue* value, int line, const char* filename)
{
    auto attr = std::make_unique<LibSimpleAttribute>(name, std::unique_ptr<LibValue>(value), filename, line);
    if (!_group_stack.empty()) {
        _group_stack.top()->addAttribute(std::move(attr));
    }
    return nullptr;
}

LibNode* LibertyDriver::createComplexAttr(const char* name, LibValueList* values, int line, const char* filename)
{
    auto attr = std::make_unique<LibComplexAttribute>(name, std::unique_ptr<LibValueList>(values), filename, line);
    if (!_group_stack.empty()) {
        _group_stack.top()->addAttribute(std::move(attr));
    }
    return nullptr;
}

LibNode* LibertyDriver::createVarDecl(const char* var, double value, int line, const char* filename)
{
    auto variable = std::make_unique<LibVarDecl>(var, value, filename, line);
    if (!_group_stack.empty()) {
        _group_stack.top()->addVariable(std::move(variable));
    }
    return nullptr;
}

}
