#pragma once

#include <bit>
#include <cstdint>

namespace ecc::geometry {

constexpr uint32_t kClockTreeSchemaVersion = 1;

enum class ClockTreeStage : uint16_t
{
  kUnknown = 0,
  kPostFloorplan = 1,
  kCts = 2,
  kFiller = 3,
};

enum class ClockTreeStatus : uint16_t
{
  kUnavailable = 0,
  kNoClocks = 1,
  kTopologyOnly = 2,
  kPartialTiming = 3,
  kCompleteTiming = 4,
};

enum class ClockTreeAnalysisSource : uint16_t
{
  kNone = 0,
  kCtsFastSta = 1,
  kIstaTypical = 2,
};

enum class ClockNodeKind : uint8_t
{
  kUnknown = 0,
  kRootPort = 1,
  kInternalRoot = 2,
  kBufferInput = 3,
  kBufferOutput = 4,
  kInverterInput = 5,
  kInverterOutput = 6,
  kIcgInput = 7,
  kIcgOutput = 8,
  kSink = 9,
  kMacroBoundary = 10,
  kOtherBoundary = 11,
};

enum class ClockEdgeKind : uint8_t
{
  kUnknown = 0,
  kNet = 1,
  kCell = 2,
};

enum class ClockEdgeTransition : uint8_t
{
  kRise = 0,
  kFall = 1,
};

enum ClockNodeFlag : uint8_t
{
  kClockNodePositionValid = 1U << 0U,
  kClockNodeOwnerValid = 1U << 1U,
  kClockNodeTimingValid = 1U << 2U,
  kClockNodeRoot = 1U << 3U,
  kClockNodeSink = 1U << 4U,
};

enum ClockRecordFlag : uint8_t
{
  kClockNameValid = 1U << 0U,
  kClockRootsValid = 1U << 1U,
  kClockTopologyValid = 1U << 2U,
  kClockTimingValid = 1U << 3U,
};

enum ClockTimingFlag : uint16_t
{
  kClockTimingArrivalValid = 1U << 0U,
  kClockTimingSlewValid = 1U << 1U,
  kClockTimingPredecessorValid = 1U << 2U,
};

struct ClockTreeMetaRecord
{
  uint32_t schema_version = kClockTreeSchemaVersion;
  uint16_t stage = static_cast<uint16_t>(ClockTreeStage::kUnknown);
  uint16_t status = static_cast<uint16_t>(ClockTreeStatus::kUnavailable);
  uint16_t analysis_source = static_cast<uint16_t>(ClockTreeAnalysisSource::kNone);
  uint16_t flags = 0;
  uint32_t dbu_per_micron = 0;
  uint64_t node_count = 0;
  uint64_t edge_count = 0;
  uint64_t timing_count = 0;
  uint64_t string_size = 0;
  double temperature_c = 0.0;
  uint32_t clock_count = 0;
  uint32_t root_count = 0;
  uint32_t rc_corner_offset = 0;
  uint32_t rc_corner_size = 0;
  uint32_t liberty_corner_offset = 0;
  uint32_t liberty_corner_size = 0;
  uint32_t diagnostics_offset = 0;
  uint32_t diagnostics_size = 0;
  uint64_t producer_fingerprint = 0;
};

// The clock-tree meta file stores one ClockTreeMetaRecord directly after the
// GeometryFileHeader, followed by meta.clock_count ClockRecord entries and then
// meta.root_count uint32_t global node indices. The header's record size and
// count describe the single metadata record; payload_size is the complete
// byte count after the header. Independent schema_version fields version this
// layout without changing the physical-geometry headers.

struct ClockRecord
{
  uint32_t clock_id = 0;
  uint32_t master_clock_id = UINT32_MAX;
  uint32_t root_node_first = 0;
  uint32_t root_node_count = 0;
  uint32_t node_first = 0;
  uint32_t node_count = 0;
  uint32_t edge_first = 0;
  uint32_t edge_count = 0;
  uint32_t sink_count = 0;
  uint8_t flags = 0;
  uint8_t source_edge = 0;
  uint16_t reserved0 = 0;
  uint32_t name_offset = 0;
  uint32_t name_size = 0;
  uint32_t diagnostics_offset = 0;
  uint32_t diagnostics_size = 0;
  uint64_t reserved1 = 0;
};

struct ClockNodeRecord
{
  uint32_t node_id = 0;
  uint32_t clock_id = 0;
  uint8_t kind = static_cast<uint8_t>(ClockNodeKind::kUnknown);
  uint8_t flags = 0;
  uint16_t reserved0 = 0;
  uint32_t owner_index = UINT32_MAX;
  uint32_t instance_offset = 0;
  uint32_t instance_size = 0;
  uint32_t pin_offset = 0;
  uint32_t pin_size = 0;
  uint32_t master_offset = 0;
  uint32_t master_size = 0;
  int32_t x = 0;
  int32_t y = 0;
  uint32_t reserved1 = 0;
  uint32_t reserved2 = 0;
};

struct ClockEdgeRecord
{
  uint32_t edge_id = 0;
  uint32_t from_node = 0;
  uint32_t to_node = 0;
  uint8_t kind = static_cast<uint8_t>(ClockEdgeKind::kUnknown);
  uint8_t polarity = 0;
  uint16_t flags = 0;
  uint32_t net_offset = 0;
  uint32_t net_size = 0;
  uint32_t cell_offset = 0;
  uint32_t cell_size = 0;
  uint64_t object_ref = 0;
};

struct ClockTimingRecord
{
  uint32_t node_id = 0;
  uint16_t flags = 0;
  uint8_t source_edge = 0;
  uint8_t local_edge = 0;
  uint32_t predecessor_node = UINT32_MAX;
  uint32_t reserved0 = 0;
  double arrival_ns = 0.0;
  double slew_ns = 0.0;
  uint32_t scenario_offset = 0;
  uint32_t scenario_size = 0;
  uint32_t reserved1 = 0;
  uint32_t reserved2 = 0;
};

static_assert(sizeof(ClockTreeMetaRecord) == 96);
static_assert(sizeof(ClockRecord) == 64);
static_assert(sizeof(ClockNodeRecord) == 56);
static_assert(sizeof(ClockEdgeRecord) == 40);
static_assert(sizeof(ClockTimingRecord) == 48);
static_assert(std::endian::native == std::endian::little,
              "clock-tree snapshot records are defined as little-endian");

}  // namespace ecc::geometry
