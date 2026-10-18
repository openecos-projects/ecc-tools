#pragma once

#include "STAHeader.hpp"
#include "TimingPoint.hpp"

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

}  // namespace ista
