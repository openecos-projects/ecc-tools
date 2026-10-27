// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "graph/Arc.hpp"
#include "graph/SimValues.hpp"
#include "graph/TimingPoint.hpp"

namespace ista {

// A bidirectional physical pin has separate receiving and driving vertices.
class GraphVertex
{
 public:
  GraphVertex() = default;
  ~GraphVertex() = default;
  // getter
  std::string& get_pin_name() { return _pin_name; }
  bool get_is_driver() const { return _is_driver; }
  std::vector<std::size_t>& get_incoming_arc_list() { return _incoming_arc_list; }
  std::vector<std::size_t>& get_outgoing_arc_list() { return _outgoing_arc_list; }
  TimingPoint& get_timing_point() { return *_timing_point; }
  const TimingPoint& get_timing_point() const { return *_timing_point; }
  // setter
  void set_pin_name(const std::string& pin_name) { _pin_name = pin_name; }
  void set_is_driver(bool is_driver) { _is_driver = is_driver; }
  void set_timing_point(TimingPoint* timing_point) { _timing_point = timing_point; }
  // function

 private:
  std::string _pin_name;
  bool _is_driver = false;
  std::vector<std::size_t> _incoming_arc_list;
  std::vector<std::size_t> _outgoing_arc_list;
  TimingPoint* _timing_point = nullptr;
};

// Owns graph topology, orders and propagated state. Driver and receiver timing
// remain distinct for inout pins; graph construction determines their identity.
class Graph
{
 public:
  Graph() = default;
  Graph(const Graph& other)
      : _structure_signature(other._structure_signature),
        _sim_values(other._sim_values),
        _vertices(other._vertices),
        _vertex_order(other._vertex_order),
        _driver_points(other._driver_points),
        _arcs(other._arcs),
        _outgoing_arcs(other._outgoing_arcs),
        _incoming_arcs(other._incoming_arcs),
        _startpoints(other._startpoints),
        _endpoints(other._endpoints),
        _timing_order(other._timing_order),
        _points(other._points)
  {
    // Vertices borrow timing points from this graph, including distinct inout
    // driver points. Copying containers must not retain the source pointers.
    for (auto& vertex : _vertices) {
      const auto& name = vertex.get_pin_name();
      vertex.set_timing_point(vertex.get_is_driver() ? &driverPoint(name) : &_points.at(name));
    }
  }
  Graph& operator=(const Graph& other)
  {
    if (this != &other)
      *this = Graph(other);
    return *this;
  }
  // The maps use standard allocators: moving transfers their nodes, preserving
  // the vertex pointers into those nodes.
  Graph(Graph&&) noexcept = default;
  Graph& operator=(Graph&&) noexcept = default;

  SimValues& simValues() { return _sim_values; }
  const SimValues& simValues() const { return _sim_values; }
  std::uint64_t structureSignature() const { return _structure_signature; }
  void setStructureSignature(std::uint64_t signature) { _structure_signature = signature; }
  std::vector<GraphVertex>& vertices() { return _vertices; }
  const std::vector<GraphVertex>& vertices() const { return _vertices; }
  std::vector<int32_t>& vertexOrder() { return _vertex_order; }
  const std::vector<int32_t>& vertexOrder() const { return _vertex_order; }
  std::map<std::string, TimingPoint>& driverPoints() { return _driver_points; }
  const std::map<std::string, TimingPoint>& driverPoints() const { return _driver_points; }
  std::vector<Arc>& arcs() { return _arcs; }
  const std::vector<Arc>& arcs() const { return _arcs; }
  std::map<std::string, std::vector<std::size_t>>& outgoingArcs() { return _outgoing_arcs; }
  const std::map<std::string, std::vector<std::size_t>>& outgoingArcs() const { return _outgoing_arcs; }
  std::map<std::string, std::vector<std::size_t>>& incomingArcs() { return _incoming_arcs; }
  const std::map<std::string, std::vector<std::size_t>>& incomingArcs() const { return _incoming_arcs; }
  std::vector<std::string>& startpoints() { return _startpoints; }
  const std::vector<std::string>& startpoints() const { return _startpoints; }
  std::vector<std::string>& endpoints() { return _endpoints; }
  const std::vector<std::string>& endpoints() const { return _endpoints; }
  std::vector<std::string>& timingOrder() { return _timing_order; }
  const std::vector<std::string>& timingOrder() const { return _timing_order; }
  std::map<std::string, TimingPoint>& points() { return _points; }
  const std::map<std::string, TimingPoint>& points() const { return _points; }
  TimingPoint& driverPoint(const std::string& pin_name)
  {
    auto iter = _driver_points.find(pin_name);
    return iter == _driver_points.end() ? _points.at(pin_name) : iter->second;
  }
  TimingPoint& sourcePoint(const Arc& arc) { return arc.get_type() == ArcType::kNet ? driverPoint(arc.get_source_pin()) : _points.at(arc.get_source_pin()); }
  TimingPoint& sinkPoint(const Arc& arc) { return arc.get_type() == ArcType::kCell ? driverPoint(arc.get_sink_pin()) : _points.at(arc.get_sink_pin()); }
  const TimingPoint& driverPoint(const std::string& pin_name) const
  {
    auto iter = _driver_points.find(pin_name);
    return iter == _driver_points.end() ? _points.at(pin_name) : iter->second;
  }
  const TimingPoint& sourcePoint(const Arc& arc) const
  {
    return arc.get_type() == ArcType::kNet ? driverPoint(arc.get_source_pin()) : _points.at(arc.get_source_pin());
  }
  const TimingPoint& sinkPoint(const Arc& arc) const
  {
    return arc.get_type() == ArcType::kCell ? driverPoint(arc.get_sink_pin()) : _points.at(arc.get_sink_pin());
  }

  // Clear propagated timing at both receiving and driving vertices.
  void clearTiming()
  {
    for (GraphVertex& vertex : _vertices) {
      TimingPoint& point = vertex.get_timing_point();
      point.set_arrival(-std::numeric_limits<double>::infinity());
      point.set_required(std::numeric_limits<double>::infinity());
      point.set_slack(0.0);
      point.set_launch_time(0.0);
      point.get_predecessor().clear();
      point.setLaunchClock("");
      point.clearClockStates();
      point.get_path_state_map().clear();
      point.get_data_slew_map().clear();
      point.set_predecessor_arc_idx(std::numeric_limits<std::size_t>::max());
      point.set_is_clock_point(false);
    }
  }

 private:
  std::uint64_t _structure_signature = 0;
  SimValues _sim_values;
  std::vector<GraphVertex> _vertices;
  std::vector<int32_t> _vertex_order;
  std::map<std::string, TimingPoint> _driver_points;
  std::vector<Arc> _arcs;
  std::map<std::string, std::vector<std::size_t>> _outgoing_arcs;
  std::map<std::string, std::vector<std::size_t>> _incoming_arcs;
  std::vector<std::string> _startpoints;
  std::vector<std::string> _endpoints;
  std::vector<std::string> _timing_order;
  std::map<std::string, TimingPoint> _points;
};

}  // namespace ista
