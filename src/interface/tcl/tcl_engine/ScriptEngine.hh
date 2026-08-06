// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file ScriptEnginer.h
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The file is the class of the script engine based on tcl.
 * @version 0.1
 * @date 2020-11-18
 */

#pragma once

#if __has_include(<tcl8.6/tcl.h>)
  #include <tcl8.6/tcl.h>
#else
  #include <tcl.h>
#endif

#include <cassert>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "utility/logger/Logger.hpp"

namespace ecc {

bool matchWildcardWithtarget(const char* const pattern, const char* const target);
bool containWildcard(const char* pattern);
/**
 * @brief The ScriptEngine is used for tcl file process such as sdc file.
 *
 */
class ScriptEngine
{
 public:
  static ScriptEngine* getOrCreateInstance();
  static void destroyInstance();

  Tcl_Interp* get_interp() { return _interp; }

  Tcl_Command createCmd(const char* cmd_name, Tcl_ObjCmdProc* proc, void* cmd_data = nullptr, Tcl_CmdDeleteProc* delete_proc = nullptr);

  int evalScriptFile(const char* file_name);
  int evalString(const char* cmd_str);

  const char* getTclFileName();
  unsigned getTclLineNo();

  void setResult(char* result);
  void appendResult(char* result);
  const char* getResult();

 private:
  ScriptEngine();
  virtual ~ScriptEngine();

  ScriptEngine(const ScriptEngine&) = delete;
  ScriptEngine& operator=(const ScriptEngine&) = delete;

  static ScriptEngine* _instance;  //!< The singleton instance.
  Tcl_Interp* _interp;             //!< The tcl interpreter.
};

/**
 * @brief The tcl option base class.
 *
 */
class TclOption
{
 public:
  TclOption(const char* option_name, unsigned is_arg);
  virtual ~TclOption();

  const char* get_option_name() const { return _option_name.c_str(); }
  unsigned is_arg() const { return _is_arg; }

  virtual unsigned isSwitchOption() { return 0; }
  virtual unsigned isDoubleOption() { return 0; }
  virtual unsigned isStringOption() { return 0; }
  virtual unsigned isIntOption() { return 0; }

  virtual unsigned isDoubleListOption() { return 0; }
  virtual unsigned isStringListOption() { return 0; }
  virtual unsigned isIntListOption() { return 0; }

  virtual unsigned isStringListListOption() { return 0; }
  virtual unsigned isStringListListListOption() { return 0; }
  virtual unsigned isStringListListListListOption() { return 0; }

  virtual double getDoubleVal()
  {
    ECCLOG.error(Loc::current(), "The option do not has float val.");
    return 0.0;
  }

  virtual double getDefaultDoubleVal()
  {
    ECCLOG.error(Loc::current(), "The option do not has float val.");
    return 0.0;
  }

  virtual const char* getStringVal()
  {
    ECCLOG.error(Loc::current(), "The option do not has string val");
    return nullptr;
  }

  virtual const char* getDefaultStringVal()
  {
    ECCLOG.error(Loc::current(), "The option do not has string val.");
    return nullptr;
  }

  virtual bool getSwitchVal()
  {
    ECCLOG.error(Loc::current(), "The option do not has switch val.");
    return 0;
  }

  virtual int getIntVal()
  {
    ECCLOG.error(Loc::current(), "The option do not has int val.");
    return 0;
  }

  virtual int getDefaultIntVal()
  {
    ECCLOG.error(Loc::current(), "The option do not has int val.");
    return 0;
  }

  virtual std::vector<int> getIntList()
  {
    ECCLOG.error(Loc::current(), "The option do not has int list.");
    return {};
  }

  virtual std::vector<int> getDefaultIntList()
  {
    ECCLOG.error(Loc::current(), "The option do not has int list.");
    return {};
  }

  virtual std::vector<double> getDoubleList()
  {
    ECCLOG.error(Loc::current(), "The option do not has double list.");
    return {};
  }

  virtual std::vector<double> getDefaultDoubleList()
  {
    ECCLOG.error(Loc::current(), "The option do not has double list.");
    return {};
  }

  virtual std::vector<std::string> getStringList()
  {
    ECCLOG.error(Loc::current(), "The option do not has string list.");
    return {};
  }

