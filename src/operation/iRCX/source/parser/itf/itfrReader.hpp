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
 * @file itfrReader.hpp
 * @brief Legacy ITF parser data structure implementation detail.
 */
#pragma once

#include <stdio.h>

#include "itfrSettings.hpp"

namespace itf
{

class itfiConductor;
class itfiDielectric;
class itfiVia;

// reader initialization. Must be called before itfrRead()
extern int itfrInit();
extern int itfrInitSession (int startSession = 1);
extern int itfrRead(FILE* file,
                    const char* file_name,
                    itfiUserData user_data);
extern int itfrClear();

// getter

extern const char* itfrFname();
extern itfiUserData itfrUserData();

// setter

extern void itfSetDebug(int);

// call back

enum class itfCallBackType {
  kNone = 0,
  kTechnologyCbType,
  kProcessFoundryCbType,
  kProcessNodeCbType,
  kProcessTypeCbType,
  kProcessVersionCbType,
  kProcessCornerCbType,
  kReferenceDirectionCbType,
  kGlobalTemperatureCbType,
  kBackgroundErCbType,
  kHalfNodeScaleFactorCbType,
  kUseSiDensityCbType,
  kDropFactorLateralSpacingCbType,
  kDielectricCbType,
  kConductorCbType,
  kViaCbType,

  itfrEndCbType
};

// A declaration of the signature of all callbacks that return nothing
typedef int (*itfrVoidCbFnType) (itfCallBackType,
                                void* ptr,
                                itfiUserData);

// A declaration of the signature of all callbacks that return a string
typedef int (*itfrStringCbFnType) (itfCallBackType,
                                  const char* string,
                                  itfiUserData);

// A declaration of the signature of all callbacks that return a integer
typedef int (*itfrIntegerCbFnType) (itfCallBackType,
                                    int num,
                                    itfiUserData);

// A declaration of the signature of all callbacks that return a double
typedef int (*itfrDoubleCbFnType) (itfCallBackType,
                                  double num,
                                  itfiUserData);

// A declaration of the signature of all callbacks that return a itfiDielectric
typedef int (*itfrDielectricCbFnType) ( itfCallBackType,
                                        itfiDielectric*,
                                        itfiUserData);

// A declaration of the signature of all callbacks that return a itfiConductor
typedef int (*itfrConductorCbFnType) (itfCallBackType,
                                      itfiConductor*,
                                      itfiUserData);

// A declaration of the signature of all callbacks that return a itfiVia
typedef int (*itfrViaCbFnType) (itfCallBackType,
                                itfiVia*,
                                itfiUserData);

extern void itfrSetTechnologyCb(itfrStringCbFnType);
extern void itfrSetProcessFoundryCb(itfrStringCbFnType);
extern void itfrSetProcessNodeCb(itfrDoubleCbFnType);
extern void itfrSetProcessTypeCb(itfrStringCbFnType);
extern void itfrSetProcessVersionCb(itfrDoubleCbFnType);
extern void itfrSetProcessCornerCb(itfrStringCbFnType);
extern void itfrSetReferenceDirectionCb(itfrStringCbFnType);
extern void itfrSetGlobalTemperatureCb(itfrDoubleCbFnType);
extern void itfrSetBackgroundErCb(itfrDoubleCbFnType);
extern void itfrSetHalfNodeScaleFactorCb(itfrDoubleCbFnType);
extern void itfrSetUseSiDensityCb(itfrIntegerCbFnType);
extern void itfrSetDropFactorLateralSpacingCb(itfrDoubleCbFnType);
extern void itfrSetConductorCb(itfrConductorCbFnType);
extern void itfrSetDielectricCb(itfrDielectricCbFnType);
extern void itfrSetViaCb(itfrViaCbFnType);

} // namespace itf
