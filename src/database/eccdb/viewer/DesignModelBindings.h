#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "query/DesignModelService.h"

namespace eccdb::viewer {
namespace py = pybind11;

inline void bindDesignModel(py::module_& module)
{
  py::class_<ModelObjectRef>(module, "ModelObjectRef")
      .def(py::init<std::string,uint64_t>(), py::arg("kind"), py::arg("id"))
      .def_readonly("kind", &ModelObjectRef::kind)
      .def_readonly("id", &ModelObjectRef::id);
  py::class_<ModelEntity>(module, "ModelEntity")
      .def_readonly("object", &ModelEntity::object)
      .def_property_readonly("kind", [](const ModelEntity& value) { return value.object.kind; })
      .def_property_readonly("id", [](const ModelEntity& value) { return value.object.id; })
      .def_readonly("name", &ModelEntity::name)
      .def_readonly("parent", &ModelEntity::parent)
      .def_readonly("master", &ModelEntity::master)
      .def_readonly("net", &ModelEntity::net)
      .def_readonly("bounds", &ModelEntity::bounds)
      .def_readonly("relations", &ModelEntity::relations)
      .def_readonly("layer", &ModelEntity::layer)
      .def_readonly("status", &ModelEntity::status)
      .def_readonly("orientation", &ModelEntity::orientation)
      .def_readonly("geometry_kind", &ModelEntity::geometry_kind)
      .def_readonly("has_origin", &ModelEntity::has_origin)
      .def_readonly("origin", &ModelEntity::origin)
      .def_readonly("flags", &ModelEntity::flags);
  py::class_<ModelEntityPage>(module, "ModelEntityPage")
      .def_readonly("records", &ModelEntityPage::records)
      .def_readonly("has_more", &ModelEntityPage::has_more)
      .def_property_readonly("next_offset", [](const ModelEntityPage& value) -> std::optional<uint64_t> {
        return value.has_more ? std::optional<uint64_t>{value.next_offset} : std::nullopt;
      });
  py::class_<ModelLayerRecord>(module, "ModelLayerRecord")
      .def_readonly("id", &ModelLayerRecord::id)
      .def_readonly("name", &ModelLayerRecord::name)
      .def_readonly("sequence_index", &ModelLayerRecord::sequence_index)
      .def_readonly("routing_level", &ModelLayerRecord::routing_level)
      .def_readonly("mask_count", &ModelLayerRecord::mask_count)
      .def_readonly("kind", &ModelLayerRecord::kind)
      .def_readonly("direction", &ModelLayerRecord::direction)
      .def_readonly("flags", &ModelLayerRecord::flags)
      .def_readonly("width", &ModelLayerRecord::width)
      .def_readonly("pitch_x", &ModelLayerRecord::pitch_x)
      .def_readonly("pitch_y", &ModelLayerRecord::pitch_y)
      .def_readonly("offset_x", &ModelLayerRecord::offset_x)
      .def_readonly("offset_y", &ModelLayerRecord::offset_y);
  py::class_<ModelTrackGridRecord>(module, "ModelTrackGridRecord")
      .def_readonly("source_id", &ModelTrackGridRecord::source_id)
      .def_readonly("layer", &ModelTrackGridRecord::layer)
      .def_readonly("axis", &ModelTrackGridRecord::axis)
      .def_readonly("start", &ModelTrackGridRecord::start)
      .def_readonly("track_count", &ModelTrackGridRecord::track_count)
      .def_readonly("step", &ModelTrackGridRecord::step)
      .def_readonly("flags", &ModelTrackGridRecord::flags)
      .def_readonly("mask", &ModelTrackGridRecord::mask);
  py::class_<DesignModelService>(module, "DesignModelService")
      .def(py::init<Database&>(), py::keep_alive<1,2>())
      .def("list", &DesignModelService::list, py::arg("kind"), py::arg("offset") = 0, py::arg("limit") = 256, py::arg("parent") = std::nullopt)
      .def("list_ids", &DesignModelService::listIds, py::arg("kind"), py::arg("offset") = 0, py::arg("limit") = 256, py::arg("parent") = std::nullopt)
      .def("describe", &DesignModelService::describe)
      .def("find", &DesignModelService::find, py::arg("kind"), py::arg("name"), py::arg("parent") = std::nullopt)
      .def("layers", &DesignModelService::layers)
      .def("track_grids", &DesignModelService::trackGrids, py::arg("layer") = std::nullopt)
      .def("list_placed_instances", &DesignModelService::listPlacedInstances, py::arg("viewport"), py::arg("offset") = 0,
           py::arg("limit") = 256, py::call_guard<py::gil_scoped_release>());
}
}  // namespace eccdb::viewer
