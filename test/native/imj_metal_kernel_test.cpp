// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "MIComParam.hpp"
#include "MIDensityWindow.hpp"
#include "MILayer.hpp"
#include "MIModel.hpp"
#include "MIRect.hpp"
#include "adapters/imj/metal/MetalFillKernel.hpp"

namespace {

class FakeMetalFillSource final : public imj::MetalFillSource
{
 public:
  [[nodiscard]] std::vector<imj::MetalFillOccupancy> query(const imj::MILayer&, const imj::MIRect& region) const override
  {
    if (_obstacle.is_intersect(region)) {
      return {{.bounds = _obstacle, .contributes_density = false, .blocks_fill = true}};
    }
    return {};
  }

  [[nodiscard]] int32_t spacing(const imj::MILayer&, int32_t, int32_t) const override { return 2; }

  [[nodiscard]] const imj::MIRect& obstacle() const noexcept { return _obstacle; }

 private:
  imj::MIRect _obstacle{35, 0, 45, 40};
};

}  // namespace

int main()
{
  imj::MIRect rect(0, 0, 10, 4);
  if (!rect.is_valid() || !rect.is_intersect(imj::MIRect(9, 0, 11, 4)) || rect.is_intersect(imj::MIRect(10, 0, 12, 4)) || rect.get_area() != 40.0) {
    return 1;
  }

  imj::MIComParam mi_com_param(100.0, 50.0, 0.10, 0.80);
  mi_com_param.set_min_fill_layer("MET1");
  mi_com_param.set_max_fill_layer("MET5");
  if (mi_com_param.get_density_window_size_micron() != 100.0 || mi_com_param.get_density_window_step_micron() != 50.0 || mi_com_param.get_min_density() != 0.10
      || mi_com_param.get_max_density() != 0.80 || mi_com_param.get_min_fill_layer() != "MET1" || mi_com_param.get_max_fill_layer() != "MET5") {
    return 1;
  }

  imj::MIDensityWindow density_window(imj::MIRect(0, 0, 10, 10), 20.0);
  density_window.add_metal_area(5.0);
  if (density_window.get_density() != 0.25) {
    return 1;
  }

  imj::MILayer mi_layer;
  mi_layer.set_layer_name("M1");
  mi_layer.set_density_window_x_num(2);
  mi_layer.set_density_window_y_num(3);
  mi_layer.set_density_window_list({density_window});
  if (mi_layer.get_layer_name() != "M1" || mi_layer.get_density_window_idx(1, 2) != 5 || mi_layer.get_density_window_list().size() != 1) {
    return 1;
  }

  imj::MIModel mi_model;
  mi_model.set_mi_com_param(mi_com_param);
  mi_model.set_die(rect);
  mi_model.set_mi_layer_list({mi_layer});
  if (mi_model.get_inserted_metal_num() != 0) {
    return 1;
  }
  mi_model.set_inserted_metal_num(2);
  mi_model.add_inserted_metal_num();
  if (mi_model.get_inserted_metal_num() != 3) {
    return 1;
  }

  // The EccDB adaptor planner must split an otherwise continuous track around the
  // backend-provided obstacle. This exercises the adaptor source/planner boundary without
  // depending on an iDB or EccDB database fixture.
  imj::MIModel fill_model;
  fill_model.set_mi_com_param(imj::MIComParam(100.0, 50.0, 0.10, 0.80));
  fill_model.set_die(imj::MIRect(0, 0, 100, 40));
  fill_model.set_micron_dbu(1);
  fill_model.set_manufacture_grid(1);
  imj::MILayer fill_layer;
  fill_layer.set_layer_name("M1");
  fill_layer.set_is_horizontal(true);
  fill_layer.set_wire_width(4);
  fill_layer.set_min_wire_length(10);
  fill_layer.set_track_start(4);
  fill_layer.set_track_pitch(10);
  fill_layer.set_max_spacing(2);
  fill_model.set_mi_layer_list({fill_layer});
  FakeMetalFillSource fill_source;
  imj::MetalFillKernel::build(fill_model, fill_source);
  const auto& planned_fills = fill_model.get_mi_layer_list().front().get_fill_rect_list();
  if (planned_fills.empty()) {
    return 1;
  }
  const auto forbidden = fill_source.obstacle().get_expand_rect(2);
  for (const auto& fill : planned_fills) {
    if (fill.is_intersect(forbidden)) {
      return 1;
    }
  }

  return 0;
}