  virtual std::vector<std::string> getDefaultStringList()
  {
    ECCLOG.error(Loc::current(), "The option do not has string list.");
    return {};
  }

  virtual std::vector<std::vector<std::string>> getStringListList()
  {
    ECCLOG.error(Loc::current(), "The option do not has string list list.");
    return {};
  }

  virtual std::vector<std::vector<std::string>> getDefaultStringListList()
  {
    ECCLOG.error(Loc::current(), "The option do not has string list list.");
    return {};
  }

  virtual std::vector<std::vector<std::vector<std::string>>> getStringListListList()
  {
    ECCLOG.error(Loc::current(), "The option do not has string list list list.");
    return {};
  }

  virtual std::vector<std::vector<std::vector<std::string>>> getDefaultStringListListList()
  {
    ECCLOG.error(Loc::current(), "The option do not has string list list list.");
    return {};
  }

  virtual std::vector<std::vector<std::vector<std::vector<std::string>>>> getStringListListListList()
  {
    ECCLOG.error(Loc::current(), "The option do not has string list list list list.");
    return {};
  }

  virtual std::vector<std::vector<std::vector<std::vector<std::string>>>> getDefaultStringListListListList()
  {
    ECCLOG.error(Loc::current(), "The option do not has string list list list list.");
    return {};
  }

  virtual void setVal(const char* /*val*/) { ECCLOG.error(Loc::current(), "The option can not set float val."); }

  virtual void resetVal() { ECCLOG.error(Loc::current(), "The option has not reset value."); }

  unsigned is_set_val() { return _is_set_val; }

 protected:
  std::vector<std::string> splitList(const char* val);

  unsigned _is_set_val = 0;

 private:
  std::string _option_name;
  unsigned _is_arg;
};

void checkTclOption(TclOption* option, const char* option_name, Loc location = Loc::current());

/**
 * @brief The tcl switch option.
 *
 */
class TclSwitchOption : public TclOption
{
 public:
  explicit TclSwitchOption(const char* option_name);
  ~TclSwitchOption() override;

  unsigned isSwitchOption() override { return 1; }

  void setVal(const char* /*val*/) override { _is_set_val = 1; }

  void resetVal() override { _is_set_val = 0; }
};

/**
 * @brief The tcl float option.
 *
 */
class TclDoubleOption : public TclOption
{
 public:
  TclDoubleOption(const char* option_name, unsigned is_arg, float default_val = 0.0);
  ~TclDoubleOption() override;

  unsigned isDoubleOption() override { return 1; }

  double getDoubleVal() override { return _is_set_val ? _val : _default_val; }
  void setVal(const char* val) override
  {
    _val = std::strtod(val, nullptr);
    _is_set_val = 1;
  }
  double getDefaultDoubleVal() override { return _default_val; }

  void resetVal() override { _is_set_val = 0; }

 private:
  double _default_val = 0.0;
  double _val;
};

/**
 * @brief The tcl string option.
 *
 */
class TclStringOption : public TclOption
{
 public:
  TclStringOption(const char* option_name, unsigned is_arg, const char* default_val = nullptr);
  ~TclStringOption() override;

  unsigned isStringOption() override { return 1; }

  const char* getStringVal() override { return _is_set_val ? _val.c_str() : (_default_val ? _default_val->c_str() : nullptr); }
  void setVal(const char* val) override
  {
    _val = val;
    _is_set_val = 1;
  }
  void resetVal() override { _is_set_val = 0; }
  const char* getDefaultStringVal() override { return _default_val ? _default_val->c_str() : nullptr; }

 private:
  std::optional<std::string> _default_val;
  std::string _val;
};

/**
 * @brief The tcl int option.
 *
 */
class TclIntOption : public TclOption
{
 public:
  TclIntOption(const char* option_name, unsigned is_arg, int default_val = 0) : TclOption(option_name, is_arg), _default_val(default_val) {}
  ~TclIntOption() override = default;

  unsigned isIntOption() override { return 1; }

  int getIntVal() override { return _is_set_val ? _val : _default_val; }
  void setVal(const char* val) override
  {
    _val = std::atoi(val);
    _is_set_val = 1;
  }
  void resetVal() override { _is_set_val = 0; }
  int getDefaultIntVal() override { return _default_val; }

