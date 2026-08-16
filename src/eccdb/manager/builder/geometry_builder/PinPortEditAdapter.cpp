#include "PinPortEditAdapter.h"

#include "IdbDesign.h"
#include "IdbLayerShape.h"
#include "IdbPins.h"
#include "IdbTerm.h"

#include <cstddef>
#include <vector>

namespace ecc::geometry {

namespace {

OwnerId pin_owner_id_from_path(idb::IdbPin* pin, uint32_t path0, uint32_t path1)
{
  if (pin == nullptr) {
    return 0;
  }

  return pin->get_id() != 0 ? pin->get_id() : (static_cast<OwnerId>(path0) << 32U | path1);
}

Rect32 rect_from_idb(idb::IdbRect* rect)
{
  if (rect == nullptr) {
    return {};
  }

  return Rect32{rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y()};
}

bool is_default_orient(idb::IdbOrient orient)
{
  return orient == idb::IdbOrient::kN_R0;
}

void set_diagnostic(GeometryEditDiagnostic* diagnostic, GeometryEditDiagnostic value)
{
  if (diagnostic != nullptr) {
    *diagnostic = value;
  }
}

idb::IdbPin* resolve_io_pin(idb::IdbDesign& design, OwnerRef owner, GeometryEditDiagnostic* diagnostic)
{
  const bool io_pin_owner = owner.type == OwnerType::kIoPinPortShape
                            || (owner.type == OwnerType::kPinPortShape && owner.path0 == 0);
  if (!io_pin_owner || owner.path0 != 0) {
    set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
    return nullptr;
  }

  auto* pins = design.get_io_pin_list();
  if (pins == nullptr) {
    set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
    return nullptr;
  }

  std::vector<idb::IdbPin*>& pin_list = pins->get_pin_list();
  if (owner.path1 >= pin_list.size()) {
    set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
    return nullptr;
  }

  idb::IdbPin* pin = pin_list[static_cast<size_t>(owner.path1)];
  if (pin == nullptr || !pin->is_io_pin() || pin->get_instance() != nullptr) {
    set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
    return nullptr;
  }

  if (pin_owner_id_from_path(pin, owner.path0, owner.path1) != owner.owner_id) {
    set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
    return nullptr;
  }

  return pin;
}

struct SourceRectRef
{
  idb::IdbRect* rect = nullptr;
  int32_t origin_x = 0;
  int32_t origin_y = 0;
};

SourceRectRef resolve_source_rect_from_port_placement(idb::IdbTerm& term, OwnerRef owner,
                                                      GeometryEditDiagnostic* diagnostic)
{
  uint32_t layer_shape_index = 0;
  for (auto* port : term.get_port_list()) {
    if (port == nullptr || port->get_coordinate() == nullptr) {
      set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
      return {};
    }
    if (!is_default_orient(port->get_orient())) {
      set_diagnostic(diagnostic, GeometryEditDiagnostic::kUnsupportedTransform);
      return {};
    }

    for (auto* layer_shape : port->get_layer_shape()) {
      if (layer_shape == nullptr) {
        ++layer_shape_index;
        continue;
      }

      if (layer_shape_index != owner.path2) {
        ++layer_shape_index;
        continue;
      }

      std::vector<idb::IdbRect*>& rects = layer_shape->get_rect_list();
      if (owner.path3 >= rects.size()) {
        set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
        return {};
      }

      return SourceRectRef{rects[static_cast<size_t>(owner.path3)], port->get_coordinate()->get_x(),
                           port->get_coordinate()->get_y()};
    }
  }

  set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
  return {};
}

SourceRectRef resolve_source_rect_from_pin_placement(idb::IdbPin& pin, idb::IdbTerm& term, OwnerRef owner,
                                                     GeometryEditDiagnostic* diagnostic)
{
  if (pin.get_location() == nullptr) {
    set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
    return {};
  }
  if (!is_default_orient(pin.get_orient())) {
    set_diagnostic(diagnostic, GeometryEditDiagnostic::kUnsupportedTransform);
    return {};
  }

  uint32_t layer_shape_index = 0;
  for (auto* port : term.get_port_list()) {
    if (port == nullptr) {
      set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
      return {};
    }

    for (auto* layer_shape : port->get_layer_shape()) {
      if (layer_shape == nullptr) {
        ++layer_shape_index;
        continue;
      }

      if (layer_shape_index != owner.path2) {
        ++layer_shape_index;
        continue;
      }

      std::vector<idb::IdbRect*>& rects = layer_shape->get_rect_list();
      if (owner.path3 >= rects.size()) {
        set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
        return {};
      }

      return SourceRectRef{rects[static_cast<size_t>(owner.path3)], pin.get_location()->get_x(),
                           pin.get_location()->get_y()};
    }
  }

  set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
  return {};
}

SourceRectRef resolve_source_rect(idb::IdbPin& pin, OwnerRef owner, GeometryEditDiagnostic* diagnostic)
{
  idb::IdbTerm* term = pin.get_term();
  if (term == nullptr || term->get_port_list().empty() || !term->is_placed()) {
    set_diagnostic(diagnostic, GeometryEditDiagnostic::kOwnerPathUnavailable);
    return {};
  }

  if (term->is_port_exist()) {
    return resolve_source_rect_from_port_placement(*term, owner, diagnostic);
  }

  return resolve_source_rect_from_pin_placement(pin, *term, owner, diagnostic);
}

Rect32 committed_port_rect(idb::IdbPin& pin, OwnerRef owner, Rect32 fallback)
{
  std::vector<idb::IdbLayerShape*>& layer_shapes = pin.get_port_box_list();
  if (owner.path2 >= layer_shapes.size()) {
    return fallback;
  }

  idb::IdbLayerShape* layer_shape = layer_shapes[static_cast<size_t>(owner.path2)];
  if (layer_shape == nullptr || owner.path3 >= layer_shape->get_rect_list().size()) {
    return fallback;
  }

  return rect_from_idb(layer_shape->get_rect_list()[static_cast<size_t>(owner.path3)]);
}

}  // namespace

bool PinPortEditAdapter::update_rect(idb::IdbDesign& design, OwnerRef owner, Rect32 requested_bbox,
                                     Rect32& committed_bbox, GeometryEditDiagnostic* diagnostic) const
{
  set_diagnostic(diagnostic, GeometryEditDiagnostic::kBackendUpdateFailed);

  idb::IdbPin* pin = resolve_io_pin(design, owner, diagnostic);
  if (pin == nullptr) {
    return false;
  }

  SourceRectRef source = resolve_source_rect(*pin, owner, diagnostic);
  if (source.rect == nullptr) {
    return false;
  }

  committed_bbox = normalize(requested_bbox);
  const Rect32 source_bbox{committed_bbox.lx - source.origin_x, committed_bbox.ly - source.origin_y,
                          committed_bbox.hx - source.origin_x, committed_bbox.hy - source.origin_y};
  source.rect->set_rect(source_bbox.lx, source_bbox.ly, source_bbox.hx, source_bbox.hy);
  pin->set_bounding_box();
  committed_bbox = normalize(committed_port_rect(*pin, owner, committed_bbox));
  return true;
}

}  // namespace ecc::geometry
