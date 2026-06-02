// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "tool_manager.h"

#include "builder.h"
#include "icts_io.h"
#include "idm.h"
#include "idrc_io.h"
#include "ieval_io.h"
#include "ifp_io.h"
#include "irt_io.h"
#include "ista_io.h"

namespace iplf {
ToolManager* ToolManager::_instance = nullptr;

ToolManager::ToolManager()
{
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// iDB
bool ToolManager::idbStart(std::string config_path)
{
  return dmInst->init(config_path);
}
bool ToolManager::idbSave(std::string name)
{
  return dmInst->save(name);
}
/// Eval
// int64_t ToolManager::evalTotalWL(const std::vector<eval::WLNet*>& net_list, const std::string& wl_type)
// {
//   EvalIO eval_io;
//   return eval_io.evalTotalWL(net_list, wl_type);
// }

// /// timing eval
// void ToolManager::estimateDelay(std::vector<eval::TimingNet*> timing_net_list, const char* sta_workspace_path, const char* sdc_file_path,
//                                 std::vector<const char*> lib_file_path_list)
// {
//   EvalIO eval_io;
//   eval_io.estimateDelay(timing_net_list, sta_workspace_path, sdc_file_path, lib_file_path_list);
// }
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// iFP
// bool ToolManager::autoRunFloorplan(std::string config)
// {
//   return fpInst->runFloorplan(config);
// }
// bool ToolManager::floorplanInit()
// {
//   return fpInst->initFunction();
// }
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// iPL
bool ToolManager::autoRunPlacer(std::string config, bool enableJsonOutput)
{
  return false;
}
bool ToolManager::runPlacerFiller(std::string config)
{
  return false;
}
bool ToolManager::runPlacerIncrementalFlow(std::string config)
{
  return false;
}
bool ToolManager::runPlacerIncrementalLegalization()
{
  return false;
}

bool ToolManager::checkLegality()
{
  return false;
}
bool ToolManager::reportPlacer()
{
  return false;
}

bool ToolManager::runAiPlacer(std::string config, std::string onnx_path, std::string normalization_path)
{
  return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// iNO
bool ToolManager::RunNOFixFanout(std::string config)
{
  return false;
}
bool ToolManager::RunNOFixIO(std::string config)
{
  return false;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// iTO
bool ToolManager::autoRunTO(std::string config)
{
  return false;
}
bool ToolManager::RunTODrv(std::string config)
{
  return false;
}
bool ToolManager::RunTODrvSpecialNet(std::string config, std::string net_name)
{
  return false;
}
bool ToolManager::RunTOHold(std::string config)
{
  return false;
}
bool ToolManager::RunTOSetup(std::string config)
{
  return false;
}
bool ToolManager::RunTOBuffering(std::string config, std::string net_name)
{
  return false;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// iCTS
bool ToolManager::autoRunCTS(std::string config, std::string work_dir)
{
  return ctsInst->runCTS(config, work_dir);
}

bool ToolManager::reportCTS(std::string path)
{
  return ctsInst->reportCTS(path);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// irt
bool ToolManager::autoRunRouter(std::string config_file_path)
{
  std::cout << "Start rt in ToolManager::autoRunRouter has been disabled!!!!!" << std::endl;
  std::cout << "Start rt in ToolManager::autoRunRouter has been disabled!!!!!" << std::endl;
  std::cout << "Start rt in ToolManager::autoRunRouter has been disabled!!!!!" << std::endl;
  std::cout << "Start rt in ToolManager::autoRunRouter has been disabled!!!!!" << std::endl;
  std::cout << "Start rt in ToolManager::autoRunRouter has been disabled!!!!!" << std::endl;
  std::cout << "Start rt in ToolManager::autoRunRouter has been disabled!!!!!" << std::endl;
  sleep(5);
  return true;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// idrc
bool ToolManager::autoRunDRC(std::string config, std::string path, bool has_init)
{
  return drcInst->runDRC(config, path, has_init);
}

bool ToolManager::readDrcDetailFromFile(std::string path)
{
  return drcInst->readDrcFromFile(path);
}

bool ToolManager::saveDrcDetailToFile(std::string path)
{
  return drcInst->saveDrcToFile(path);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// iSTA
bool ToolManager::autoRunSTA(std::string config)
{
  return staInst->autoRunSTA(config);
}

bool ToolManager::initSTA(std::string config)
{
  return staInst->initSTA(config);
}

bool ToolManager::runSTA(std::string config)
{
  return staInst->runSTA(config);
}

bool ToolManager::buildClockTree(std::string config, std::string data_path)
{
  bool is_ok;
  if (data_path.empty()) {
    is_ok = staInst->buildClockTree(config);
  } else {
    // read clock tree from file
    is_ok = ctsInst->readTreeDataFromFile(data_path);
  }

  return is_ok;
}

bool ToolManager::saveClockTree(std::string data_path)
{
  return ctsInst->saveTreeDataToFile(data_path);
}

/// iPW
bool ToolManager::autoRunPower(std::string config)
{
  return false;
}

/// iPNP
bool ToolManager::autoRunPNP(std::string config)
{
  return false;
}

}  // namespace iplf
