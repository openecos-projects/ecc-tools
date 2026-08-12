#include "SpefParser.hh"

#include <cctype>
#include <cstdlib>
#include <utility>

#include "SpefText.hh"

namespace spef {
namespace {

bool hasCoord(const Coord& coord)
{
  return coord.x >= 0.0 && coord.y >= 0.0;
}

void refreshBoxFlag(GeometryAttr& geometry)
{
  geometry.has_box = hasCoord(geometry.ll_coordinate) && hasCoord(geometry.ur_coordinate);
}

}  // namespace

ParserContext::ParserContext(std::string file_name) : exchange_(std::move(file_name)) {}

void ParserContext::setSection(SectionType section)
{
  current_section_ = section;
  if (section == SectionType::kEnd) {
    finishNet();
    finishReducedNet();
  }
}

void ParserContext::startHeader(std::string key)
{
  pending_header_key_ = std::move(key);
  pending_header_values_.clear();
}

void ParserContext::addHeaderValue(std::string value)
{
  pending_header_values_.push_back(std::move(value));
}

void ParserContext::finishHeader()
{
  if (pending_header_key_.empty()) {
    return;
  }
  exchange_.header.push_back(
      HeaderEntry{pending_header_key_, text::joinHeaderValues(pending_header_values_)});
  pending_header_key_.clear();
  pending_header_values_.clear();
}

void ParserContext::addNameMap(std::string index_name, std::string mapped_name)
{
  const std::size_t index = parseNameIndex(index_name);
  exchange_.index_to_name_map[index] = mapped_name;
  exchange_.name_to_index_map[std::move(mapped_name)] = index;
}

void ParserContext::addPort(std::string name, ConnectionDirection direction, Coord coordinate)
{
  if (text::startsWithNameIndex(name)) {
    name.erase(0, 1);
  }
  exchange_.ports.push_back(PortEntry{std::move(name), direction, coordinate, false});
}

void ParserContext::startPort(std::string name,
                              ConnectionDirection direction,
                              bool physical)
{
  current_port_ = PortEntry{};
  current_port_.name = std::move(name);
  current_port_.direction = direction;
  current_port_.physical = physical;
  has_current_port_ = true;
}

void ParserContext::setPortCoordinate(Coord coordinate)
{
  if (has_current_port_) {
    current_port_.coordinate = coordinate;
  }
}

void ParserContext::finishPort()
{
  if (!has_current_port_) {
    return;
  }
  exchange_.ports.push_back(std::move(current_port_));
  current_port_ = PortEntry{};
  has_current_port_ = false;
}

void ParserContext::startNet(std::string name, double lcap, std::size_t line_no)
{
  ParValue value;
  value.first = lcap;
  value.second = lcap;
  value.third = lcap;
  startNet(std::move(name), value, false, line_no);
}

void ParserContext::startNet(std::string name,
                             ParValue lcap,
                             bool physical,
                             std::size_t line_no)
{
  if (has_current_net_) {
    finishNet();
  }
  if (has_current_reduced_net_) {
    finishReducedNet();
  }
  current_net_ = Net{};
  current_net_.name = std::move(name);
  current_net_.lcap = lcap.selected();
  current_net_.lcap_value = lcap;
  current_net_.physical = physical;
  current_net_.line_no = line_no;
  has_current_net_ = true;
}

void ParserContext::finishNet()
{
  if (!has_current_net_) {
    return;
  }
  exchange_.nets.push_back(std::move(current_net_));
  current_net_ = Net{};
  has_current_net_ = false;
}

void ParserContext::startConn(ConnectionType type, std::string name, ConnectionDirection direction)
{
  current_conn_ = ConnEntry{};
  current_conn_.conn_type = type;
  current_conn_.pin_port_name = std::move(name);
  current_conn_.conn_direction = direction;
  if (type == ConnectionType::kInternal && direction == ConnectionDirection::kUninitialized) {
    current_conn_.conn_direction = ConnectionDirection::kInternal;
  }
  has_current_conn_ = true;
}

void ParserContext::setConnCoordinate(Coord coordinate)
{
  if (has_current_conn_) {
    current_conn_.coordinate = coordinate;
  }
}

void ParserContext::setConnLoad(double load)
{
  ParValue value;
  value.first = load;
  value.second = load;
  value.third = load;
  setConnLoad(value);
}

void ParserContext::setConnLoad(ParValue load)
{
  if (has_current_conn_) {
    current_conn_.load = load.selected();
    current_conn_.load_value = load;
  }
}

void ParserContext::setConnDrivingCell(std::string driving_cell)
{
  if (has_current_conn_) {
    current_conn_.driving_cell = std::move(driving_cell);
  }
}

void ParserContext::setConnLowerLeft(Coord coordinate)
{
  if (has_current_conn_) {
    current_conn_.ll_coordinate = coordinate;
    current_conn_.geometry.ll_coordinate = coordinate;
    refreshBoxFlag(current_conn_.geometry);
  }
}

void ParserContext::setConnUpperRight(Coord coordinate)
{
  if (has_current_conn_) {
    current_conn_.ur_coordinate = coordinate;
    current_conn_.geometry.ur_coordinate = coordinate;
    refreshBoxFlag(current_conn_.geometry);
  }
}

void ParserContext::setConnLayer(int layer)
{
  if (has_current_conn_) {
    current_conn_.layer = layer;
    current_conn_.geometry.has_layer = true;
    current_conn_.geometry.layer = layer;
  }
}

void ParserContext::finishConn()
{
  if (!has_current_net_ || !has_current_conn_) {
    return;
  }
  current_net_.conns.push_back(std::move(current_conn_));
  current_conn_ = ConnEntry{};
  has_current_conn_ = false;
}

void ParserContext::addCap(std::string node1, std::string node2, double cap)
{
  ParValue value;
  value.first = cap;
  value.second = cap;
  value.third = cap;
  addCap(std::move(node1), std::move(node2), value);
}

void ParserContext::addCap(std::string node1, std::string node2, ParValue cap)
{
  addCap(0, std::move(node1), std::move(node2), cap);
}

void ParserContext::addCap(std::size_t index,
                           std::string node1,
                           std::string node2,
                           ParValue cap)
{
  if (has_current_net_) {
    current_net_.caps.push_back(
        ResCap{std::move(node1), std::move(node2), cap.selected(), cap, {}, index});
  }
}

void ParserContext::addRes(std::string node1, std::string node2, double res)
{
  ParValue value;
  value.first = res;
  value.second = res;
  value.third = res;
  addRes(std::move(node1), std::move(node2), value);
}

void ParserContext::addRes(std::string node1, std::string node2, ParValue res)
{
  addRes(0, std::move(node1), std::move(node2), res);
}

void ParserContext::addRes(std::size_t index,
                           std::string node1,
                           std::string node2,
                           ParValue res)
{
  if (has_current_net_) {
    current_net_.ress.push_back(
        ResCap{std::move(node1), std::move(node2), res.selected(), res, {}, index});
  }
}

void ParserContext::addInductance(std::string node1,
                                  std::string node2,
                                  ParValue inductance)
{
  addInductance(0, std::move(node1), std::move(node2), inductance);
}

void ParserContext::addInductance(std::size_t index,
                                  std::string node1,
                                  std::string node2,
                                  ParValue inductance)
{
  if (has_current_net_) {
    current_net_.inductances.push_back(
        ResCap{std::move(node1), std::move(node2), inductance.selected(), inductance, {}, index});
  }
}

void ParserContext::addCapOrRes(std::string node1, std::string node2, double value)
{
  ParValue par_value;
  par_value.first = value;
  par_value.second = value;
  par_value.third = value;
  addCapOrRes(std::move(node1), std::move(node2), par_value);
}

void ParserContext::addCapOrRes(std::string node1, std::string node2, ParValue value)
{
  addCapOrRes(0, std::move(node1), std::move(node2), value);
}

void ParserContext::addCapOrRes(std::size_t index,
                                std::string node1,
                                std::string node2,
                                ParValue value)
{
  if (current_section_ == SectionType::kCap) {
    addCap(index, std::move(node1), std::move(node2), value);
  } else if (current_section_ == SectionType::kRes) {
    addRes(index, std::move(node1), std::move(node2), value);
  } else if (current_section_ == SectionType::kInduc) {
    addInductance(index, std::move(node1), std::move(node2), value);
  }
}

void ParserContext::startReducedNet(std::string name,
                                    ParValue total_cap,
                                    bool physical,
                                    std::size_t line_no)
{
  if (has_current_net_) {
    finishNet();
  }
  if (has_current_reduced_net_) {
    finishReducedNet();
  }
  current_reduced_net_ = ReducedNet{};
  current_reduced_net_.name = std::move(name);
  current_reduced_net_.line_no = line_no;
  current_reduced_net_.total_cap = total_cap.selected();
  current_reduced_net_.total_cap_value = total_cap;
  current_reduced_net_.physical = physical;
  has_current_reduced_net_ = true;
}

void ParserContext::finishReducedNet()
{
  if (!has_current_reduced_net_) {
    return;
  }
  if (has_current_reduced_driver_) {
    current_reduced_net_.drivers.push_back(std::move(current_reduced_driver_));
    current_reduced_driver_ = ReducedDriver{};
    has_current_reduced_driver_ = false;
  }
  exchange_.reduced_nets.push_back(std::move(current_reduced_net_));
  current_reduced_net_ = ReducedNet{};
  has_current_reduced_net_ = false;
}

void ParserContext::startReducedDriver(std::string pin_name)
{
  if (!has_current_reduced_net_) {
    return;
  }
  if (has_current_reduced_driver_) {
    current_reduced_net_.drivers.push_back(std::move(current_reduced_driver_));
  }
  current_reduced_driver_ = ReducedDriver{};
  current_reduced_driver_.pin_name = std::move(pin_name);
  has_current_reduced_driver_ = true;
}

void ParserContext::setReducedDriverCell(std::string cell_type)
{
  if (has_current_reduced_driver_) {
    current_reduced_driver_.cell_type = std::move(cell_type);
  }
}

void ParserContext::setReducedPi(ParValue c2, ParValue r1, ParValue c1)
{
  if (has_current_reduced_driver_) {
    current_reduced_driver_.c2 = c2;
    current_reduced_driver_.r1 = r1;
    current_reduced_driver_.c1 = c1;
  }
}

void ParserContext::addReducedLoad(std::string pin_name, ParValue rc)
{
  if (has_current_reduced_driver_) {
    current_reduced_driver_.loads.push_back(
        ReducedLoad{std::move(pin_name), rc.selected(), rc});
  }
}

void ParserContext::setError(std::string message)
{
  if (error_message_.empty()) {
    error_message_ = std::move(message);
  }
}

std::size_t ParserContext::parseNameIndex(const std::string& index_name)
{
  const std::size_t begin = index_name.front() == '*' ? 1 : 0;
  const std::size_t end = index_name.find(':', begin);
  return static_cast<std::size_t>(
      std::strtoull(index_name.substr(begin, end - begin).c_str(), nullptr, 10));
}

}  // namespace spef
