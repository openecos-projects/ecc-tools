#include "CppLibertyAST.hh"

#include <cstdlib>

namespace liberty {

LibValueList* LibSimpleAttribute::getAllValues()
{
    auto list = new LibValueList();
    if (_value) {
        if (_value->isFloat()) {
            list->push_back(std::make_unique<LibFloatValue>(_value->asFloat()));
        } else {
            list->push_back(std::make_unique<LibStringValue>(_value->asString()));
        }
    }
    return list;
}

double LibStringValue::asFloat() const
{
    return std::strtod(_val.c_str(), nullptr);
}

LibValue* LibComplexAttribute::getFirstValue()
{
    if (_values && !_values->empty()) {
        return (*_values)[0].get();
    }
    return nullptr;
}

LibGroup::LibGroup(const char* type, std::unique_ptr<LibValueList> params,
                           const char* filename, int line_no)
    : LibNode(filename, line_no), _type(type ? type : ""), _params(std::move(params))
{
}

const char* LibGroup::getFirstParamName()
{
    if (_params && !_params->empty()) {
        return (*_params)[0]->asString();
    }
    return nullptr;
}

const char* LibGroup::getSecondParamName()
{
    if (_params && _params->size() > 1) {
        return (*_params)[1]->asString();
    }
    return nullptr;
}

void LibGroup::addChildGroup(std::unique_ptr<LibGroup> subgroup)
{
    _statements.push_back(subgroup.get());
    _subgroups.push_back(std::move(subgroup));
}

void LibGroup::addAttribute(std::unique_ptr<LibAttribute> attr)
{
    _attr_map[attr->getName()] = attr.get();
    _statements.push_back(attr.get());
    _attrs.push_back(std::move(attr));
}

void LibGroup::addVariable(std::unique_ptr<LibVarDecl> var)
{
    _statements.push_back(var.get());
    _variables.push_back(std::move(var));
}

LibAttribute* LibGroup::findAttribute(const char* name)
{
    auto it = _attr_map.find(name);
    if (it != _attr_map.end()) {
        return it->second;
    }
    return nullptr;
}

}
