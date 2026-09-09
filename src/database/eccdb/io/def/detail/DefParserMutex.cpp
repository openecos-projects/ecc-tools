#include "def/detail/DefParserMutex.h"

namespace eccdb::def_detail {

std::mutex& parserMutex()
{
  static std::mutex mutex;
  return mutex;
}

}  // namespace eccdb::def_detail