 private:
  int _default_val = 0;
  int _val;
};

/**
 * @brief The tcl int list option.
 *
 */
class TclIntListOption : public TclOption
{
 public:
  TclIntListOption(const char* option_name, unsigned is_arg, std::vector<int> default_val = {})
      : TclOption(option_name, is_arg), _default_val(default_val)
  {
  }
  ~TclIntListOption() override = default;

  unsigned isIntListOption() override { return 1; }

  std::vector<int> getIntList() override { return _is_set_val ? _val : _default_val; }
  void setVal(const char* val) override
  {
    _val.clear();
    for (const std::string& item : splitList(val)) {
      _val.push_back(std::atoi(item.c_str()));
    }
    _is_set_val = 1;
  }
  void resetVal() override
  {
    std::vector<int>().swap(_val);
    _is_set_val = 0;
  }
  std::vector<int> getDefaultIntList() override { return _default_val; }

 private:
  std::vector<int> _default_val = {};
  std::vector<int> _val;
};

/**
 * @brief The tcl string list option.
 *
 */
class TclStringListOption : public TclOption
{
 public:
  TclStringListOption(const char* option_name, unsigned is_arg, std::vector<std::string> default_val = {})
      : TclOption(option_name, is_arg), _default_val(default_val)
  {
  }
  ~TclStringListOption() override = default;

  unsigned isStringOption() override { return 1; }

  std::vector<std::string> getStringList() override { return _is_set_val ? _val : _default_val; }
  void setVal(const char* val) override
  {
    _val = splitList(val);
    _is_set_val = 1;
  }
  void resetVal() override
  {
    // clear string vector, and release memory(is it needed for string?)
    std::vector<std::string>().swap(_val);
    _is_set_val = 0;
  }
  std::vector<std::string> getDefaultStringList() override { return _default_val; }

 private:
  std::vector<std::string> _default_val = {};
  std::vector<std::string> _val;
};

/**
 * @brief The tcl double list option.
 *
 */
class TclDoubleListOption : public TclOption
{
 public:
  TclDoubleListOption(const char* option_name, unsigned is_arg, std::vector<double> default_val = {})
      : TclOption(option_name, is_arg), _default_val(default_val)
  {
  }
  ~TclDoubleListOption() override = default;

  unsigned isStringOption() override { return 1; }

  std::vector<double> getDoubleList() override { return _is_set_val ? _val : _default_val; }
  void setVal(const char* val) override
  {
    _val.clear();
    for (const std::string& item : splitList(val)) {
      _val.push_back(std::strtod(item.c_str(), nullptr));
    }
    _is_set_val = 1;
  }
  void resetVal() override
  {
    std::vector<double>().swap(_val);
    _is_set_val = 0;
  }
  std::vector<double> getDefaultDoubleList() override { return _default_val; }

 private:
  std::vector<double> _default_val = {};
  std::vector<double> _val;
};

/**
 * @brief The tcl string list list option.
 * such as set_clock_group -group xxx -group xxx.
 *
 */
class TclStringListListOption : public TclOption
{
 public:
  using StrList = std::vector<std::string>;
  TclStringListListOption(const char* option_name, unsigned is_arg, std::vector<StrList>&& default_val = {});
  ~TclStringListListOption() override = default;

  unsigned isStringListListOption() override { return 1; }

  void setVal(const char* val) override;
  void resetVal() override
  {
    std::vector<StrList>().swap(_val);
    _is_set_val = 0;
  }

  std::vector<StrList> getStringListList() override { return _is_set_val ? _val : _default_val; }
  std::vector<StrList> getDefaultStringListList() override { return _default_val; }

 private:
  std::vector<StrList> _default_val;
  std::vector<StrList> _val;
};

/**
 * @brief The tcl string list list list option.
 *
 */
class TclStringListListListOption : public TclOption
{
 public:
  using StrList = std::vector<std::string>;
  using StrListList = std::vector<StrList>;
  TclStringListListListOption(const char* option_name, unsigned is_arg, std::vector<StrListList>&& default_val = {});
  ~TclStringListListListOption() override = default;

  unsigned isStringListListListOption() override { return 1; }

  void setVal(const char* val) override;
  void resetVal() override
  {
    std::vector<StrListList>().swap(_val);
    _is_set_val = 0;
  }

