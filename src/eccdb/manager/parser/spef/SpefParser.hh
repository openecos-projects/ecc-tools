#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spef {

enum class SectionType
{
  kHeader,
  kNameMap,
  kPorts,
  kPhysicalPorts,
  kConn,
  kCap,
  kRes,
  kInduc,
  kEnd
};

enum class ConnectionDirection
{
  kInput,
  kOutput,
  kInout,
  kInternal,
  kUninitialized
};

enum class ConnectionType
{
  kInternal,
  kExternal,
  kUninitialized
};

struct Coord
{
  double x = -1.0;
  double y = -1.0;
};

struct ParValue
{
  double first = 0.0;
  double second = 0.0;
  double third = 0.0;
  bool is_triple = false;

  double selected() const { return first; }
};

struct GeometryAttr
{
  bool has_box = false;
  Coord ll_coordinate;
  Coord ur_coordinate;

  bool has_layer = false;
  int layer = 0;

  bool has_length = false;
  double length = 0.0;

  bool has_width = false;
  double width = 0.0;

  bool has_area = false;
  double area = 0.0;

  bool has_direction = false;
  int direction = -1;
};

struct HeaderEntry
{
  std::string key;
  std::string value;
};

struct LayerMapEntry
{
  int level = 0;
  std::string layer_name;
  std::string raw_info;
};

struct PortEntry
{
  std::string name;
  ConnectionDirection direction = ConnectionDirection::kUninitialized;
  Coord coordinate;
  bool physical = false;
};

struct ConnEntry
{
  ConnectionType conn_type = ConnectionType::kUninitialized;
  ConnectionDirection conn_direction = ConnectionDirection::kUninitialized;
  std::string pin_port_name;
  std::string driving_cell;
  double load = 0.0;
  ParValue load_value;
  int layer = 0;
  Coord coordinate;
  Coord ll_coordinate;
  Coord ur_coordinate;
  GeometryAttr geometry;
};

struct ResCap
{
  std::string node1;
  std::string node2;
  double res_or_cap = 0.0;
  ParValue value;
  GeometryAttr geometry;
  std::size_t index = 0;
};

struct Net
{
  std::string name;
  std::size_t line_no = 0;
  double lcap = 0.0;
  ParValue lcap_value;
  bool physical = false;
  std::vector<ConnEntry> conns;
  std::vector<ResCap> caps;
  std::vector<ResCap> ress;
  std::vector<ResCap> inductances;
};

struct ReducedLoad
{
  std::string pin_name;
  double rc = 0.0;
  ParValue rc_value;
};

struct ReducedDriver
{
  std::string pin_name;
  std::string cell_type;
  ParValue c2;
  ParValue r1;
  ParValue c1;
  std::vector<ReducedLoad> loads;
};

struct ReducedNet
{
  std::string name;
  std::size_t line_no = 0;
  double total_cap = 0.0;
  ParValue total_cap_value;
  bool physical = false;
  std::vector<ReducedDriver> drivers;
};

struct Exchange
{
  explicit Exchange(std::string file_name) : file_name(std::move(file_name)) {}

  std::string file_name;
  std::vector<HeaderEntry> header;
  std::unordered_map<std::size_t, std::string> index_to_name_map;
  std::unordered_map<std::string, std::size_t> name_to_index_map;
  std::unordered_map<int, LayerMapEntry> layer_map;
  std::vector<PortEntry> ports;
  std::vector<Net> nets;
  std::vector<ReducedNet> reduced_nets;
};

#define FOREACH_SPEF_NET(spef_file, spef_net) \
  for (const auto& spef_net : (spef_file)->nets)

#define FOREACH_SPEF_CONN(spef_net, conn) \
  for (const auto& conn : (spef_net).conns)

#define FOREACH_SPEF_CAP(spef_net, cap) \
  for (const auto& cap : (spef_net).caps)

#define FOREACH_SPEF_RES(spef_net, res) \
  for (const auto& res : (spef_net).ress)

#define FOREACH_SPEF_INDUC(spef_net, induc) \
  for (const auto& induc : (spef_net).inductances)

class ParserContext
{
 public:
  explicit ParserContext(std::string file_name);

