/**
 * @file   PyPlaceDB.cpp
 * @author Yibo Lin
 * @date   Apr 2020
 * @brief  Placement database for python
 */

#include "macro_ista_interface.h"
// #include "ContestDriver.h"
// #include "PowerEngine.hh"
// #include "Power.hh"
#include <boost/polygon/polygon.hpp>
#include <vector>

namespace python_interface {

bool runMacroSTA(const std::string& def_name)
{
  return dmInst->saveMacroTCL(def_name);
}

bool initMacroSTA(const std::string& def_name)
{
  return dmInst->saveMacroTCL(def_name);
}
void MacroISTAInterface::initTimingEngine()
{
  _idb_builder = _db->get_idb_builder();
  std::cout << "ok1 " << std::endl;
  _timing_engine = ista::TimingEngine::getOrCreateTimingEngine();
  std::cout << "new sdc: " << _sdc_file << std::endl;
  std::cout << "new lib size: " << _lib_files.size() << std::endl;
  for (auto lib_file : _lib_files) {
    std::cout << "lib file : " << lib_file << std::endl;
  }

  _timing_engine->readLiberty(_lib_files);

  // auto* power_engine = ipower::PowerEngine::getOrCreatePowerEngine();
  
  auto db_adapter = std::make_unique<ista::TimingIDBAdapter>(_timing_engine->get_ista());
  db_adapter->set_idb(_idb_builder);
  db_adapter->convertDBToTimingNetlist();
  _timing_engine->set_db_adapter(std::move(db_adapter));
  _timing_engine->buildGraph();
  _timing_engine->readSdc(_sdc_file.c_str());
}

void MacroISTAInterface::initPaths()
{
  _db->get_idb_design()->get_instance_list(); 
}

}  // namespace python_interface