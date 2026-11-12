// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace ipw::sdc {

enum class SdcObjectType
{
  kNone,
  kPort,
  kPin,
  kCell,
  kClock,
  kDesign,
  kLibrary,
  kLibCell,
  kLibPin,
  kDelayCorner,
  kNet
};

}  // namespace ipw::sdc
