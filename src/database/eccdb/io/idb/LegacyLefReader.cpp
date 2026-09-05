#include "idb/LegacyLefReader.h"

#include "IdbCellMaster.h"
#include "IdbObs.h"
#include "IdbTerm.h"
#include "lef_read.h"

namespace eccdb {

LegacyLefReader::LegacyLefReader() = default;
LegacyLefReader::~LegacyLefReader() = default;

::idb::IdbLefService* LegacyLefReader::buildLef(std::vector<std::string>& files, bool technology_file)
{
  if (!_service || technology_file) {
    _service = std::make_unique<::idb::IdbLefService>();
  }

  if (_service->LefFileInit(files) != ::idb::IdbLefServiceResult::kServiceSuccess) {
    return nullptr;
  }
  for (const auto& file : files) {
    ::idb::LefRead reader(_service.get());
    if (!reader.createDb(file.c_str())) {
      return nullptr;
    }
  }
  if (!technology_file) {
    normalizeMacroOrigins();
  }
  return _service.get();
}

void LegacyLefReader::updateLefData()
{
  normalizeMacroOrigins();
}

void LegacyLefReader::normalizeMacroOrigins()
{
  auto* layout = _service ? _service->get_layout() : nullptr;
  if (layout == nullptr) {
    return;
  }

  for (auto* master : layout->get_cell_master_list()->get_cell_master()) {
    if (master->get_origin_x() == 0 && master->get_origin_y() == 0) {
      continue;
    }
    for (auto* term : master->get_term_list()) {
      for (auto* port : term->get_port_list()) {
        for (auto* shape : port->get_layer_shape()) {
          if (shape == nullptr) {
            continue;
          }
          for (auto* rect : shape->get_rect_list()) {
            rect->moveByStep(master->get_origin_x(), master->get_origin_y());
          }
        }
      }
    }
    for (auto* obs : master->get_obs_list()) {
      for (auto* layer_shape : obs->get_obs_layer_list()) {
        auto* shape = layer_shape->get_shape();
        if (shape == nullptr) {
          continue;
        }
        for (auto* rect : shape->get_rect_list()) {
          rect->moveByStep(master->get_origin_x(), master->get_origin_y());
        }
      }
    }
  }
}

}  // namespace eccdb
