// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ScriptEngine.hh"

namespace ista::sdc {

enum class SdcObjectType
{
  kNone,
  kPort,
  kNet,
  kPin,
  kCell,
  kClock,
  kLibrary,
  kLibCell,
  kLibPin,
  kDesign,
  kDelayCorner
};

// Keep query object identity without changing the visible Tcl name.
class SdcObject
{
 public:
  // A clock-only client may parse literal identities before loading a design.
  // The option belongs to this interpreter; normal STA queries remain strict.
  static void allowUnboundObjects(Tcl_Interp* interp, bool allow) { Tcl_SetAssocData(interp, "ista.sdc.unbound_objects", nullptr, allow ? interp : nullptr); }
  static bool allowsUnboundObjects(Tcl_Interp* interp) { return Tcl_GetAssocData(interp, "ista.sdc.unbound_objects", nullptr) != nullptr; }
  static std::vector<Tcl_Obj*> collection(Tcl_Obj* object)
  {
    std::vector<Tcl_Obj*> result;
    const auto* list_type = Tcl_GetObjType("list");
    std::function<void(Tcl_Obj*, bool, int)> append = [&](Tcl_Obj* value, bool root, int depth) {
      if (depth > 64)
        throw std::invalid_argument("object collection nesting exceeds 64 levels");
      if (SdcObject::getType(value) != SdcObjectType::kNone || (!root && value->typePtr != list_type)) {
        result.push_back(value);
        return;
      }
      int count = 0;
      Tcl_Obj** elements = nullptr;
      if (Tcl_ListObjGetElements(nullptr, value, &count, &elements) != TCL_OK)
        throw std::invalid_argument("invalid object collection");
      for (int index = 0; index < count; ++index)
        append(elements[index], false, depth + 1);
    };
    append(object, true, 0);
    return result;
  }

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
