#include "py_replay.h"

#include "ReplayCapture.h"
#include "idm.h"

namespace python_interface {
std::string replayGeometry()
{
  auto* design = dmInst->get_idb_design();
  auto* layout = dmInst->get_idb_layout();
  if (!design || !layout) throw std::runtime_error("Replay requires a loaded database");
  return ecc::replay::geometry(*design, *layout).dump();
}
}
