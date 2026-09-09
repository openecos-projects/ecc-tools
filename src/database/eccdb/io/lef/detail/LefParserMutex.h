#pragma once

#include <mutex>

namespace eccdb::lef_detail {

// SI2 keeps parser callbacks and session state globally. Every direct LEF
// importer must hold this mutex for the complete sequence of input files.
[[nodiscard]] std::mutex& parserMutex();

}  // namespace eccdb::lef_detail
