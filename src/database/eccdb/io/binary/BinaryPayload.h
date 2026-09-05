#pragma once

#include <iosfwd>

namespace eccdb {

class DesignStore;
class LibraryStore;
class TechStore;

namespace binary_detail {

void writeTechPayload(std::ostream& output, const TechStore& database);
void writeLibraryPayload(std::ostream& output, const LibraryStore& database);
void writeDesignPayload(std::ostream& output, const DesignStore& database);

}  // namespace binary_detail
}  // namespace eccdb
