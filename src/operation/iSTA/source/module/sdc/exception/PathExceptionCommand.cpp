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
#include "PathExceptionCommand.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "query/ObjectQuery.hpp"

namespace ista::sdc {

TclPathException::TclPathException(const char* cmd_name, ClientData client_data, bool comment_option) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringListOption("-from", 0));
  addOption(new ecc::TclStringListOption("-rise_from", 0));
  addOption(new ecc::TclStringListOption("-fall_from", 0));
  addOption(new ecc::TclStringListListOption("-through", 0));
  addOption(new ecc::TclStringListListOption("-rise_through", 0));
  addOption(new ecc::TclStringListListOption("-fall_through", 0));
  addOption(new ecc::TclStringListOption("-to", 0));
  addOption(new ecc::TclStringListOption("-rise_to", 0));
  addOption(new ecc::TclStringListOption("-fall_to", 0));
  addOption(new ecc::TclSwitchOption("-rise"));
  addOption(new ecc::TclSwitchOption("-fall"));
  if (comment_option) {
    addOption(new ecc::TclStringOption("-comment", 0));
  }
}

TimingException TclPathException::parsePathSelector(Database& database, bool require_selector)
{
  int from_count = 0;
  int to_count = 0;
  for (const char* option : {"-from", "-rise_from", "-fall_from"}) {
    from_count += getOptionOrArg(option)->is_set_val();
  }
  for (const char* option : {"-to", "-rise_to", "-fall_to"}) {
    to_count += getOptionOrArg(option)->is_set_val();
  }
  if (from_count > 1 || to_count > 1) {
    throw std::invalid_argument(std::string(get_cmd_name()) + " accepts only one from selector and one to selector");
  }
  const bool has_through
      = getOptionOrArg("-through")->is_set_val() || getOptionOrArg("-rise_through")->is_set_val() || getOptionOrArg("-fall_through")->is_set_val();
  if (require_selector && from_count == 0 && to_count == 0 && !has_through) {
    throw std::invalid_argument(std::string(get_cmd_name()) + " requires -from, -through, or -to");
  }

  TimingException exception;
  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  exception.set_rise(rise || !fall);
  exception.set_fall(fall || !rise);

  for (const std::pair<const char*, TransType>& selector :
       {std::pair{"-from", TransType::kNone}, std::pair{"-rise_from", TransType::kRise}, std::pair{"-fall_from", TransType::kFall}}) {
    if (getOptionOrArg(selector.first)->is_set_val()) {
      exception.set_from_objects(findExceptionObjects(interpreter(), database, getOptionObjects(selector.first)));
      exception.set_from_trans_type(selector.second);
    }
  }
  for (const std::pair<const char*, TransType>& selector :
       {std::pair{"-to", TransType::kNone}, std::pair{"-rise_to", TransType::kRise}, std::pair{"-fall_to", TransType::kFall}}) {
    if (getOptionOrArg(selector.first)->is_set_val()) {
      exception.set_to_objects(findExceptionObjects(interpreter(), database, getOptionObjects(selector.first)));
      exception.set_to_trans_type(selector.second);
    }
  }

  for (const auto* objects : {&exception.get_from_objects(), &exception.get_to_objects()}) {
    for (const auto& object : *objects) {
      if (object.type == ExceptionObjectType::kPin && database.network().hierarchyPins().contains(object.name))
        throw std::invalid_argument("hierarchical -from/-to endpoints are not supported; use a valid timing endpoint: " + object.name);
    }
  }
  std::vector<TimingExceptionThrough> through_list;
  for (const auto& [option, value] : getOptionObjectValueList()) {
    TransType trans_type = TransType::kNone;
    if (option == "-rise_through") {
      trans_type = TransType::kRise;
    } else if (option == "-fall_through") {
      trans_type = TransType::kFall;
    } else if (option != "-through") {
      continue;
    }
    TimingExceptionThrough through;
    through.set_objects(findExceptionObjects(interpreter(), database, SdcObject::collection(value)));
    through.set_trans_type(trans_type);
    through_list.push_back(std::move(through));
  }
  exception.set_through_list(through_list);

  ecc::TclOption* comment = getOptionOrArg("-comment");
  if (comment != nullptr && comment->is_set_val()) {
    exception.set_comment(comment->getStringVal());
  }
  return exception;
}

void TclPathException::applyAnalysisQualifiers(TimingException& exception)
{
  const bool setup = getOptionOrArg("-setup")->is_set_val();
  const bool hold = getOptionOrArg("-hold")->is_set_val();
  exception.set_setup(setup || !hold);
  exception.set_hold(hold || !setup);
}

}  // namespace ista::sdc
