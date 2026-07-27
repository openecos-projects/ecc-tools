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
/**
 * @file itfrReader.cpp
 * @brief Legacy ITF parser data structure implementation detail.
 */
#include "itfrReader.hpp"

#include "itfrCallBacks.hpp"
#include "itfrData.hpp"
#include "itfrSettings.hpp"
#include "itf_lex.hpp"
#include "itf_parser.hpp"

namespace itf
{

extern int itf_parse (void);

#define ITF_INIT itf_init(__FUNCTION__)

static const char* init_call_func = nullptr;

namespace
{

void resetParserSession()
{
  itfrData::clear();
  itfrCallBacks::clear();
  itfrSettings::clear();
  init_call_func = nullptr;
}

} // namespace

void itf_init(const char* func)
{
  if (itfSettings == nullptr) {
    itfrSettings::reset();
    init_call_func = func;
  }

  if (itfCallbacks == nullptr) {
    itfrCallBacks::reset();
    init_call_func = func;
  }
}

int
itfrInit()
{
  return itfrInitSession(0);
}

int
itfrInitSession (int startSession)
{
  if (startSession) {
    if (init_call_func) {
      fprintf(stderr,
              "ERROR: Attempt to call configuration function '%s' in ITF parser "
              "before lefrInit() call in session-based mode.\n",
              init_call_func);
      return 1;
    }

    itfrSettings::reset();
    itfrCallBacks::reset();

  } else {
    if (itfSettings == nullptr)  {
      itfrSettings::reset();
    }
    
    if (itfCallbacks == nullptr) {
      itfrCallBacks::reset();
    }
  }

  return 0;
}

int
itfrClear()
{
  resetParserSession();

  return 0;
}

// @param file readable itf file
// @param file_name itf file name
// @param user_data For extension
// @return 0 succeed
int
itfrRead(FILE* file, const char* file_name, itfiUserData user_data)
{
  if (file == nullptr) return 1;

  itfrData::reset();

  itfData->itf_file = file_name ? file_name : "";
  if (itfSettings) itfSettings->user_data = user_data;
  itf_restart(file);
  auto status = itf_parse();
  itf_lex_destroy();
  return status;
}

const char*
itfrFname()
{
  if (itfData) return itfData->itf_file.c_str();
  else return "";
}

itfiUserData
itfrUserData()
{
  if (itfSettings) return itfSettings->user_data;
  else return nullptr;
}

// This function is effective only when enabling itf/CMakeLists.txt
// 
//  bison_target(itfParser
//    ...
//    COMPILE_FLAGS "-t" ...  # <!>
//    ...
//  )
void
itfSetDebug(int flag)
{
  #ifdef ITF_DEBUG
    #if ITF_DEBUG
      extern int itf_debug;
      itf_debug = flag ? 1 : 0;
    #else
      if (flag) std::cout << "itfSetDebug() does not work" << std::endl;
    #endif
  #endif
  
  ++flag; // for warning
}

void
itfrSetTechnologyCb(itfrStringCbFnType f)
{
  ITF_INIT;
  itfCallbacks->technology_cb = f;
}

void
itfrSetProcessFoundryCb(itfrStringCbFnType f)
{
  ITF_INIT;
  itfCallbacks->process_foundry_cb = f;
}

void
itfrSetProcessNodeCb(itfrDoubleCbFnType f)
{
  ITF_INIT;
  itfCallbacks->process_node_cb = f;
}

void
itfrSetProcessTypeCb(itfrStringCbFnType f)
{
  ITF_INIT;
  itfCallbacks->process_type_cb = f;
}

void
itfrSetProcessVersionCb(itfrDoubleCbFnType f)
{
  ITF_INIT;
  itfCallbacks->process_version_cb = f;
}

void
itfrSetProcessCornerCb(itfrStringCbFnType f)
{
  ITF_INIT;
  itfCallbacks->process_corner_cb = f;
}

void
itfrSetReferenceDirectionCb(itfrStringCbFnType f)
{
  ITF_INIT;
  itfCallbacks->reference_direction_cb = f;
}

void
itfrSetGlobalTemperatureCb(itfrDoubleCbFnType f)
{
  ITF_INIT;
  itfCallbacks->global_temperature_cb = f;
}

void
itfrSetBackgroundErCb(itfrDoubleCbFnType f)
{
  ITF_INIT;
  itfCallbacks->background_er_cb = f;
}

void
itfrSetHalfNodeScaleFactorCb(itfrDoubleCbFnType f)
{
  ITF_INIT;
  itfCallbacks->half_node_scale_factor_cb = f;
}

void
itfrSetUseSiDensityCb(itfrIntegerCbFnType f)
{
  ITF_INIT;
  itfCallbacks->use_si_density_cb = f;
}

void
itfrSetDropFactorLateralSpacingCb(itfrDoubleCbFnType f)
{
  ITF_INIT;
  itfCallbacks->drop_factor_lateral_spacing_cb = f;
}


void
itfrSetDielectricCb(itfrDielectricCbFnType f)
{
  ITF_INIT;
  itfCallbacks->dielectric_cb = f;
}

void
itfrSetConductorCb(itfrConductorCbFnType f)
{
  ITF_INIT;
  itfCallbacks->conductor_cb = f;
}

void
itfrSetViaCb(itfrViaCbFnType f)
{
  ITF_INIT;
  itfCallbacks->via_cb = f;
}

} // namespace itf
