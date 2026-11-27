#include <limits>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "RCXInterface.hpp"
#include "idm.h"

namespace ircx {
void RCXInterface::wrapDensityMetal()
{
  auto* design = dmInst->get_idb_def_service()->get_design();
  auto& database = RCXDM.getDatabase();
  auto& output = database.get_layout_data().get_density_metal_shapes();
  output.clear();

  auto append_bounds = [&](idb::IdbLayer* layer, int64_t xl, int64_t yl, int64_t xh, int64_t yh) {
    if (!layer || !layer->is_routing())
      return;
    for (int64_t coordinate : {xl, yl, xh, yh})
      if (coordinate < std::numeric_limits<int32_t>::min() || coordinate > std::numeric_limits<int32_t>::max())
        RCXLOG.error(Loc::current(), "Metal density geometry exceeds database coordinate range");
    if (xl > xh || yl > yh)
      RCXLOG.error(Loc::current(), "Metal density geometry contains a reversed rectangle on ", layer->get_name());
    if (xl == xh || yl == yh)
      return;
    output.emplace_back(database.get_layer_table().get_design_idx(layer->get_name()),
                        GTLRectInt(static_cast<int32_t>(xl), static_cast<int32_t>(yl), static_cast<int32_t>(xh), static_cast<int32_t>(yh)));
  };
  auto append_rectangle = [&](idb::IdbLayer* layer, idb::IdbRect* rect, int64_t dx = 0, int64_t dy = 0) {
    if (rect)
      append_bounds(layer, rect->get_low_x() + dx, rect->get_low_y() + dy, rect->get_high_x() + dx, rect->get_high_y() + dy);
  };
  auto append_pin = [&](idb::IdbPin* pin) {
    if (!pin)
      return;
    for (auto* shape : pin->get_port_box_list())
      if (shape)
        for (auto* rect : shape->get_rect_list())
          append_rectangle(shape->get_layer(), rect);
  };
  auto append_via = [&](idb::IdbVia* via, idb::IdbCoordinate<int32_t>* location = nullptr) {
    if (!via || !via->get_coordinate())
      return;
    // Via accessors already translate the master shape to the via coordinate.
    const int64_t dx = location ? int64_t(location->get_x()) - via->get_coordinate()->get_x() : 0;
    const int64_t dy = location ? int64_t(location->get_y()) - via->get_coordinate()->get_y() : 0;
    for (auto shape : {via->get_bottom_layer_shape(), via->get_top_layer_shape()})
      for (auto* rect : shape.get_rect_list())
        append_rectangle(shape.get_layer(), rect, dx, dy);
  };

  if (auto* pins = design->get_io_pin_list())
    for (auto* pin : pins->get_pin_list())
      append_pin(pin);
  if (auto* instances = design->get_instance_list())
    for (auto* instance : instances->get_instance_list())
      if (instance && instance->get_pin_list())
        for (auto* pin : instance->get_pin_list()->get_pin_list())
          append_pin(pin);
  if (auto* nets = design->get_net_list())
    for (auto* net : nets->get_net_list())
      if (net && net->get_wire_list())
        for (auto* wire : net->get_wire_list()->get_wire_list())
          if (wire)
            for (auto* segment : wire->get_segment_list())
              if (segment)
                for (auto* via : segment->get_via_list())
                  append_via(via);
  if (auto* nets = design->get_special_net_list())
    for (auto* net : nets->get_net_list())
      if (net && net->get_wire_list())
        for (auto* wire : net->get_wire_list()->get_wire_list())
          if (wire)
            for (auto* segment : wire->get_segment_list()) {
              if (!segment)
                continue;
              if (segment->is_via())
                append_via(segment->get_via());
              if (segment->is_rect())
                append_rectangle(segment->get_layer(), segment->get_delta_rect());
              auto* layer = dynamic_cast<idb::IdbLayerRouting*>(segment->get_layer());
              const auto& points = segment->get_point_list();
              if (!layer || points.size() < 2)
                continue;
              const int64_t width = segment->get_route_width() > 0 ? segment->get_route_width() : layer->get_width();
              if (width <= 0)
                RCXLOG.error(Loc::current(), "Invalid special route width for metal density");
              for (std::size_t i = 1; i < points.size(); ++i) {
                auto* a = points[i - 1];
                auto* b = points[i];
                if (!a || !b)
                  RCXLOG.error(Loc::current(), "Missing special route point for metal density");
                if (a->get_x() == b->get_x() && a->get_y() == b->get_y())
                  continue;
                if (a->get_x() != b->get_x() && a->get_y() != b->get_y())
                  RCXLOG.error(Loc::current(), "Nonorthogonal special route is unsupported for metal density");
                const bool horizontal = a->get_y() == b->get_y();
                const int64_t first = horizontal ? a->get_x() : a->get_y(), second = horizontal ? b->get_x() : b->get_y();
                const int64_t ext_a = segment->get_point_ext(a).value_or(i == 1 ? 0 : width / 2);
                const int64_t ext_b = segment->get_point_ext(b).value_or(i + 1 == points.size() ? 0 : width / 2);
                if (ext_a < 0 || ext_b < 0)
                  RCXLOG.error(Loc::current(), "Negative special route endpoint extension");
                const int64_t lo = first < second ? first - ext_a : second - ext_b;
                const int64_t hi = first < second ? second + ext_b : first + ext_a;
                const int64_t side = (horizontal ? a->get_y() : a->get_x()) - width / 2;
                if (horizontal)
                  append_bounds(layer, lo, side, hi, side + width);
                else
                  append_bounds(layer, side, lo, side + width, hi);
              }
            }
  if (auto* fills = design->get_fill_list())
    for (auto* fill : fills->get_fill_list()) {
      if (!fill)
        continue;
      if (fill->get_type() == idb::IdbFill::kLayer && fill->get_layer()) {
        auto* layer = fill->get_layer();
        for (auto* rect : layer->get_rect_list())
          append_rectangle(layer->get_layer(), rect);
      } else if (fill->get_type() == idb::IdbFill::kVia && fill->get_via()) {
        auto* fill_via = fill->get_via();
        for (auto* location : fill_via->get_coordinate_list())
          if (location)
            append_via(fill_via->get_via(), location);
      }
    }
}
}  // namespace ircx
