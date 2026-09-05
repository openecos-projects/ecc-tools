#include "lef/detail/LefParserMutex.h"

namespace eccdb::lef_detail {

std::mutex& parserMutex()
{
  static std::mutex mutex;
  return mutex;
}

}  // namespace eccdb::lef_detail
