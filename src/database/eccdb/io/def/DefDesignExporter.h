#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "design/DesignStore.h"

namespace eccdb {

// Deterministic DEF 5.8 writer for the syntax represented by Design V1.
class DefDesignExporter
{
 public:
  explicit DefDesignExporter(const DesignStore& design) : _design(design) {}

  void write(std::ostream& output) const;
  [[nodiscard]] std::string exportText() const;
  void write(const std::filesystem::path& file) const;

 private:
  const DesignStore& _design;
};

}  // namespace eccdb
