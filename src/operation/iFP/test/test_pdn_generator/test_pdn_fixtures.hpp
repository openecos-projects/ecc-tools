#pragma once

#include <string>

#include "PGSegment.hpp"

namespace idb {
class IdbLefService;
}

namespace ifp {

bool check(bool condition, const std::string& message);
void initFixture(const std::string& directory);
void addLine(std::string net, std::string layer, PGSegmentType type, int32_t x1, int32_t y1,
             int32_t x2, int32_t y2, int32_t width);
void addConnect(std::string first_layer, std::string second_layer);
void addIDBViaRule(idb::IdbLefService& lef, std::string bottom_name, std::string cut_name, std::string top_name);
bool testIOCoreStraightOnly(const std::string& directory);

}  // namespace ifp