  std::vector<StrListList> getStringListListList() override { return _is_set_val ? _val : _default_val; }
  std::vector<StrListList> getDefaultStringListListList() override { return _default_val; }

 private:
  std::vector<StrListList> _default_val;
  std::vector<StrListList> _val;
};

/**
 * @brief The tcl string list list list list option.
 *
 */
class TclStringListListListListOption : public TclOption
{
 public:
  using StrList = std::vector<std::string>;
  using StrListList = std::vector<StrList>;
  using StrListListList = std::vector<StrListList>;
  TclStringListListListListOption(const char* option_name, unsigned is_arg, std::vector<StrListListList>&& default_val = {});
  ~TclStringListListListListOption() override = default;

  unsigned isStringListListListListOption() override { return 1; }

  void setVal(const char* val) override;
  void resetVal() override
  {
    std::vector<StrListListList>().swap(_val);
    _is_set_val = 0;
  }

  std::vector<StrListListList> getStringListListListList() override { return _is_set_val ? _val : _default_val; }
  std::vector<StrListListList> getDefaultStringListListListList() override { return _default_val; }

 private:
  std::vector<StrListListList> _default_val;
  std::vector<StrListListList> _val;
};

/**
 * @brief The tcl cmd base class.
 *
 */
class TclCmd
{
 public:
  explicit TclCmd(const char* cmd_name);
  virtual ~TclCmd();

  const char* get_cmd_name() const { return _cmd_name.c_str(); }
  TclOption* getOptionOrArg(const char* option_name)
  {
    if (containWildcard(option_name)) {
      return findOptionWithWildcard(option_name);
    } else {
      if (auto it = _options.find(option_name); it != _options.end()) {
        return it->second.get();
      }
    }

    return nullptr;
  }
  void addOption(TclOption* option)
  {
    // The arg need keep order.
    if (option->is_arg()) {
      _args.push_back(option);
    }

    _options.emplace(option->get_option_name(), option);
  }

  TclOption* getArg(int index) { return (static_cast<int>(_args.size()) > index) ? _args[index] : nullptr; }

  void resetOptionArgValue();

  virtual unsigned printHelp() {
    ECCLOG.error(Loc::current(), "This cmd has not define print help body.");
    return 0;
  }

  virtual unsigned check()
  {
    ECCLOG.error(Loc::current(), "This cmd has not define check body.");
    return 0;
  }
  virtual unsigned exec()
  {
    ECCLOG.error(Loc::current(), "This cmd has not define exe body.");
    return 0;
  }

 private:
  TclOption* findOptionWithWildcard(const char* option_name)
  {
    int match_times = 0;
    auto res = _options.end();
    for (auto it = _options.begin(); it != _options.end(); ++it) {
      if (matchWildcardWithtarget(option_name, it->second.get()->get_option_name())) {
        if (++match_times > 1) {
          ECCLOG.warn(Loc::current(), "invalid option wildcard(s), multiple options matched.");
          assert(0);
        }
        res = it;
      }
    }
    return res == _options.end() ? nullptr : res->second.get();
  }
  std::string _cmd_name;
  std::map<std::string, std::unique_ptr<TclOption>> _options;  //!< The tcl option do not need keep order.
  absl::InlinedVector<TclOption*, 64> _args;  //!< The tcl arg need keep order.
};

/**
 * @brief The all tcl cmd container.
 *
 */
class TclCmds
{
 public:
  static void addTclCmd(std::unique_ptr<TclCmd> cmd);
  static TclCmd* getTclCmd(const char* cmd_name);

 private:
  static std::map<std::string, std::unique_ptr<TclCmd>> _cmds;
};

/**
 * @brief Encode/decode tcl pointer result.
 *
 */
class TclEncodeResult
{
 public:
  static std::string encode(void* pointer);
  static void* decode(const char* encode_str);
  static const char* get_encode_preamble() { return _encode_preamble; }

 private:
  inline static const char* _encode_preamble = "@ptr";
};

/**
 * @brief initialization -- register a user defined command
 * @param type-class type command class type (a derived classe of TclCmd)
 * @param name-const char* command's name
 */
#define registerTclCmd(type, name)               \
  do {                                           \
    auto cmd_ptr = std::make_unique<type>(name); \
    TclCmds::addTclCmd(std::move(cmd_ptr));      \
  } while (0)

}  // namespace ecc
