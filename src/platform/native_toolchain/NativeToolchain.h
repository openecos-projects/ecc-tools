// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// Licensed under Mulan PSL v2. See the project LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace eccdb {
class DatabaseContext;
}

namespace ecc {

// Single-threaded flow owner. Algorithm work models are transient; the owned
// EccDB is the design source throughout the flow. Diagnostic JSON is not a
// second database object model. Operation singletons permit one active flow.
class NativeToolchain
{
 public:
  NativeToolchain(const std::vector<std::string>& lef_files, const std::string& def_file);
  explicit NativeToolchain(const std::string& binary_directory);
  ~NativeToolchain();
  NativeToolchain(const NativeToolchain&) = delete;
  NativeToolchain& operator=(const NativeToolchain&) = delete;

  void loadLiberty(const std::vector<std::string>& files);
  void loadSdc(const std::string& file);
  void loadSpef(const std::string& file);
  void loadReferenceVerilog(const std::string& file, const std::string& top);

  std::string run(const std::string& stage, const std::string& config_file,
                  const std::string& options_json = "{}");
  std::string summary() const;
  std::string inspectInstance(const std::string& name) const;
  std::string inspectNet(const std::string& name) const;
  void writeDef(const std::string& file) const;
  void writeBinary(const std::string& directory) const;
  eccdb::DatabaseContext& context() const;

 private:
  struct State;
  std::unique_ptr<State> _state;
};

}  // namespace ecc
