// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "PowerVectorSimulator.hpp"

#include <bit>
#include <cstdlib>

#include "Logger.hpp"

namespace ista {
namespace {
using Word = uint64_t;
constexpr Word kOnes = ~Word{0};

int simulationCount(const char* name, int fallback, int minimum, int maximum)
{
  const char* text = std::getenv(name);
  if (!text) return fallback;
  char* end = nullptr;
  long value = std::strtol(text, &end, 10);
  return end != text && *end == '\0' && value >= minimum && value <= maximum ? static_cast<int>(value) : fallback;
}

struct Instruction
{
  LogicOperationType op;
  std::size_t node = 0;
};
using Program = std::vector<Instruction>;
struct Gate
{
  std::size_t output;
  Program function;
};
struct Sequential
{
  std::size_t state;
  std::size_t inverse;
  bool latch;
  Program data, clock, clear, preset;
  Word previous_clock = 0;
};
struct Root
{
  std::size_t node;
  PowerActivity activity;
  TimingClock* clock = nullptr;
  double rise_probability = 0.0;
  double fall_probability = 0.0;
};

// Sixty-four trajectories share the same netlist operations.
// Each clock/input event settles at zero delay before transitions are counted.
// Gate-delay glitches are excluded, while distinct events retain both edges.
class Simulator
{
 public:
  Simulator(Database& database, double period) : _database(database), _period(period), _step(period / 2.0) {}
  bool run();

 private:
  std::size_t node(const std::string& name);
  bool compile(LogicExpression& expression, std::map<std::string, std::size_t>& ports, Program& program);
  Word evaluate(const Program& program) const;
  bool build();
  void orderGates();
  void settle();
  bool advance();
  Word randomWord();
  Word randomMask(double probability);
  Word randomSubset(Word available, int count);

