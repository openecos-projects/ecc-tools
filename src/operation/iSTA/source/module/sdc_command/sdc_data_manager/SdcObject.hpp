// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include "STAHeader.hpp"
#include "ScriptEngine.hh"
#include "SdcObjectType.hpp"

namespace ista::sdc {

// Keep query object identity without changing the visible Tcl name.
class SdcObject
{
 public:
  static Tcl_Obj* create(const std::string& name, SdcObjectType type)
  {
    Tcl_Obj* object = Tcl_NewStringObj(name.c_str(), static_cast<int>(name.size()));
    if (type != SdcObjectType::kNone) {
      object->internalRep.longValue = static_cast<long>(type);
      object->typePtr = getObjectType();
    }
    return object;
  }

  static SdcObjectType getType(Tcl_Obj* object)
  {
    return object->typePtr == getObjectType() ? static_cast<SdcObjectType>(object->internalRep.longValue) : SdcObjectType::kNone;
  }

 private:
  static const Tcl_ObjType* getObjectType()
  {
    // Objects always retain their immutable string representation. Only the
    // scalar type tag is duplicated; no Database pointer or allocation is held.
    static const Tcl_ObjType object_type = {"ista_sdc_object", nullptr, duplicate, nullptr, nullptr};
    return &object_type;
  }

  static void duplicate(Tcl_Obj* source, Tcl_Obj* target)
  {
    target->internalRep = source->internalRep;
    target->typePtr = source->typePtr;
  }
};

}  // namespace ista::sdc
