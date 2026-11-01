# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

cmake_minimum_required(VERSION 3.20)
project(eccdb_native_consumer LANGUAGES CXX)

find_package(EccDB CONFIG REQUIRED COMPONENTS core database viewer)

add_executable(eccdb_native_consumer main.cpp)
target_link_libraries(eccdb_native_consumer PRIVATE EccDB::database EccDB::core)