  Database& _database;
  double _period, _step;
  int _steps_per_period = 2;
  std::size_t _implied_root_count = 0;
  std::map<std::string, std::size_t> _node_index;
  std::map<std::string, std::size_t> _pin_nodes;
  std::vector<Word> _values;
  std::vector<Gate> _gates;
  std::vector<std::size_t> _gate_order;
  std::vector<Sequential> _sequentials;
  std::vector<Word> _next_state;
  std::vector<Root> _roots;
  std::map<std::size_t, bool> _constants;
  uint64_t _random_state = 0x4d595df4d0f33173ULL;
};

std::size_t Simulator::node(const std::string& name)
{
  auto [it, inserted] = _node_index.emplace(name, _values.size());
  if (inserted) _values.push_back(0);
  return it->second;
}

bool Simulator::compile(LogicExpression& expression, std::map<std::string, std::size_t>& ports, Program& program)
{
  if (expression.get_is_empty()) return false;
  int depth = 0;
  for (auto& term : expression.get_term_list()) {
    Instruction instruction{term.get_operation_type()};
    switch (instruction.op) {
      case LogicOperationType::kPort: {
        auto port = ports.find(term.get_port_name());
        if (port == ports.end()) return false;
        instruction.node = port->second;
        ++depth;
        break;
      }
      case LogicOperationType::kOne:
      case LogicOperationType::kZero: ++depth; break;
      case LogicOperationType::kNot: if (depth < 1) return false; break;
      case LogicOperationType::kAnd:
      case LogicOperationType::kOr:
      case LogicOperationType::kXor: if (depth < 2) return false; --depth; break;
      default: return false;
    }
    if (depth >= 64) return false;
    program.push_back(instruction);
  }
  return depth == 1;
}

Word Simulator::evaluate(const Program& program) const
{
  Word stack[64];
  std::size_t count = 0;
  for (const auto& instruction : program) {
    switch (instruction.op) {
      case LogicOperationType::kPort: stack[count++] = _values[instruction.node]; break;
      case LogicOperationType::kOne: stack[count++] = kOnes; break;
      case LogicOperationType::kZero: stack[count++] = 0; break;
      case LogicOperationType::kNot: stack[count - 1] = ~stack[count - 1]; break;
      case LogicOperationType::kAnd: --count; stack[count - 1] &= stack[count]; break;
      case LogicOperationType::kOr: --count; stack[count - 1] |= stack[count]; break;
      case LogicOperationType::kXor: --count; stack[count - 1] ^= stack[count]; break;
      default: return 0;
    }
  }
  return count ? stack[0] : 0;
}

bool Simulator::build()
{
  if (!std::isfinite(_period) || _period <= 0.0 || !_database.get_vcd_activity_map().empty()) return false;
  for (auto& [name, clock] : _database.get_timing_constraint().get_clock_map()) {
    if (clock.get_period() > 0 && clock.get_period() < _period) _period = clock.get_period();
  }
  _steps_per_period = simulationCount("ISTA_POWER_SIM_STEPS_PER_PERIOD", 2, 2, 64);
  _step = _period / _steps_per_period;
  // Fixed sampling must resolve every declared clock edge. Keep the existing
  // analytical flow for clock waveforms that need a finer event schedule.
  for (auto& [name, clock] : _database.get_timing_constraint().get_clock_map()) {
    for (double value : {clock.get_period(), clock.get_rise_edge(), clock.get_fall_edge()}) {
      double ticks = value / _step;
      if (!std::isfinite(ticks) || std::abs(ticks - std::round(ticks)) > 1e-9) return false;
    }
  }
  for (auto& [name, net] : _database.get_net_map()) {
    auto id = node("net:" + name);
    for (auto& pin : net.get_pin_name_list()) _pin_nodes[pin] = id;
  }
  for (auto& [name, pin] : _database.get_pin_map()) {
    if (!_pin_nodes.count(name)) _pin_nodes[name] = node("pin:" + name);
  }
  std::set<std::size_t> driven;
  for (auto& [name, instance] : _database.get_instance_map()) {
    auto cell_it = _database.get_timing_library().get_cell_map().find(instance.get_cell_name());
    if (cell_it == _database.get_timing_library().get_cell_map().end()) continue;
    auto& cell = cell_it->second;
    std::map<std::string, std::size_t> ports;
    for (auto& [port_name, port] : cell.get_port_map()) {
      std::string pin_name = name + ":" + port_name;
      auto pin = _pin_nodes.find(pin_name);
      if (pin != _pin_nodes.end()) ports[port_name] = pin->second;
    }
    for (auto& seq : cell.get_sequentials()) {
      ports[seq.state_port] = node("state:" + name + ":" + seq.state_port);
      if (!seq.inverted_state_port.empty()) ports[seq.inverted_state_port] = node("state:" + name + ":" + seq.inverted_state_port);
    }
    for (auto& seq : cell.get_sequentials()) {
      Sequential model;
      model.state = ports[seq.state_port];
      model.inverse = seq.inverted_state_port.empty() ? model.state : ports[seq.inverted_state_port];
      model.latch = seq.is_latch;
      if (!compile(seq.data, ports, model.data) || !compile(seq.clock, ports, model.clock)) return false;
      if (!seq.clear.get_is_empty() && !compile(seq.clear, ports, model.clear)) return false;
      if (!seq.preset.get_is_empty() && !compile(seq.preset, ports, model.preset)) return false;
      driven.insert(model.state);
      driven.insert(model.inverse);
      // Start vectorless simulation from a reproducible logic-zero state.
      // The warm-up phase lets reset and clock activity establish reachable
      // states; storage that is never written retains this initial assumption.
      _values[model.state] = 0;
      if (model.inverse != model.state) _values[model.inverse] = ~_values[model.state];
      _sequentials.push_back(std::move(model));
    }
    // A sequential cell without its state function cannot be simulated safely.
    if (cell.get_is_sequential() && cell.get_sequentials().empty() && !cell.get_is_clock_gating()) return false;
    for (auto& [port_name, port] : cell.get_port_map()) {
      if (!port.get_is_output() || !ports.count(port_name)) continue;
      Gate gate{ports[port_name], {}};
      if (compile(port.get_function_expression(), ports, gate.function)) {
        if (!driven.insert(gate.output).second) return false;
        _gates.push_back(std::move(gate));
      } else if (!port.get_function_expression().get_is_empty()) return false;
    }
  }
  std::map<std::size_t, PowerActivity> root_activity;
  for (auto& [name, id] : _pin_nodes) {
    if (driven.count(id)) continue;
    auto it = _database.get_power_activity_map().find(name);
    if (it != _database.get_power_activity_map().end() && it->second.get_is_valid()) {
      if (!root_activity.count(id) || it->second.get_origin() == PowerActivityOrigin::kInput
          || it->second.get_origin() == PowerActivityOrigin::kClock || it->second.get_origin() == PowerActivityOrigin::kConstant) {
        root_activity[id] = it->second;
      }
    }
  }
  for (auto& [name, id] : _node_index) {
    if (driven.count(id) || root_activity.count(id)) continue;
    PowerActivity activity;
    activity.set_is_valid(true);
    activity.set_static_probability(0.5);
    activity.set_transition_density(0.1 / _period);
    activity.set_origin(PowerActivityOrigin::kInput);
    root_activity[id] = activity;
  }
  for (auto& [name, clock] : _database.get_timing_constraint().get_clock_map()) {
    if (clock.get_period() <= 0.0) continue;
    for (auto& source : clock.get_source_list()) {
      auto pin = _pin_nodes.find(source);
      if (pin == _pin_nodes.end()) continue;
      double duty = (clock.get_fall_edge() - clock.get_rise_edge()) / clock.get_period();
      if (duty <= 0.0) duty += 1.0;
      PowerActivity activity;
      activity.set_is_valid(true);
      activity.set_static_probability(duty);
      activity.set_transition_density(2.0 / clock.get_period());
      activity.set_origin(PowerActivityOrigin::kClock);
      root_activity[pin->second] = activity;
    }
  }
  // Case analysis takes precedence even when it constrains an internal output.
  // Propagate that constant through the implied-activity network as well.
  for (auto& [name, value] : _database.get_timing_constraint().get_case_analysis_map()) {
    auto pin = _pin_nodes.find(name);
    if (pin == _pin_nodes.end()) continue;
    _constants[pin->second] = value;
    PowerActivity activity;
    activity.set_is_valid(true);
    activity.set_static_probability(value ? 1.0 : 0.0);
    activity.set_transition_density(0.0);
    activity.set_origin(PowerActivityOrigin::kConstant);
    root_activity[pin->second] = activity;
  }
  if (simulationCount("ISTA_POWER_SIM_IMPLIED_ROOTS", 1, 0, 1)) {
    // Vectorless annotation first implies activity through buffers/inverters.
    // Non-clock implied nets become annotated simulation boundaries, each
    // retaining its exact marginal probability and density. Loads of one net
    // share a trajectory; separate annotated nets use separate random vectors.
    // Clock waveforms retain their phase relationship through the logic graph.
    orderGates();
    if (_gate_order.size() != _gates.size()) return false;
    std::set<std::size_t> implied;
    for (auto idx : _gate_order) {
      auto& gate = _gates[idx];
      auto& function = gate.function;
      bool invert = function.size() == 2 && function[1].op == LogicOperationType::kNot;
      if ((function.size() != 1 && !invert) || function[0].op != LogicOperationType::kPort) continue;
      auto input = root_activity.find(function[0].node);
      if (input == root_activity.end()) continue;
      PowerActivity activity = input->second;
      if (invert) {
        activity.set_static_probability(1.0 - activity.get_static_probability());
        double rise = activity.get_rise_transition_density();
        activity.set_rise_transition_density(activity.get_fall_transition_density());
        activity.set_fall_transition_density(rise);
      }
      auto existing = root_activity.find(gate.output);
      if (_constants.count(gate.output)
          || (existing != root_activity.end() && existing->second.get_origin() == PowerActivityOrigin::kClock)) activity = existing->second;
      root_activity[gate.output] = activity;
      if (activity.get_origin() != PowerActivityOrigin::kClock) {
        implied.insert(gate.output);
        driven.erase(gate.output);
      }
    }
    std::erase_if(_gates, [&](const Gate& gate) { return implied.count(gate.output); });
    _implied_root_count = implied.size();
    _gate_order.clear();
  }
  // Include roots with no prior annotation, using the documented input default.
  for (auto& [name, id] : _node_index) {
    if (driven.count(id)) continue;
    Root root;
    root.node = id;
    auto it = root_activity.find(id);
    if (it != root_activity.end()) root.activity = it->second;
    else {
      root.activity.set_is_valid(true);
      root.activity.set_static_probability(0.5);
      root.activity.set_transition_density(0.1 / _period);
      root.activity.set_origin(PowerActivityOrigin::kInput);
    }
    double probability = root.activity.get_static_probability();
    double transitions = root.activity.get_transition_density() * _step / 2.0;
    root.rise_probability = probability < 1.0 ? std::min(1.0, transitions / (1.0 - probability)) : 0.0;
    root.fall_probability = probability > 0.0 ? std::min(1.0, transitions / probability) : 0.0;
    for (auto& [clock_name, clock] : _database.get_timing_constraint().get_clock_map()) {
      for (auto& source : clock.get_source_list()) {
        auto pin = _pin_nodes.find(source);
        if (pin != _pin_nodes.end() && pin->second == id) root.clock = &clock;
      }
    }
    if (root.clock && root.clock->get_period() > 0.0) {
      double duty = (root.clock->get_fall_edge() - root.clock->get_rise_edge()) / root.clock->get_period();
      if (duty <= 0.0) duty += 1.0;
      root.activity.set_transition_density(2.0 / root.clock->get_period());
      root.activity.set_static_probability(duty);
      root.activity.set_origin(PowerActivityOrigin::kClock);
    }
    _values[id] = randomMask(probability);
    _roots.push_back(root);
  }
  orderGates();
  _next_state.resize(_sequentials.size());
  return _gate_order.size() == _gates.size();
}

void Simulator::orderGates()
{
  std::vector<std::vector<std::size_t>> fanout(_values.size());
  std::vector<int> indegree(_gates.size(), 0);
  std::set<std::size_t> gate_outputs;
  for (auto& gate : _gates) gate_outputs.insert(gate.output);
  for (std::size_t idx = 0; idx < _gates.size(); ++idx) {
    std::set<std::size_t> dependencies;
    for (auto& instruction : _gates[idx].function) {
      if (instruction.op == LogicOperationType::kPort && gate_outputs.count(instruction.node)) dependencies.insert(instruction.node);
    }
    indegree[idx] = dependencies.size();
    for (auto id : dependencies) fanout[id].push_back(idx);
  }
  std::queue<std::size_t> ready;
  for (std::size_t idx = 0; idx < _gates.size(); ++idx) if (!indegree[idx]) ready.push(idx);
  while (!ready.empty()) {
    auto idx = ready.front(); ready.pop();
    _gate_order.push_back(idx);
    for (auto next : fanout[_gates[idx].output]) if (--indegree[next] == 0) ready.push(next);
  }
}

void Simulator::settle()
{
  for (auto& [id, value] : _constants) _values[id] = value ? kOnes : 0;
  for (auto idx : _gate_order) {
    auto& gate = _gates[idx];
    auto constant = _constants.find(gate.output);
    _values[gate.output] = constant == _constants.end() ? evaluate(gate.function) : (constant->second ? kOnes : 0);
  }
}

bool Simulator::advance()
{
  // Additional passes handle clocks produced by sequential logic.
  for (int pass = 0; pass < 16; ++pass) {
    bool changed = false;
    for (std::size_t idx = 0; idx < _sequentials.size(); ++idx) {
      auto& seq = _sequentials[idx];
      Word clock = evaluate(seq.clock);
      Word enabled = seq.latch ? clock : (clock & ~seq.previous_clock);
      seq.previous_clock = clock;
      Word value = (_values[seq.state] & ~enabled) | (evaluate(seq.data) & enabled);
      Word clear = evaluate(seq.clear), preset = evaluate(seq.preset);
      // Conflicting async controls require Liberty clear_preset_var semantics,
      // including X states, which this two-state simulator cannot represent.
      if (clear & preset) return false;
      value = (value & ~(clear | preset)) | preset;
      _next_state[idx] = value;
      changed |= value != _values[seq.state];
    }
    if (!changed) return true;
    for (std::size_t idx = 0; idx < _sequentials.size(); ++idx) {
      auto& seq = _sequentials[idx];
      _values[seq.state] = _next_state[idx];
      if (seq.inverse != seq.state) _values[seq.inverse] = ~_next_state[idx];
    }
    settle();
  }
  // A latch loop or an unresolved generated-clock chain must not publish
  // partially settled activity. The caller can use analytical propagation.
  return false;
}

Word Simulator::randomWord()
{
  uint64_t value = (_random_state += 0x9e3779b97f4a7c15ULL);
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

Word Simulator::randomMask(double probability)
{
  if (probability <= 0.0) return 0;
  if (probability >= 1.0) return kOnes;
  if (probability == 0.5) return randomWord();
  uint32_t threshold = static_cast<uint32_t>(probability * 65536.0 + 0.5);
  if (threshold >= 65536) return kOnes;
  Word equal = kOnes, less = 0;
  for (int bit = 15; bit >= 0; --bit) {
    Word random = randomWord();
    if (threshold & (1U << bit)) { less |= equal & ~random; equal &= random; }
    else equal &= ~random;
  }
  return less;
}

Word Simulator::randomSubset(Word available, int count)
{
  int size = std::popcount(available);
  if (count <= 0) return 0;
  if (count >= size) return available;
  if (count > size / 2) return available ^ randomSubset(available, size - count);
  Word selected = 0;
  while (count) {
    Word bit = Word{1} << (randomWord() >> 58);
    if (available & bit) {
      selected |= bit;
      available ^= bit;
      --count;
    }
  }
  return selected;
}

bool Simulator::run()
{
  _random_state += simulationCount("ISTA_POWER_SIM_SEED", 0, 0, 1000000);
  if (!build()) return false;
  const int warmup = simulationCount("ISTA_POWER_SIM_WARMUP", 128 * _steps_per_period, 0, 1000000);
  const int samples = simulationCount("ISTA_POWER_SIM_SAMPLES", 1024 * _steps_per_period, 128, 1000000);
  const bool balanced = simulationCount("ISTA_POWER_SIM_BALANCED_INPUTS", 1, 0, 1);
  const int batches = simulationCount("ISTA_POWER_SIM_BATCHES", 4, 1, 256);
  // Average short, independently initialized batches. Extending one stateful
  // trajectory instead would also change the simulated workload duration.
  auto initial_values = _values;
  std::vector<uint64_t> ones(_values.size()), rises(_values.size()), falls(_values.size());
  for (int batch = 0; batch < batches; ++batch) {
    _values = initial_values;
    for (auto& root : _roots) {
      if (balanced && !root.clock && root.activity.get_static_probability() == 0.5) {
        _values[root.node] = randomSubset(kOnes, 32);
      } else if (batch) _values[root.node] = randomMask(root.activity.get_static_probability());
    }
    settle();
    for (auto& seq : _sequentials) seq.previous_clock = evaluate(seq.clock);
    std::vector<Word> previous = _values;
    for (int sample = 0; sample < warmup + samples; ++sample) {
      // Clock edges are on the step boundary; random-input events occur one
      // quarter step later. Probe inside the clock interval to avoid round-off
      // ambiguity exactly on an edge. Count both settled event results, including
      // a pulse that returns to its initial value within this sampling step.
      double time = (sample + 0.25) * _step;
      auto collect = [&](uint64_t duration) {
        if (sample >= warmup) {
          for (std::size_t idx = 0; idx < _values.size(); ++idx) {
            Word value = _values[idx];
            ones[idx] += duration * std::popcount(value);
            rises[idx] += std::popcount(value & ~previous[idx]);
            falls[idx] += std::popcount(~value & previous[idx]);
          }
        }
        previous = _values;
      };
      for (auto& root : _roots) {
        if (root.clock && root.clock->get_period() > 0.0) {
          double phase = std::fmod(time - root.clock->get_rise_edge(), root.clock->get_period());
          if (phase < 0.0) phase += root.clock->get_period();
          double high_time = root.clock->get_fall_edge() - root.clock->get_rise_edge();
          if (high_time <= 0.0) high_time += root.clock->get_period();
          _values[root.node] = phase < high_time ? kOnes : 0;
        }
      }
      settle();
      if (!advance()) return false;
      collect(1);
      for (auto& root : _roots) {
        if (root.clock && root.clock->get_period() > 0.0) continue;
        Word value = _values[root.node];
        if (balanced && root.activity.get_static_probability() == 0.5) {
          // Preserve the annotated 50% probability across the population.
          // Independently round the transition count at each event so every
          // trajectory has the requested Markov transition probability. A
          // carried fractional quota would correlate successive event counts.
          double expected = 32.0 * root.rise_probability;
          int count = static_cast<int>(expected);
          double draw = static_cast<double>(randomWord() >> 11) / 9007199254740992.0;
          if (draw < expected - count) ++count;
          value ^= randomSubset(value, count) | randomSubset(~value, count);
        } else if (root.rise_probability == root.fall_probability) value ^= randomMask(root.rise_probability);
        else value = (value & ~randomMask(root.fall_probability)) | (~value & randomMask(root.rise_probability));
        _values[root.node] = value;
      }

      settle();
      if (!advance()) return false;
      collect(3);
    }
  }
  std::map<std::size_t, PowerActivity> annotated;
  for (auto& root : _roots) annotated[root.node] = root.activity;
  for (auto& [pin, id] : _pin_nodes) {
    PowerActivity activity;
    activity.set_is_valid(true);
    activity.set_static_probability(static_cast<double>(ones[id]) / (64.0 * samples * batches * 4.0));
    activity.set_rise_transition_density(static_cast<double>(rises[id]) / (64.0 * samples * batches * _step));
    activity.set_fall_transition_density(static_cast<double>(falls[id]) / (64.0 * samples * batches * _step));
    activity.set_origin(PowerActivityOrigin::kPropagated);
    if (annotated.count(id)) activity = annotated[id];
    if (_constants.count(id)) {
      activity.set_static_probability(_constants[id] ? 1.0 : 0.0);
      activity.set_transition_density(0.0);
      activity.set_origin(PowerActivityOrigin::kConstant);
    }
    _database.get_power_activity_map()[pin] = activity;
  }
  STALOG.info(Loc::current(), "Simulated ", _gates.size(), " output functions and ", _sequentials.size(),
              " sequential states with 64 trajectories and ", samples, " samples in ", batches, " batches; ", _implied_root_count, " implied activity roots.");
  return true;
}
}  // namespace

bool simulateVectorlessActivity(Database& database, double reference_period)
{
  return Simulator(database, reference_period).run();
}
}  // namespace ista
