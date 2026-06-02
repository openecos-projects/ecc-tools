#include <vector>
#include <string>
#include <unordered_map>
#include <cstddef>
#include <fstream>
#include <unordered_set>

#include "Block.hh"
#include "HyperGraph.hh"
#include "Pin.hh"
#include "Net.hh"

namespace imp {
	struct SubPath {
    std::string outputPin;
    std::string inputPin;
    double delay;
  }; 
  struct TimingPath {
    std::string beginpoint;
    std::string endpoint;
    double slack_time;
    std::vector<SubPath> sub_paths;
    int path_depth;
  };

	std::unordered_map<std::string, size_t> mapVertexNamesToIds(const HyperGraph<Multilevel>& graph);
	std::vector<TimingPath> parseTimingReport(const std::string& filePath);
  std::vector<std::string> splitByPath(const std::string& str);
  TimingPath parsePathBlock(const std::string& block, bool& skip_path);
  std::vector<std::string> splitString(const std::string& str, char delimiter);
  std::string trim(const std::string& str);
  std::string processBrackets(const std::string& input);
	void updateHedgeSlacks(const std::vector<TimingPath>& paths, const std::unordered_map<std::string, size_t>& vertex_name_to_id, HyperGraph<Multilevel>& graph, std::vector<std::vector<double>>& hedge_slacks, std::vector<std::vector<double>>& hedge_avaliable_delay);
	void processCriticalNets(HyperGraph<Multilevel>& graph, std::vector<std::vector<double>>& hedge_slacks, double extra_delay, double clock_period, std::unordered_set<std::string>& critical_nets_name);
  void processNonCriticalNets(HyperGraph<Multilevel>& graph, std::vector<std::vector<double>>& hedge_available_delay, double extra_delay, std::unordered_set<std::string>& non_critical_nets_name);
  void calculateAndPrintHedgeWeights(HyperGraph<Multilevel>& graph);
  void timingDrivenNetWeight(HyperGraph<Multilevel>& graph, std::unordered_set<std::string>& critical_nets_name, std::unordered_set<std::string>& non_critical_nets_name);
}  // namespace imp