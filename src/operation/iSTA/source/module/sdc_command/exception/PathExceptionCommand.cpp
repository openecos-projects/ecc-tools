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

#include "object/ObjectQuery.hpp"

namespace ista::sdc {

namespace {

bool intersects(const std::set<std::string>& lhs, const std::set<std::string>& rhs)
{
  if (lhs.empty() || rhs.empty()) {
    return true;
  }
  return std::any_of(lhs.begin(), lhs.end(), [&rhs](const std::string& object) { return rhs.contains(object); });
}

bool transitionsIntersect(TransType lhs, TransType rhs)
{
  return lhs == TransType::kNone || rhs == TransType::kNone || lhs == rhs;
}

bool throughSelectorsIntersect(const std::vector<TimingExceptionThrough>& filter, const std::vector<TimingExceptionThrough>& existing)
{
  if (filter.empty()) {
    return true;
  }
  if (existing.empty()) {
    return false;
  }
  const std::size_t count = std::min(filter.size(), existing.size());
  for (std::size_t index = 0; index < count; ++index) {
    if (!intersects(filter[index].get_objects(), existing[index].get_objects())
        || !transitionsIntersect(filter[index].get_trans_type(), existing[index].get_trans_type())) {
      return false;
    }
  }
  return true;
}

bool selectorIntersects(const TimingException& filter, const TimingException& existing)
{
  const bool from_matches = filter.get_from_objects().empty()
                            || (!existing.get_from_objects().empty() && intersects(filter.get_from_objects(), existing.get_from_objects())
                                && transitionsIntersect(filter.get_from_trans_type(), existing.get_from_trans_type()));
  const bool to_matches = filter.get_to_objects().empty()
                          || (!existing.get_to_objects().empty() && intersects(filter.get_to_objects(), existing.get_to_objects())
                              && transitionsIntersect(filter.get_to_trans_type(), existing.get_to_trans_type()));
  return from_matches && to_matches && throughSelectorsIntersect(filter.get_through_list(), existing.get_through_list());
}

std::vector<TimingException> expandExceptionObjects(const TimingException& exception)
{
  std::vector<TimingException> expanded{exception};
  const auto expandObjects = [&expanded](const std::set<std::string>& objects, auto setter) {
    if (objects.size() <= 1) {
      return;
    }
    std::vector<TimingException> next;
    for (const TimingException& current : expanded) {
      for (const std::string& object : objects) {
        TimingException clone = current;
        setter(clone, std::set<std::string>{object});
        next.push_back(std::move(clone));
      }
    }
    expanded = std::move(next);
  };
  expandObjects(exception.get_from_objects(), [](TimingException& item, const std::set<std::string>& objects) { item.set_from_objects(objects); });
  expandObjects(exception.get_to_objects(), [](TimingException& item, const std::set<std::string>& objects) { item.set_to_objects(objects); });
  for (std::size_t through_index = 0; through_index < exception.get_through_list().size(); ++through_index) {
    const std::set<std::string> objects = exception.get_through_list()[through_index].get_objects();
    expandObjects(objects, [through_index](TimingException& item, const std::set<std::string>& selected) {
      std::vector<TimingExceptionThrough> through_list = item.get_through_list();
      through_list[through_index].set_objects(selected);
      item.set_through_list(through_list);
    });
  }
  return expanded;
}

std::vector<TimingException> subtractExceptionDimensions(const TimingException& existing, const TimingException& filter)
{
  std::vector<TimingException> result;
  for (TransType transition : {TransType::kRise, TransType::kFall}) {
    const bool existing_transition = transition == TransType::kRise ? existing.get_rise() : existing.get_fall();
    if (!existing_transition) {
      continue;
    }
    const bool reset_transition = transition == TransType::kRise ? filter.get_rise() : filter.get_fall();
    TimingException remainder = existing;
    remainder.set_rise(transition == TransType::kRise);
    remainder.set_fall(transition == TransType::kFall);

    if (existing.get_type() == TimingExceptionType::kMulticycle) {
      std::optional<int32_t> setup = existing.get_setup_multiplier();
      std::optional<int32_t> hold = existing.get_hold_multiplier();
      if (reset_transition && filter.get_setup()) {
        setup.reset();
      }
      if (reset_transition && filter.get_hold()) {
        hold.reset();
      }
      if (!setup.has_value() && !hold.has_value()) {
        continue;
      }
      remainder.set_setup_multiplier(setup);
      remainder.set_hold_multiplier(hold);
      remainder.set_setup(setup.has_value());
      remainder.set_hold(hold.has_value());
      result.push_back(std::move(remainder));
      continue;
    }

    bool setup = existing.get_setup();
    bool hold = existing.get_hold();
    if (reset_transition && filter.get_setup()) {
      setup = false;
    }
    if (reset_transition && filter.get_hold()) {
      hold = false;
    }
    if (!setup && !hold) {
      continue;
    }
    remainder.set_setup(setup);
    remainder.set_hold(hold);
    result.push_back(std::move(remainder));
  }
  return result;
}

}  // namespace

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
  const bool has_through = getOptionOrArg("-through")->is_set_val() || getOptionOrArg("-rise_through")->is_set_val()
                           || getOptionOrArg("-fall_through")->is_set_val();
  if (require_selector && from_count == 0 && to_count == 0 && !has_through) {
    throw std::invalid_argument(std::string(get_cmd_name()) + " requires -from, -through, or -to");
  }

