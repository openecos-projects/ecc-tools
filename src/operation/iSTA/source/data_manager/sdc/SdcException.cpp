// SPDX-License-Identifier: MulanPSL-2.0
#include <algorithm>
#include <iterator>
#include <utility>

#include "sdc/Sdc.hpp"

namespace ista {
namespace {

bool intersects(const ExceptionObjects& lhs, const ExceptionObjects& rhs)
{
  if (lhs.empty() || rhs.empty()) {
    return true;
  }
  return std::any_of(lhs.begin(), lhs.end(), [&rhs](const ExceptionObject& object) { return rhs.contains(object); });
}

bool throughSelectorsIntersect(const std::vector<TimingExceptionThrough>& filter, const std::vector<TimingExceptionThrough>& existing)
{
  // reset_path selects an ordered subsequence, not a common prefix. Edge
  // qualifiers identify distinct exception selectors (including unqualified).
  std::size_t selected = 0;
  for (const auto& through : existing) {
    if (selected < filter.size() && intersects(filter[selected].get_objects(), through.get_objects())
        && filter[selected].get_trans_type() == through.get_trans_type())
      ++selected;
  }
  return selected == filter.size();
}

bool selectorIntersects(const TimingException& filter, const TimingException& existing)
{
  const bool from_matches = filter.get_from_objects().empty()
                            || (!existing.get_from_objects().empty() && intersects(filter.get_from_objects(), existing.get_from_objects())
                                && filter.get_from_trans_type() == existing.get_from_trans_type());
  const bool to_matches = filter.get_to_objects().empty()
                          || (!existing.get_to_objects().empty() && intersects(filter.get_to_objects(), existing.get_to_objects())
                              && filter.get_to_trans_type() == existing.get_to_trans_type());
  return from_matches && to_matches && throughSelectorsIntersect(filter.get_through_list(), existing.get_through_list());
}

std::vector<TimingException> expandExceptionObjects(const TimingException& exception)
{
  std::vector<TimingException> expanded{exception};
  const auto expandObjects = [&expanded](const ExceptionObjects& objects, auto setter) {
    if (objects.size() <= 1) {
      return;
    }
    std::vector<TimingException> next;
    for (const TimingException& current : expanded) {
      for (const ExceptionObject& object : objects) {
        TimingException clone = current;
        setter(clone, ExceptionObjects{object});
        next.push_back(std::move(clone));
      }
    }
    expanded = std::move(next);
  };
  expandObjects(exception.get_from_objects(), [](TimingException& item, const ExceptionObjects& objects) { item.set_from_objects(objects); });
  expandObjects(exception.get_to_objects(), [](TimingException& item, const ExceptionObjects& objects) { item.set_to_objects(objects); });
  for (std::size_t through_index = 0; through_index < exception.get_through_list().size(); ++through_index) {
    const ExceptionObjects objects = exception.get_through_list()[through_index].get_objects();
    expandObjects(objects, [through_index](TimingException& item, const ExceptionObjects& selected) {
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

void Sdc::resetPathExceptions(const TimingException& filter)
{
  std::vector<TimingException> result;
  for (const TimingException& existing : _path_exception_list) {
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
  _path_exception_list = std::move(result);
}

}  // namespace ista
