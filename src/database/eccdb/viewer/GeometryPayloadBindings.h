// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstddef>
#include <initializer_list>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include "geometry/GeometryPayloadService.h"
#include "database/Database.h"

namespace eccdb::viewer {
namespace py = pybind11;

struct NumericField { const char* name; py::dtype type; std::size_t offset; };

template <typename T>
py::array numericView(const std::vector<T>& values, py::object owner,
                       std::initializer_list<NumericField> fields)
{
  py::list names, formats, offsets;
  for (const auto& field : fields) {
    names.append(field.name);
    formats.append(field.type);
    offsets.append(field.offset);
  }
  py::dict specification;
  specification["names"] = names;
  specification["formats"] = formats;
  specification["offsets"] = offsets;
  specification["itemsize"] = sizeof(T);
  py::array result(py::dtype::from_args(specification),
                   {static_cast<py::ssize_t>(values.size())},
                   {static_cast<py::ssize_t>(sizeof(T))}, values.data(), owner);
  result.attr("setflags")(false);
  return result;
}

inline void bindGeometryPayload(py::module_& module)
{
  using H = GeometryShapeRef;
  using S = GeometryPayloadSegment;
  using R = GeometryPayloadRectangle;
  using P = GeometryPayloadPolygon;
  using A = GeometryPayloadPoint;
  using V = GeometryPayloadVia;
#define FIELD(Type, Member) NumericField{#Member, py::dtype::of<decltype(Type::Member)>(), offsetof(Type, Member)}
#define COORD(Type, Member, Name, Axis) NumericField{Name, py::dtype::of<int32_t>(), offsetof(Type, Member) + offsetof(Point, Axis)}
#define BOUND(Type, Member, Axis) NumericField{#Axis, py::dtype::of<int32_t>(), offsetof(Type, Member) + offsetof(Rect, Axis)}

  py::class_<GeometryPayloadRouteAttributes>(module, "RouteAttributes")
      .def_readonly("width", &GeometryPayloadRouteAttributes::width)
      .def_readonly("mask", &GeometryPayloadRouteAttributes::mask)
      .def_readonly("taper_rule", &GeometryPayloadRouteAttributes::taper_rule)
      .def_readonly("shape", &GeometryPayloadRouteAttributes::shape)
      .def_readonly("style", &GeometryPayloadRouteAttributes::style);

  py::class_<GeometryPayloadBatch, std::shared_ptr<GeometryPayloadBatch>>(module, "GeometryPayloadBatch")
      .def_readonly("geometry_epoch", &GeometryPayloadBatch::geometry_epoch)
      .def_property_readonly("shape_count", [](const GeometryPayloadBatch& batch) { return batch.headers.size(); })
      .def("headers_array", [](py::object self) {
        return numericView(self.cast<const GeometryPayloadBatch&>().headers, self,
            {FIELD(H,id), FIELD(H,kind), FIELD(H,owner_domain), FIELD(H,owner), FIELD(H,net), FIELD(H,wire), FIELD(H,layer),
             BOUND(H,bounds,ll_x), BOUND(H,bounds,ll_y), BOUND(H,bounds,ur_x), BOUND(H,bounds,ur_y), FIELD(H,flags), FIELD(H,source_path)});
      })
      .def("segments_array", [](py::object self) {
        return numericView(self.cast<const GeometryPayloadBatch&>().segments, self,
            {FIELD(S,header_index), COORD(S,p0,"x0",x), COORD(S,p0,"y0",y), COORD(S,p1,"x1",x), COORD(S,p1,"y1",y),
             FIELD(S,width), FIELD(S,begin_cap), FIELD(S,end_cap), FIELD(S,begin_flags), FIELD(S,end_flags), FIELD(S,begin_extension), FIELD(S,end_extension), FIELD(S,mask), FIELD(S,style)});
      })
      .def("rectangles_array", [](py::object self) {
        return numericView(self.cast<const GeometryPayloadBatch&>().rectangles, self,
            {FIELD(R,header_index), FIELD(R,layer), BOUND(R,rectangle,ll_x), BOUND(R,rectangle,ll_y),
             BOUND(R,rectangle,ur_x), BOUND(R,rectangle,ur_y), FIELD(R,via_layer), FIELD(R,flags), FIELD(R,mask), FIELD(R,spacing), FIELD(R,design_rule_width)});
      })
      .def("polygons_array", [](py::object self) {
        return numericView(self.cast<const GeometryPayloadBatch&>().polygons, self,
            {FIELD(P,header_index), FIELD(P,layer), FIELD(P,point_offset), FIELD(P,point_count), FIELD(P,flags), FIELD(P,mask), FIELD(P,spacing), FIELD(P,design_rule_width)});
      })
      .def("polygon_points_array", [](py::object self) {
        const auto& points = self.cast<const GeometryPayloadBatch&>().polygon_points;
        static_assert(sizeof(Point) == 2 * sizeof(int32_t));
        py::array result(py::dtype::of<int32_t>(), {static_cast<py::ssize_t>(points.size()), py::ssize_t{2}},
                         {static_cast<py::ssize_t>(sizeof(Point)), static_cast<py::ssize_t>(sizeof(int32_t))}, points.data(), self);
        result.attr("setflags")(false);
        return result;
      })
      .def("points_array", [](py::object self) {
        return numericView(self.cast<const GeometryPayloadBatch&>().points, self,
            {FIELD(A,header_index), FIELD(A,layer), COORD(A,point,"x",x), COORD(A,point,"y",y), FIELD(A,flags), FIELD(A,extension), FIELD(A,mask), FIELD(A,style)});
      })
      .def("vias_array", [](py::object self) {
        return numericView(self.cast<const GeometryPayloadBatch&>().vias, self,
            {FIELD(V,header_index), COORD(V,origin,"x",x), COORD(V,origin,"y",y), FIELD(V,tech_via), FIELD(V,design_via),
             FIELD(V,orientation), FIELD(V,flags), FIELD(V,top_mask), FIELD(V,cut_mask), FIELD(V,bottom_mask),
             FIELD(V,rows), FIELD(V,columns), FIELD(V,step_x), FIELD(V,step_y), FIELD(V,exit_layer)});
      })
      .def("route_attributes", [](const GeometryPayloadBatch& batch, uint32_t index) -> std::optional<GeometryPayloadRouteAttributes> {
        for (const auto& value : batch.segments) if (value.header_index == index) return value.attributes;
        for (const auto& value : batch.rectangles) if (value.header_index == index && value.route_attributes) return value.route_attributes;
        for (const auto& value : batch.polygons) if (value.header_index == index && value.route_attributes) return value.route_attributes;
        for (const auto& value : batch.points) if (value.header_index == index) return value.attributes;
        for (const auto& value : batch.vias) if (value.header_index == index) return value.route_attributes;
        return std::nullopt;
      });
  py::class_<GeometryPayloadService>(module, "GeometryPayloadService")
      .def(py::init([](Database& database) { return GeometryPayloadService{database.context()}; }), py::keep_alive<1,2>())
      .def("fetch_shapes", [](const GeometryPayloadService& service, const std::vector<uint64_t>& ids) {
        return std::make_shared<GeometryPayloadBatch>(service.fetchShapes(ids));
      }, py::arg("ids"), py::call_guard<py::gil_scoped_release>())
      .def("query_geometry", [](const GeometryPayloadService& service, Rect box, const GeometryQueryFilter& filter) {
        return std::make_shared<GeometryPayloadBatch>(service.queryGeometry(box, filter));
      }, py::arg("box"), py::arg("filter") = GeometryQueryFilter{}, py::call_guard<py::gil_scoped_release>());
#undef FIELD
#undef COORD
#undef BOUND
}

}  // namespace eccdb::viewer