  TimingException exception;
  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  exception.set_rise(rise || !fall);
  exception.set_fall(fall || !rise);

  for (const std::pair<const char*, TransType>& selector : {std::pair{"-from", TransType::kNone}, std::pair{"-rise_from", TransType::kRise},
                                                            std::pair{"-fall_from", TransType::kFall}}) {
    if (getOptionOrArg(selector.first)->is_set_val()) {
      exception.set_from_objects(findExceptionObjects(database, getOptionOrArg(selector.first)->getStringList()));
      exception.set_from_trans_type(selector.second);
    }
  }
  for (const std::pair<const char*, TransType>& selector : {std::pair{"-to", TransType::kNone}, std::pair{"-rise_to", TransType::kRise},
                                                            std::pair{"-fall_to", TransType::kFall}}) {
    if (getOptionOrArg(selector.first)->is_set_val()) {
      exception.set_to_objects(findExceptionObjects(database, getOptionOrArg(selector.first)->getStringList()));
      exception.set_to_trans_type(selector.second);
    }
  }

  std::vector<TimingExceptionThrough> through_list;
  for (const auto& [option, value] : getOptionValueList()) {
    TransType trans_type = TransType::kNone;
    if (option == "-rise_through") {
      trans_type = TransType::kRise;
    } else if (option == "-fall_through") {
      trans_type = TransType::kFall;
    } else if (option != "-through") {
      continue;
    }
    TimingExceptionThrough through;
    through.set_objects(findExceptionObjects(database, parseObjectPatterns(value, false)));
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

void resetPathExceptions(Database& database, const TimingException& filter)
{
  std::vector<TimingException> result;
  for (const TimingException& existing : database.get_timing_constraint().get_path_exception_list()) {
    if (!selectorIntersects(filter, existing)) {
      result.push_back(existing);
      continue;
    }
    for (const TimingException& expansion : expandExceptionObjects(existing)) {
      if (!selectorIntersects(filter, expansion)) {
        result.push_back(expansion);
        continue;
      }
      std::vector<TimingException> remaining = subtractExceptionDimensions(expansion, filter);
      result.insert(result.end(), std::make_move_iterator(remaining.begin()), std::make_move_iterator(remaining.end()));
    }
  }
  database.get_timing_constraint().get_path_exception_list() = std::move(result);
}

}  // namespace ista::sdc
