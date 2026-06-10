// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include "view_write.h"

#include "view_json_edit_apply.h"
#include "view_json_writer.h"

namespace idb {

bool writeViewJson(IdbDefService* def_service, const std::string& output_dir)
{
  ViewJsonWriter writer(def_service);
  return writer.write(output_dir);
}

bool applyViewJsonEdits(IdbDefService* def_service, const std::string& edits_path)
{
  ViewJsonEditApplier applier(def_service);
  return applier.apply(edits_path);
}

}  // namespace idb
