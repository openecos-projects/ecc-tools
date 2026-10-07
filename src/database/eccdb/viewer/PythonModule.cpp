#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "query/GeometryEditService.h"
#include "query/GeometryQueryService.h"
#include "GeometryPayloadBindings.h"
#include "GeometryOccurrenceBindings.h"
#include "DesignModelBindings.h"

namespace py = pybind11;

PYBIND11_MODULE(eccdb_viewer, module)
{
  py::class_<eccdb::Database>(module, "Database")
      .def_static("open_binary", [](const std::string& technology,
                                     const std::string& library,
                                     const std::string& design) {
        return std::make_unique<eccdb::Database>(eccdb::Database::open(
            eccdb::Config{.input = eccdb::BinaryInput{.files = {technology, library, design}}}));
      }, py::call_guard<py::gil_scoped_release>())
      .def_static("open_lef_def", [](const std::vector<std::string>& lefs,
                                     const std::string& def_file) {
        eccdb::LefDefInput input;
        input.def_file = def_file;
        for (const auto& lef : lefs) input.lef_files.emplace_back(lef);
        return std::make_unique<eccdb::Database>(eccdb::Database::open(
            eccdb::Config{.input = std::move(input)}));
      }, py::call_guard<py::gil_scoped_release>())
      .def("write_def", [](const eccdb::Database& database, const std::string& path) {
        database.writeDef(path);
      }, py::arg("path"), py::call_guard<py::gil_scoped_release>())
      .def("write_binary", [](const eccdb::Database& database, const std::string& technology,
                               const std::string& library, const std::string& design) {
        database.writeBinary(eccdb::BinaryFiles{technology, library, design});
      }, py::arg("technology"), py::arg("library"), py::arg("design"), py::call_guard<py::gil_scoped_release>());

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
      .def_readonly("net", &eccdb::GeometryShapeRecord::net)
      .def_readonly("wire", &eccdb::GeometryShapeRecord::wire)
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
      // the EccDB registry remains the source of truth. Columns are
      // id,kind,owner_domain,owner,layer,net,wire,ll_x,ll_y,ur_x,ur_y,flags,source_path.
      .def("as_array", [](const eccdb::GeometryShapeBatch& batch) {
        constexpr py::ssize_t kColumns = 13;
        py::array_t<int64_t> result({static_cast<py::ssize_t>(batch.records.size()), kColumns});
        auto view = result.mutable_unchecked<2>();
        for (py::ssize_t row = 0; row < view.shape(0); ++row) {
          const auto& value = batch.records[static_cast<std::size_t>(row)];
          view(row, 0) = static_cast<int64_t>(value.id);
          view(row, 1) = value.kind;
          view(row, 2) = value.owner_domain;
          view(row, 3) = static_cast<int64_t>(value.owner);
          view(row, 4) = static_cast<int64_t>(value.layer);
          view(row, 5) = static_cast<int64_t>(value.net);
          view(row, 6) = static_cast<int64_t>(value.wire);
          view(row, 7) = value.ll_x;
          view(row, 8) = value.ll_y;
          view(row, 9) = value.ur_x;
          view(row, 10) = value.ur_y;
          view(row, 11) = value.flags;
          view(row, 12) = value.source_path;
        }
        return result;
      });

  py::class_<eccdb::GeometryQueryFilter>(module, "GeometryQueryFilter")
      .def(py::init<>())
      .def_readwrite("layer", &eccdb::GeometryQueryFilter::layer)
      .def_readwrite("layers", &eccdb::GeometryQueryFilter::layers)
      .def_readwrite("kind_owner_masks", &eccdb::GeometryQueryFilter::kind_owner_masks)
      .def_readwrite("kind_mask", &eccdb::GeometryQueryFilter::kind_mask)
      .def_property(
          "owner_domain",
          [](const eccdb::GeometryQueryFilter& filter) {
            return static_cast<uint8_t>(filter.owner_domain);
          },
          [](eccdb::GeometryQueryFilter& filter, uint8_t value) {
            filter.owner_domain = static_cast<eccdb::GeometryOwnerDomain>(value);
          })
      .def_readwrite("owner", &eccdb::GeometryQueryFilter::owner)
      .def_readwrite("net", &eccdb::GeometryQueryFilter::net)
      .def_readwrite("wire", &eccdb::GeometryQueryFilter::wire)
      .def_readwrite("include_local_vias", &eccdb::GeometryQueryFilter::include_local_vias);

  py::class_<eccdb::GeometryShapeRef>(module, "ShapeRef")
      .def_readonly("id", &eccdb::GeometryShapeRef::id)
      .def_readonly("kind", &eccdb::GeometryShapeRef::kind)
      .def_readonly("owner_domain", &eccdb::GeometryShapeRef::owner_domain)
      .def_readonly("owner", &eccdb::GeometryShapeRef::owner)
      .def_readonly("net", &eccdb::GeometryShapeRef::net)
      .def_readonly("wire", &eccdb::GeometryShapeRef::wire)
      .def_readonly("layer", &eccdb::GeometryShapeRef::layer)
      .def_readonly("bounds", &eccdb::GeometryShapeRef::bounds)
      .def_readonly("flags", &eccdb::GeometryShapeRef::flags)
      .def_readonly("source_path", &eccdb::GeometryShapeRef::source_path);

  py::class_<eccdb::GeometryShapeRefBatch>(module, "ShapeRefBatch")
      .def_readonly("records", &eccdb::GeometryShapeRefBatch::records)
      .def_readonly("geometry_epoch", &eccdb::GeometryShapeRefBatch::geometry_epoch);

  py::class_<eccdb::GeometryOverviewLayerRecord>(module, "OverviewLayerRecord")
      .def_readonly("layer", &eccdb::GeometryOverviewLayerRecord::layer)
      .def_readonly("shape_count", &eccdb::GeometryOverviewLayerRecord::shape_count)
      .def_readonly("bounds", &eccdb::GeometryOverviewLayerRecord::bounds)
      .def_readonly("occupancy", &eccdb::GeometryOverviewLayerRecord::occupancy)
      .def("occupied_cell", [](const eccdb::GeometryOverviewLayerRecord& summary,
                                uint32_t column,
                                uint32_t row) {
        if (column >= 16u || row >= 16u) return false;
        const auto bit = row * 16u + column;
        return (summary.occupancy[bit / 64u] & (uint64_t{1} << (bit % 64u))) != 0;
      });

  py::class_<eccdb::GeometryOverviewTileRecord>(module, "OverviewTileRecord")
      .def_readonly("level", &eccdb::GeometryOverviewTileRecord::level)
      .def_readonly("tile_x", &eccdb::GeometryOverviewTileRecord::tile_x)
      .def_readonly("tile_y", &eccdb::GeometryOverviewTileRecord::tile_y)
      .def_readonly("bounds", &eccdb::GeometryOverviewTileRecord::bounds)
      .def_readonly("occupied", &eccdb::GeometryOverviewTileRecord::occupied)
      .def_readonly("shape_count", &eccdb::GeometryOverviewTileRecord::shape_count)
      .def_readonly("covered_area", &eccdb::GeometryOverviewTileRecord::covered_area)
      .def_readonly("occupancy", &eccdb::GeometryOverviewTileRecord::occupancy)
      .def_readonly("layers", &eccdb::GeometryOverviewTileRecord::layers)
      .def_readonly("geometry_epoch", &eccdb::GeometryOverviewTileRecord::geometry_epoch)
      .def("occupied_cell", [](const eccdb::GeometryOverviewTileRecord& tile,
                                uint32_t column,
                                uint32_t row) {
        if (column >= 16u || row >= 16u) return false;
        const auto bit = row * 16u + column;
        return (tile.occupancy[bit / 64u] & (uint64_t{1} << (bit % 64u))) != 0;
      });

  py::class_<eccdb::GeometryOverviewBatch>(module, "OverviewBatch")
      .def_readonly("tiles", &eccdb::GeometryOverviewBatch::tiles)
      .def_readonly("level", &eccdb::GeometryOverviewBatch::level)
      .def_readonly("base_tile_size", &eccdb::GeometryOverviewBatch::base_tile_size)
      .def_readonly("geometry_epoch", &eccdb::GeometryOverviewBatch::geometry_epoch)
      // Columns: level, tile_x, tile_y, tile bounds (4), occupied bounds (4),
      // shape count, covered area, occupancy words (4), tile epoch.
      .def("as_array", [](const eccdb::GeometryOverviewBatch& batch) {
        constexpr py::ssize_t kColumns = 18;
        py::array_t<int64_t> result({static_cast<py::ssize_t>(batch.tiles.size()), kColumns});
        auto view = result.mutable_unchecked<2>();
        for (py::ssize_t row = 0; row < view.shape(0); ++row) {
          const auto& tile = batch.tiles[static_cast<std::size_t>(row)];
          view(row, 0) = tile.level;
          view(row, 1) = tile.tile_x;
          view(row, 2) = tile.tile_y;
          view(row, 3) = tile.bounds.ll_x;
          view(row, 4) = tile.bounds.ll_y;
          view(row, 5) = tile.bounds.ur_x;
          view(row, 6) = tile.bounds.ur_y;
          view(row, 7) = tile.occupied.ll_x;
          view(row, 8) = tile.occupied.ll_y;
          view(row, 9) = tile.occupied.ur_x;
          view(row, 10) = tile.occupied.ur_y;
          view(row, 11) = tile.shape_count;
          view(row, 12) = static_cast<int64_t>(tile.covered_area);
          view(row, 13) = static_cast<int64_t>(tile.occupancy[0]);
          view(row, 14) = static_cast<int64_t>(tile.occupancy[1]);
          view(row, 15) = static_cast<int64_t>(tile.occupancy[2]);
          view(row, 16) = static_cast<int64_t>(tile.occupancy[3]);
          view(row, 17) = static_cast<int64_t>(tile.geometry_epoch);
        }
        return result;
      });

  py::class_<eccdb::GeometryQueryService>(module, "GeometryQueryService")
      .def(py::init<eccdb::Database&>(), py::keep_alive<1, 2>())
      .def("query", &eccdb::GeometryQueryService::query, py::call_guard<py::gil_scoped_release>())
      .def("query_refs", &eccdb::GeometryQueryService::queryRefs,
           py::arg("box"), py::arg("filter") = eccdb::GeometryQueryFilter{}, py::call_guard<py::gil_scoped_release>())
      .def("design_bounds", &eccdb::GeometryQueryService::designBounds)
      .def("warmup_index", &eccdb::GeometryQueryService::warmupIndex, py::call_guard<py::gil_scoped_release>())
      .def_property_readonly("index_ready", &eccdb::GeometryQueryService::indexReady)
      .def_property_readonly("indexed_shape_count", &eccdb::GeometryQueryService::indexedShapeCount)
      .def("query_overview", &eccdb::GeometryQueryService::queryOverview,
           py::arg("box"), py::arg("level") = 0, py::call_guard<py::gil_scoped_release>())
      // Import only detached tile metadata from a GUI sidecar.  The Python
      // adapter passes tuples to keep this boundary independent of Qt types;
      // no canonical Shape payload crosses the binding.
      .def("seed_overview", [](eccdb::GeometryQueryService& service,
                                uint64_t epoch,
                                uint8_t level,
                                int64_t base_tile_size,
                                const py::sequence& records) {
        auto read_rect = [](py::handle value) {
          const auto rectangle = py::reinterpret_borrow<py::sequence>(value);
          if (py::len(rectangle) < 4) throw py::value_error("overview rectangle needs four coordinates");
          const auto coordinate = [&](py::ssize_t index) {
            return static_cast<int32_t>(py::cast<int64_t>(rectangle[index]));
          };
          return eccdb::Rect{coordinate(0), coordinate(1), coordinate(2), coordinate(3)};
        };
        auto read_occupancy = [](py::handle value) {
          std::array<uint64_t, 4> occupancy{};
          const auto words = py::reinterpret_borrow<py::sequence>(value);
          const auto count = std::min<py::ssize_t>(4, py::len(words));
          for (py::ssize_t index = 0; index < count; ++index)
            occupancy[static_cast<std::size_t>(index)] = py::cast<uint64_t>(words[index]);
          return occupancy;
        };
        eccdb::GeometryOverviewBatch batch;
        batch.geometry_epoch = epoch;
        batch.level = level;
        batch.base_tile_size = base_tile_size;
        batch.tiles.reserve(static_cast<std::size_t>(py::len(records)));
        for (const auto item : records) {
          const auto record = py::reinterpret_borrow<py::sequence>(item);
          if (py::len(record) < 10) throw py::value_error("overview tile record is incomplete");
          eccdb::GeometryOverviewTileRecord tile;
          tile.level = py::cast<uint8_t>(record[0]);
          tile.tile_x = py::cast<int64_t>(record[1]);
          tile.tile_y = py::cast<int64_t>(record[2]);
          tile.bounds = read_rect(record[3]);
          tile.occupied = read_rect(record[4]);
          tile.shape_count = py::cast<uint32_t>(record[5]);
          tile.covered_area = py::cast<uint64_t>(record[6]);
          tile.occupancy = read_occupancy(record[7]);
          tile.geometry_epoch = py::cast<uint64_t>(record[9]);
          const auto layers = py::reinterpret_borrow<py::sequence>(record[8]);
          tile.layers.reserve(static_cast<std::size_t>(py::len(layers)));
          for (const auto layer_item : layers) {
            const auto layer_record = py::reinterpret_borrow<py::sequence>(layer_item);
            if (py::len(layer_record) < 4) throw py::value_error("overview layer record is incomplete");
            tile.layers.push_back(eccdb::GeometryOverviewLayerRecord{
                .layer = py::cast<uint64_t>(layer_record[0]),
                .shape_count = py::cast<uint32_t>(layer_record[1]),
                .bounds = read_rect(layer_record[2]),
                .occupancy = read_occupancy(layer_record[3]),
            });
          }
          batch.tiles.push_back(std::move(tile));
        }
        py::gil_scoped_release release;
        return service.seedOverview(batch);
      }, py::arg("epoch"), py::arg("level"), py::arg("base_tile_size"), py::arg("tiles"))
      .def("hit_test", &eccdb::GeometryQueryService::hitTest, py::arg("point"), py::arg("tolerance") = 0,
           py::arg("filter") = eccdb::GeometryQueryFilter{}, py::call_guard<py::gil_scoped_release>())
      .def("query_shape_ids_array", [](eccdb::GeometryQueryService& service,
                                         eccdb::Rect box,
                                         const eccdb::GeometryQueryFilter& filter) {
        std::vector<uint64_t> ids;
        {
          py::gil_scoped_release release;
          ids = service.queryShapeIds(box, filter);
        }
        const py::array::ShapeContainer shape{static_cast<py::ssize_t>(ids.size())};
        const py::array::StridesContainer strides{static_cast<py::ssize_t>(sizeof(uint64_t))};
        py::array_t<uint64_t> result(shape, strides);
        auto output = result.mutable_unchecked<1>();
        for (py::ssize_t index = 0; index < output.shape(0); ++index)
          output(index) = ids[static_cast<std::size_t>(index)];
        return result;
      }, py::arg("box"), py::arg("filter") = eccdb::GeometryQueryFilter{})
      .def("intersect_shapes", &eccdb::GeometryQueryService::intersectShapes, py::arg("box"),
           py::arg("filter") = eccdb::GeometryQueryFilter{}, py::call_guard<py::gil_scoped_release>())
      // Marquee selection can return hundreds of thousands of stable IDs.
      // The regular STL binding materializes one Python int per ID before the
      // GUI worker can consume it.  Keep the native query identical, but
      // expose a contiguous uint64 array for the high-volume GUI path.
      .def("intersect_shapes_array", [](eccdb::GeometryQueryService& service,
                                          eccdb::Rect box,
                                          const eccdb::GeometryQueryFilter& filter) {
        std::vector<uint64_t> ids;
        {
          py::gil_scoped_release release;
          ids = service.intersectShapes(box, filter);
        }
        // intersectShapes() already emits at most one ID per matched header.
        // Preserve that native order and avoid an O(n log n) sort on the
        // large marquee hot path; the GUI selection contract consumes IDs.
        // Use an explicit one-dimensional shape.  Passing ``ids.size()`` to
        // array_t's scalar constructor is ambiguous across pybind11 versions
        // and can produce a zero-filled scalar/incorrect view instead of an
        // array with one element per ID.
        const py::array::ShapeContainer shape{static_cast<py::ssize_t>(ids.size())};
        const py::array::StridesContainer strides{static_cast<py::ssize_t>(sizeof(uint64_t))};
        py::array_t<uint64_t> result(shape, strides);
        auto output = result.mutable_unchecked<1>();
        for (py::ssize_t index = 0; index < output.shape(0); ++index) {
          output(index) = ids[static_cast<std::size_t>(index)];
        }
        return result;
      }, py::arg("box"), py::arg("filter") = eccdb::GeometryQueryFilter{})
      .def("contains", &eccdb::GeometryQueryService::contains)
      .def_property_readonly("geometry_epoch", &eccdb::GeometryQueryService::geometryEpoch);

  py::class_<eccdb::GeometryEditService>(module, "GeometryEditService")
      .def(py::init<eccdb::Database&>(), py::keep_alive<1, 2>())
      .def("move_shape", &eccdb::GeometryEditService::moveShape, py::call_guard<py::gil_scoped_release>())
      .def("remove_shape", &eccdb::GeometryEditService::removeShape, py::call_guard<py::gil_scoped_release>())
      .def("restore_shape", &eccdb::GeometryEditService::restoreShape, py::call_guard<py::gil_scoped_release>())
      .def("remove_shapes", [](eccdb::GeometryEditService& service, const std::vector<uint64_t>& ids) { return service.removeShapes(ids); },
           py::call_guard<py::gil_scoped_release>())
      .def("restore_shapes", [](eccdb::GeometryEditService& service, const std::vector<uint64_t>& ids) { return service.restoreShapes(ids); },
           py::call_guard<py::gil_scoped_release>())
      .def("destroy_shape", &eccdb::GeometryEditService::destroyShape, py::call_guard<py::gil_scoped_release>());

  eccdb::viewer::bindGeometryPayload(module);
  eccdb::viewer::bindGeometryOccurrences(module);
  eccdb::viewer::bindDesignModel(module);

  // Keep the old read-only facade name as a source-compatible query alias for
  // clients that have not migrated yet. Mutations now live exclusively on the
  // explicit GeometryEditService boundary.
  module.attr("GeometryView") = module.attr("GeometryQueryService");
}
