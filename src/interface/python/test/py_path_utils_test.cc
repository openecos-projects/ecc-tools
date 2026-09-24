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
// Self-contained compile-plus-assert fixture for py_path_utils.h. Build:
//   g++ -std=c++20 -Wall -Werror src/interface/python/test/py_path_utils_test.cc -o /tmp/py_path_utils_test
#include <cassert>
#include <filesystem>
#include <optional>
#include <string>

#include "../py_path_utils.h"

// Compile-time guard against calling .empty() directly on a converted
// optional path parameter: std::optional<std::filesystem::path> has no such
// member, so misuse fails to compile. The concept must stay dependent on T;
// a bare requires-expression on a concrete type is a hard error, not a
// false constraint.
template <typename T>
concept has_empty_member = requires(T t) { t.empty(); };
static_assert(not has_empty_member<std::optional<std::filesystem::path>>);
static_assert(has_empty_member<std::filesystem::path>);  // control

int main()
{
  using python_interface::path_or_empty;

  assert(path_or_empty(std::nullopt) == "");
  assert(path_or_empty(std::optional{std::filesystem::path{}}) == "");
  assert(path_or_empty(std::optional{std::filesystem::path{""}}) == "");
  assert(path_or_empty(std::optional{std::filesystem::path{"foo/bar"}}) == "foo/bar");

  return 0;
}
