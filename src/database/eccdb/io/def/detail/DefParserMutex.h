#pragma once

#include <mutex>

namespace eccdb::def_detail {

// SI2 DEF callbacks and parser session state are process-global.
[[nodiscard]] std::mutex& parserMutex();

}  // namespace eccdb::def_detail
