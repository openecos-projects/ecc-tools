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
#pragma once

#include <set>
#include <string>
#include <vector>

#include "Database.hpp"
#include "ObjectCatalog.hpp"
struct Tcl_Interp;
struct Tcl_Obj;
namespace ista::sdc {
QueryObjectType queryObjectType(Tcl_Obj* object);
// Enabled only for a clock-subset interpreter with no loaded network.
bool deferObjectBinding(Tcl_Interp* interp, const Database& database);

struct ObjectQueryOptions
{
  bool regexp = false;
  bool nocase = false;
  bool exact = false;
  bool hierarchical = false;
  bool leaf = false;
  std::string filter;
  bool has_of_objects = false;
  std::vector<std::pair<QueryObjectType, std::string>> of_objects;
};

std::vector<std::string> parseObjectPatterns(Tcl_Interp* interp, const std::string& text, bool regexp);
std::vector<std::string> findObjects(Tcl_Interp* interp, Database& database, const std::vector<std::string>& patterns, QueryObjectType type,
                                     bool regexp = false);
std::vector<std::string> findObjects(Tcl_Interp* interp, Database& database, const std::vector<std::string>& patterns, QueryObjectType type,
                                     const ObjectQueryOptions& options);
// Null input denotes the command's default pattern. Typed objects are identities,
// while untyped Tcl words retain the text pattern grammar (including regexp).
std::vector<std::string> findQueryObjects(Tcl_Interp* interp, Database& database, Tcl_Obj* objects, QueryObjectType type, const ObjectQueryOptions& options,
                                          const std::string& default_pattern = "*");
std::string findClock(Tcl_Interp* interp, Database& database, Tcl_Obj* object);
std::set<std::string> findClocks(Tcl_Interp* interp, Database& database, const std::vector<std::string>& objects);
std::set<std::string> findClocks(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects);
std::vector<std::string> findClockSources(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects);
std::set<std::string> findClocksFromObjects(Tcl_Interp* interp, Database& database, const std::vector<std::string>& objects);
ExceptionObjects findExceptionObjects(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects);
std::vector<std::string> findClockSources(Tcl_Interp* interp, Database& database, const std::vector<std::string>& objects);
std::vector<std::string> findObjectsByType(Tcl_Interp* interp, Database& database, const std::vector<std::string>& objects, QueryObjectType type);

// Typed clock objects stay clocks, typed pins/ports stay local annotations.
std::vector<std::pair<QueryObjectType, std::string>> resolveClockTargets(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects,
                                                                         bool clocks_only = false);
std::vector<std::string> resolvePinTargets(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects, bool ports_only = false,
                                           bool inputs_only = false);
std::vector<ClockSourceKind> findClockSourceKinds(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects,
                                                  const std::vector<std::string>& sources);
std::vector<std::string> getFullNames(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects);
TimingPortConstraint& getOrCreatePortConstraint(Database& database, const std::string& port_name);

}  // namespace ista::sdc
