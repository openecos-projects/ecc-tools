#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <stack>
#include <string>
#include <vector>

#include "CppLibertyAST.hh"
#include "CppLibertyTokens.hh"

namespace liberty {

class LibertyScanner;

class LibertyDriver
{
public:
    LibertyDriver();
    ~LibertyDriver();

    bool parse(const char* filename);
    bool parse(std::istream& is, const char* filename = "<stream>");

    void reportError(const YYLTYPE& loc, const std::string& msg);
    void reportError(const std::string& msg);

    void setParseResult(LibNode* node);
    LibGroup* getParseResult() { return _result; }

    void beginGroup(const char* type, LibValueList* params, int line, const char* filename);
    LibGroup* endGroup();

    LibNode* createSimpleAttr(const char* name, LibValue* value, int line, const char* filename);
    LibNode* createComplexAttr(const char* name, LibValueList* values, int line, const char* filename);
    LibNode* createVarDecl(const char* var, double value, int line, const char* filename);

    const std::string& getCurrentFile() const { return _filename; }

private:
    std::string _filename;
    LibGroup* _result;
    std::stack<LibGroup*> _group_stack;
    std::vector<std::string> _strings;
    
    bool parseScanner(LibertyScanner& scanner);
    bool parseGroupContent(LibertyScanner& scanner, int first_token, YYSTYPE& first_yylval, YYLTYPE& first_yylloc);
    bool parseGroupBody(LibertyScanner& scanner);
    bool parseGroupMember(LibertyScanner& scanner, int first_token, YYSTYPE& first_yylval, YYLTYPE& first_yylloc);
    LibValue* parseValue(LibertyScanner& scanner);
};

}