  Exchange& exchange() { return exchange_; }
  const Exchange& exchange() const { return exchange_; }

  void setSection(SectionType section);
  SectionType section() const { return current_section_; }

  void startHeader(std::string key);
  void addHeaderValue(std::string value);
  void finishHeader();

  void addNameMap(std::string index_name, std::string mapped_name);
  void addPort(std::string name, ConnectionDirection direction, Coord coordinate);
  void startPort(std::string name, ConnectionDirection direction, bool physical);
  void setPortCoordinate(Coord coordinate);
  void finishPort();

  void startNet(std::string name, double lcap, std::size_t line_no);
  void startNet(std::string name, ParValue lcap, bool physical, std::size_t line_no);
  void finishNet();

  void startConn(ConnectionType type, std::string name, ConnectionDirection direction);
  void setConnCoordinate(Coord coordinate);
  void setConnLoad(double load);
  void setConnLoad(ParValue load);
  void setConnDrivingCell(std::string driving_cell);
  void setConnLowerLeft(Coord coordinate);
  void setConnUpperRight(Coord coordinate);
  void setConnLayer(int layer);
  void finishConn();

  void addCap(std::string node1, std::string node2, double cap);
  void addCap(std::string node1, std::string node2, ParValue cap);
  void addCap(std::size_t index, std::string node1, std::string node2, ParValue cap);
  void addRes(std::string node1, std::string node2, double res);
  void addRes(std::string node1, std::string node2, ParValue res);
  void addRes(std::size_t index, std::string node1, std::string node2, ParValue res);
  void addInductance(std::string node1, std::string node2, ParValue inductance);
  void addInductance(std::size_t index,
                     std::string node1,
                     std::string node2,
                     ParValue inductance);
  void addCapOrRes(std::string node1, std::string node2, double value);
  void addCapOrRes(std::string node1, std::string node2, ParValue value);
  void addCapOrRes(std::size_t index, std::string node1, std::string node2, ParValue value);

  void startReducedNet(std::string name, ParValue total_cap, bool physical, std::size_t line_no);
  void finishReducedNet();
  void startReducedDriver(std::string pin_name);
  void setReducedDriverCell(std::string cell_type);
  void setReducedPi(ParValue c2, ParValue r1, ParValue c1);
  void addReducedLoad(std::string pin_name, ParValue rc);

  void setError(std::string message);
  bool ok() const { return error_message_.empty(); }
  const std::string& errorMessage() const { return error_message_; }

 private:
  static std::size_t parseNameIndex(const std::string& index_name);

  Exchange exchange_;
  SectionType current_section_ = SectionType::kHeader;
  Net current_net_;
  bool has_current_net_ = false;
  PortEntry current_port_;
  bool has_current_port_ = false;
  ConnEntry current_conn_;
  bool has_current_conn_ = false;
  ReducedNet current_reduced_net_;
  bool has_current_reduced_net_ = false;
  ReducedDriver current_reduced_driver_;
  bool has_current_reduced_driver_ = false;
  std::string pending_header_key_;
  std::vector<std::string> pending_header_values_;
  std::string error_message_;
};

double toDouble(const char* text);
ParValue parseParValue(const char* text);
int toInt(const char* text);
std::size_t toSize(const char* text);
std::string tokenToString(const char* text);
std::string stripQuotes(std::string text);
ConnectionDirection parseDirection(const char* text);
ConnectionType parseConnectionType(const char* text);
void expandAllNames(Exchange& exchange);
std::string expandName(const Exchange& exchange, const std::string& name);
std::string removeEscapes(const std::string& name);

Exchange* parseSpefFile(const char* spef_path);
std::string getSpefCapUnit(const Exchange& exchange);
std::string getSpefResUnit(const Exchange& exchange);

class SpefReader
{
 public:
  SpefReader() = default;
  ~SpefReader() = default;

  bool read(const std::string& file_path);

  Exchange* getSpefFile() { return spef_file_.get(); }
  const Exchange* getSpefFile() const { return spef_file_.get(); }

  void expandName();
  std::string getSpefCapUnit() const;
  std::string getSpefResUnit() const;

 private:
  std::unique_ptr<Exchange> spef_file_;
};

}  // namespace spef
