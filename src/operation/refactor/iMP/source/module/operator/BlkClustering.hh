#pragma once
#include <cstddef>
#include <map>
#include <memory>
#include <numeric>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace imp {
class Block;
class HMetis;
class ParserEngine;
class Instance;
class Object;

struct BlkClustering
{
  void operator()(imp::Block& block);

  size_t l1_nparts{std::numeric_limits<size_t>::max()};
  size_t l2_nparts{std::numeric_limits<size_t>::max()};
};

// multilevel-level clustering operation
struct BlkClustering2
{
  void operator()(imp::Block& block);
  void multiLevelClustering(imp::Block& root_cluster);
  void singleLevelClustering(imp::Block& root_cluster);
  size_t l1_nparts{std::numeric_limits<size_t>::max()};
  size_t l2_nparts{std::numeric_limits<size_t>::max()};
  size_t level_num = 2;
  std::weak_ptr<ParserEngine> parser;
  std::unordered_set<std::string> critical_nets_name;
  std::unordered_set<std::string> non_critical_nets_name;

  void paramCheck();
  void reorderClusters(std::vector<size_t>& parts, Block& block);
  enum ClusterType { STD, MIX, MACRO, IO};
  void findClusterTypes(std::shared_ptr<Object> vertex_prop, std::vector<ClusterType>& clusterTypes, bool& isFixed);
};

}  // namespace imp
