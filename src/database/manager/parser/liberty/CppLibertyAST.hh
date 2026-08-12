#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace liberty {

class LibValue
{
public:
    LibValue() = default;
    virtual ~LibValue() = default;

    virtual bool isString() const { return false; }
    virtual bool isFloat() const { return false; }
    virtual double asFloat() const { return 0.0; }
    virtual const char* asString() const { return ""; }
};

class LibFloatValue : public LibValue
{
public:
    explicit LibFloatValue(double val) : _val(val) {}
    ~LibFloatValue() override = default;

    bool isFloat() const override { return true; }
    double asFloat() const override { return _val; }

private:
    double _val;
};

class LibStringValue : public LibValue
{
public:
    explicit LibStringValue(const char* val) : _val(val ? val : "") {}
    ~LibStringValue() override = default;

    bool isString() const override { return true; }
    const char* asString() const override { return _val.c_str(); }
    double asFloat() const override;

private:
    std::string _val;
};

using LibValueList = std::vector<std::unique_ptr<LibValue>>;

class LibNode
{
public:
    LibNode(const char* filename, int line_no) 
        : _filename(filename ? filename : ""), _line_no(line_no) {}
    virtual ~LibNode() = default;

    virtual bool isGroup() const { return false; }
    virtual bool isSimpleAttr() const { return false; }
    virtual bool isComplexAttr() const { return false; }
    virtual bool isVariable() const { return false; }

    const char* getSourceFile() const { return _filename.c_str(); }
    int getSourceLine() const { return _line_no; }

protected:
    std::string _filename;
    int _line_no;
};

class LibAttribute : public LibNode
{
public:
    LibAttribute(const char* name, const char* filename, int line_no)
        : LibNode(filename, line_no), _name(name ? name : "") {}
    ~LibAttribute() override = default;

    const char* getName() const { return _name.c_str(); }
    virtual bool isSimple() const = 0;
    virtual bool isComplex() const = 0;
    virtual LibValue* getFirstValue() = 0;
    virtual LibValueList* getAllValues() = 0;

protected:
    std::string _name;
};

class LibSimpleAttribute : public LibAttribute
{
public:
    LibSimpleAttribute(const char* name, std::unique_ptr<LibValue> value,
                       const char* filename, int line_no)
        : LibAttribute(name, filename, line_no), _value(std::move(value)) {}
    ~LibSimpleAttribute() override = default;

    bool isSimpleAttr() const override { return true; }
    bool isSimple() const override { return true; }
    bool isComplex() const override { return false; }
    LibValue* getFirstValue() override { return _value.get(); }
    LibValueList* getAllValues() override;

private:
    std::unique_ptr<LibValue> _value;
};

class LibComplexAttribute : public LibAttribute
{
public:
    LibComplexAttribute(const char* name, std::unique_ptr<LibValueList> values,
                        const char* filename, int line_no)
        : LibAttribute(name, filename, line_no), _values(std::move(values)) {}
    ~LibComplexAttribute() override = default;

    bool isComplexAttr() const override { return true; }
    bool isSimple() const override { return false; }
    bool isComplex() const override { return true; }
    LibValue* getFirstValue() override;
    LibValueList* getAllValues() override { return _values.get(); }

private:
    std::unique_ptr<LibValueList> _values;
};

class LibVarDecl : public LibNode
{
public:
    LibVarDecl(const char* var_name, double value, const char* filename, int line_no)
        : LibNode(filename, line_no), _var_name(var_name ? var_name : ""), _value(value) {}
    ~LibVarDecl() override = default;

    bool isVariable() const override { return true; }
    const char* getVarName() const { return _var_name.c_str(); }
    double getValue() const { return _value; }

private:
    std::string _var_name;
    double _value;
};

class LibGroup : public LibNode
{
public:
    LibGroup(const char* type, std::unique_ptr<LibValueList> params,
             const char* filename, int line_no);
    ~LibGroup() override = default;

    bool isGroup() const override { return true; }

    const char* getGroupType() const { return _type.c_str(); }
    const char* getFirstParamName();
    const char* getSecondParamName();
    LibValueList* getParams() { return _params.get(); }

    void addChildGroup(std::unique_ptr<LibGroup> subgroup);
    void addAttribute(std::unique_ptr<LibAttribute> attr);
    void addVariable(std::unique_ptr<LibVarDecl> var);

    LibAttribute* findAttribute(const char* name);

    std::vector<std::unique_ptr<LibGroup>>& getChildGroups() { return _subgroups; }
    std::vector<std::unique_ptr<LibAttribute>>& getAttributes() { return _attrs; }
    std::vector<std::unique_ptr<LibVarDecl>>& getVariables() { return _variables; }
    std::vector<LibNode*>& getStatements() { return _statements; }

private:
    std::string _type;
    std::unique_ptr<LibValueList> _params;
    std::vector<std::unique_ptr<LibGroup>> _subgroups;
    std::vector<std::unique_ptr<LibAttribute>> _attrs;
    std::unordered_map<std::string, LibAttribute*> _attr_map;
    std::vector<std::unique_ptr<LibVarDecl>> _variables;
    std::vector<LibNode*> _statements;
};

} // namespace liberty
