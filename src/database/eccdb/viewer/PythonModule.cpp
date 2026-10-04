#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <memory>

#include "query/GeometryEditService.h"
#include "query/GeometryQueryService.h"

namespace py = pybind11;

PYBIND11_MODULE(eccdb_viewer, module)
{
  py::class_<eccdb::Database>(module, "Database")
      .def_static("open_binary", [](const std::string& technology,
                                     const std::string& library,
                                     const std::string& design) {
        return std::make_unique<eccdb::Database>(eccdb::Database::open(
            eccdb::Config{.input = eccdb::BinaryInput{.files = {technology, library, design}}}));
      })
      .def_static("open_lef_def", [](const std::vector<std::string>& lefs,
                                     const std::string& def_file) {
        eccdb::LefDefInput input;
        input.def_file = def_file;
        for (const auto& lef : lefs) input.lef_files.emplace_back(lef);
        return std::make_unique<eccdb::Database>(eccdb::Database::open(
            eccdb::Config{.input = std::move(input)}));
      });

  py::class_<eccdb::Point>(module, "Point")
      .def(py::init<int32_t, int32_t>())
      .def_readwrite("x", &eccdb::Point::x)
      .def_readwrite("y", &eccdb::Point::y);
  py::class_<eccdb::Rect>(module, "Rect")
      .def(py::init<int32_t, int32_t, int32_t, int32_t>())
      .def_readwrite("ll_x", &eccdb::Rect::ll_x)
      .def_readwrite("ll_y", &eccdb::Rect::ll_y)
      .def_readwrite("ur_x", &eccdb::Rect::ur_x)
      .def_readwrite("ur_y", &eccdb::Rect::ur_y);

  py::class_<eccdb::GeometryShapeRecord>(module, "ShapeRecord")
      .def_readonly("id", &eccdb::GeometryShapeRecord::id)
      .def_readonly("kind", &eccdb::GeometryShapeRecord::kind)
      .def_readonly("owner_domain", &eccdb::GeometryShapeRecord::owner_domain)
      .def_readonly("owner", &eccdb::GeometryShapeRecord::owner)
      .def_readonly("layer", &eccdb::GeometryShapeRecord::layer)
      .def_readonly("ll_x", &eccdb::GeometryShapeRecord::ll_x)
      .def_readonly("ll_y", &eccdb::GeometryShapeRecord::ll_y)
      .def_readonly("ur_x", &eccdb::GeometryShapeRecord::ur_x)
      .def_readonly("ur_y", &eccdb::GeometryShapeRecord::ur_y)
      .def_readonly("flags", &eccdb::GeometryShapeRecord::flags)
      .def_readonly("source_path", &eccdb::GeometryShapeRecord::source_path);

  py::class_<eccdb::GeometryShapeBatch>(module, "ShapeBatch")
      .def_readonly("records", &eccdb::GeometryShapeBatch::records)
      .def_readonly("geometry_epoch", &eccdb::GeometryShapeBatch::geometry_epoch)
      // One contiguous numeric view is convenient for a renderer and avoids
      // constructing one Python object per shape.  The array is a snapshot;
      // the EccDB registry remains the source of truth.
      .def("as_array", [](const eccdb::GeometryShapeBatch& batch) {
        constexpr py::ssize_t kColumns = 11;
        py::array_t<int64_t> result({static_cast<py::ssize_t>(batch.records.size()), kColumns});
        auto view = result.mutable_unchecked<2>();
        for (py::ssize_t row = 0; row < view.shape(0); ++row) {
          const auto& value = batch.records[static_cast<std::size_t>(row)];
          view(row, 0) = static_cast<int64_t>(value.id);
          view(row, 1) = value.kind;
          view(row, 2) = value.owner_domain;
          view(row, 3) = static_cast<int64_t>(value.owner);
          view(row, 4) = static_cast<int64_t>(value.layer);
          view(row, 5) = value.ll_x;
          view(row, 6) = value.ll_y;
          view(row, 7) = value.ur_x;
          view(row, 8) = value.ur_y;
          view(row, 9) = value.flags;
          view(row, 10) = value.source_path;
        }
        return result;
      });

  py::class_<eccdb::GeometryQueryService>(module, "GeometryQueryService")
      .def(py::init<eccdb::Database&>(), py::keep_alive<1, 2>())
      .def("query", &eccdb::GeometryQueryService::query)
      .def("hit_test", &eccdb::GeometryQueryService::hitTest, py::arg("point"), py::arg("tolerance") = 0)
      .def("contains", &eccdb::GeometryQueryService::contains)
      .def_property_readonly("geometry_epoch", &eccdb::GeometryQueryService::geometryEpoch);

  py::class_<eccdb::GeometryEditService>(module, "GeometryEditService")
      .def(py::init<eccdb::Database&>(), py::keep_alive<1, 2>())
      .def("move_shape", &eccdb::GeometryEditService::moveShape)
      .def("destroy_shape", &eccdb::GeometryEditService::destroyShape);

  // Keep the old read-only facade name as a source-compatible query alias for
  // clients that have not migrated yet. Mutations now live exclusively on the
  // explicit GeometryEditService boundary.
  module.attr("GeometryView") = module.attr("GeometryQueryService");
}
