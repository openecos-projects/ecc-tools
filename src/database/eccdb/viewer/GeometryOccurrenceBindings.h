// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <vector>

#include "GeometryPayloadBindings.h"
#include "geometry/GeometryOccurrenceService.h"

namespace eccdb::viewer {

inline void bindGeometryOccurrences(py::module_& module)
{
  using H = GeometryOccurrenceHeader;
  using R = GeometryPayloadRectangle;
  using P = GeometryPayloadPolygon;
  using V = GeometryPayloadVia;
#define FIELD(T, member) NumericField{#member, py::dtype::of<decltype(T::member)>(), offsetof(T, member)}
#define BOUND(T, member, axis) NumericField{#axis, py::dtype::of<int32_t>(), offsetof(T, member) + offsetof(Rect, axis)}
#define COORD(T, member, name, axis) NumericField{name, py::dtype::of<int32_t>(), offsetof(T, member) + offsetof(Point, axis)}
  py::class_<GeometryOccurrencePayloadBatch, std::shared_ptr<GeometryOccurrencePayloadBatch>>(module, "GeometryOccurrenceBatch")
      .def_property_readonly("occurrence_count", [](const GeometryOccurrencePayloadBatch& b) { return b.headers.size(); })
      .def("headers_array", [](py::object self) {
        return numericView(self.cast<const GeometryOccurrencePayloadBatch&>().headers, self,
            {NumericField{"kind", py::dtype::of<uint8_t>(), offsetof(H,kind)}, FIELD(H,instance_term), FIELD(H,instance),
             FIELD(H,master_pin), BOUND(H,bounds,ll_x), BOUND(H,bounds,ll_y), BOUND(H,bounds,ur_x), BOUND(H,bounds,ur_y), FIELD(H,has_bounds)});
      })
      .def("rectangles_array", [](py::object self) {
        return numericView(self.cast<const GeometryOccurrencePayloadBatch&>().rectangles, self,
            {FIELD(R,header_index), FIELD(R,layer), BOUND(R,rectangle,ll_x), BOUND(R,rectangle,ll_y),
             BOUND(R,rectangle,ur_x), BOUND(R,rectangle,ur_y), FIELD(R,flags), FIELD(R,mask)});
      })
      .def("polygons_array", [](py::object self) {
        return numericView(self.cast<const GeometryOccurrencePayloadBatch&>().polygons, self,
            {FIELD(P,header_index), FIELD(P,layer), FIELD(P,point_offset), FIELD(P,point_count), FIELD(P,flags), FIELD(P,mask)});
      })
      .def("polygon_points_array", [](py::object self) {
        const auto& points = self.cast<const GeometryOccurrencePayloadBatch&>().polygon_points;
        py::array result(py::dtype::of<int32_t>(), {static_cast<py::ssize_t>(points.size()), py::ssize_t{2}},
                         {static_cast<py::ssize_t>(sizeof(Point)), static_cast<py::ssize_t>(sizeof(int32_t))}, points.data(), self);
        result.attr("setflags")(false);
        return result;
      })
      .def("vias_array", [](py::object self) {
        return numericView(self.cast<const GeometryOccurrencePayloadBatch&>().vias, self,
            {FIELD(V,header_index), COORD(V,origin,"x",x), COORD(V,origin,"y",y), FIELD(V,tech_via), FIELD(V,design_via), FIELD(V,orientation)});
      });
  py::class_<GeometryOccurrenceService>(module, "GeometryOccurrenceService")
      .def(py::init([](Database& database) { return GeometryOccurrenceService{database.context()}; }), py::keep_alive<1,2>())
      .def("fetch_master_pin", [](const GeometryOccurrenceService& service, uint64_t id) {
        return std::make_shared<GeometryOccurrencePayloadBatch>(service.fetchMasterPin(LibraryMasterPinId{static_cast<LibraryEntity>(id)}));
      }, py::arg("id"), py::call_guard<py::gil_scoped_release>())
      .def("fetch_instance_pin", [](const GeometryOccurrenceService& service, uint64_t id) {
        return std::make_shared<GeometryOccurrencePayloadBatch>(service.fetchInstancePin(DesignInstanceTermId{static_cast<DesignEntity>(id)}));
      }, py::arg("id"), py::call_guard<py::gil_scoped_release>())
      .def("fetch_instance_pins", [](const GeometryOccurrenceService& service, const std::vector<uint64_t>& ids) {
        std::vector<DesignInstanceId> instances;
        instances.reserve(ids.size());
        for (const auto id : ids) {
          instances.emplace_back(static_cast<DesignEntity>(id));
        }
        return std::make_shared<GeometryOccurrencePayloadBatch>(service.fetchInstancePins(instances));
      }, py::arg("ids"), py::call_guard<py::gil_scoped_release>());
#undef FIELD
#undef BOUND
#undef COORD
}

}  // namespace eccdb::viewer
