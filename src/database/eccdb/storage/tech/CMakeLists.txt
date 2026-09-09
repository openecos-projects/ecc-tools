# ***************************************************************************************
# Copyright (c) 2023-2025 Peng Cheng Laboratory
# Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
# Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
#
# iEDA is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
# http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.
# ***************************************************************************************

add_library(eccdb_tech_layers
  cut_layer/storage/CutLayerStorage.cpp
  implant_layer/storage/ImplantLayerStorage.cpp
  masterslice_layer/storage/MastersliceLayerStorage.cpp
  overlap_layer/storage/OverlapLayerStorage.cpp
  routing_layer/storage/RoutingLayerStorage.cpp
)
eccdb_storage_target(eccdb_tech_layers)
target_link_libraries(eccdb_tech_layers PUBLIC eccdb_entt)

add_library(eccdb_tech_global
  global/storage/TechGlobalStorage.cpp
)
eccdb_storage_target(eccdb_tech_global)
target_link_libraries(eccdb_tech_global PUBLIC eccdb_entt)

add_library(eccdb_tech_via_master
  via_master/storage/ViaMasterStorage.cpp
)
eccdb_storage_target(eccdb_tech_via_master)
target_link_libraries(eccdb_tech_via_master
  PUBLIC
    eccdb_tech_layers
    eccdb_geometry
)

add_library(eccdb_tech_via_rule
  via_rule/storage/ViaRuleStorage.cpp
)
eccdb_storage_target(eccdb_tech_via_rule)
target_link_libraries(eccdb_tech_via_rule
  PUBLIC
    eccdb_entt
    eccdb_tech_layers
    eccdb_tech_via_master
)

add_library(eccdb_tech_via_rule_generate
  via_rule_generate/storage/ViaRuleGenerateStorage.cpp
)
eccdb_storage_target(eccdb_tech_via_rule_generate)
target_link_libraries(eccdb_tech_via_rule_generate PUBLIC eccdb_tech_layers)

add_library(eccdb_tech_non_default_rule
  non_default_rule/storage/NonDefaultRuleStorage.cpp
)
eccdb_storage_target(eccdb_tech_non_default_rule)
target_link_libraries(eccdb_tech_non_default_rule
  PUBLIC
    eccdb_entt
    eccdb_geometry
    eccdb_tech_layers
    eccdb_tech_via_master
    eccdb_tech_via_rule_generate
)

add_library(eccdb_tech_database
  TechStore.cpp
)
eccdb_storage_target(eccdb_tech_database)
target_link_libraries(eccdb_tech_database
  PUBLIC
    eccdb_geometry
    eccdb_tech_global
    eccdb_tech_layers
    eccdb_tech_via_rule
    eccdb_tech_via_rule_generate
    eccdb_tech_via_master
    eccdb_tech_non_default_rule
)
