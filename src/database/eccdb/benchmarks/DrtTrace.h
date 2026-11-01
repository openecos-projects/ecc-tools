// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/json.hpp>

namespace drt_trace {
namespace json = boost::json;

struct Shape
{
  // segment, via, or patch. VIA layer is its canonical bottom routing layer.
  std::string kind, layer, via;
  int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  std::optional<int32_t> begin_extension, end_extension;
  bool tech_via = true;
};

struct ViaRect
{
  std::string layer;
  int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

// Original encoder inputs, in call order. Wire create/begin/end are represented
// by the enclosing replace_wire event; new_path preserves every path boundary.
struct WireAction
{
  std::string op, layer, via, rule;
  int32_t x = 0, y = 0, a = 0, b = 0;
  std::optional<int32_t> extension;
};

inline json::object wireActionRecord(const WireAction& action)
{
  json::object result{{"op", action.op}};
  if (action.op == "new_path") {
    result["layer"] = action.layer;
    if (!action.rule.empty()) result["rule"] = action.rule;
  } else if (action.op == "point") {
    result["x"] = action.x;
    result["y"] = action.y;
    result["extension"] = action.extension ? json::value(*action.extension) : json::value(nullptr);
  } else if (action.op == "tech_via" || action.op == "via") result["via"] = action.via;
  else if (action.op == "rect") {
    result["x"] = action.x;
    result["y"] = action.y;
    result["a"] = action.a;
    result["b"] = action.b;
  } else throw std::runtime_error("unknown wire encoder action: " + action.op);
  return result;
}

struct NativeOperationTiming
{
  uint64_t calls = 0, items = 0, ns = 0;
};

// Only native API batches belong inside measure(). Binding, trace conversion,
// result JSON construction and validation happen before or after the callback.
struct NativeTimer
{
  uint64_t elapsed_ns = 0, calls = 0;
  std::map<std::string, NativeOperationTiming> operations;

  template <typename Function>
  decltype(auto) measure(std::string_view operation, uint64_t items, Function&& function)
  {
    using Clock = std::chrono::steady_clock;
    const auto begin = Clock::now();
    const auto record = [&] {
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count();
      elapsed_ns += static_cast<uint64_t>(ns);
      ++calls;
      auto& value = operations[std::string(operation)];
      ++value.calls;
      value.items += items;
      value.ns += static_cast<uint64_t>(ns);
    };
    if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
      std::invoke(std::forward<Function>(function));
      record();
    } else {
      auto result = std::invoke(std::forward<Function>(function));
      record();
      return result;
    }
  }
};

inline json::value shapeJson(Shape value, bool extensions)
{
  if (value.kind == "segment") {
    if (value.x1 > value.x2 || (value.x1 == value.x2 && value.y1 > value.y2)) {
      std::swap(value.x1, value.x2);
      std::swap(value.y1, value.y2);
      std::swap(value.begin_extension, value.end_extension);
    }
    json::array out{"segment", value.layer, value.x1, value.y1, value.x2, value.y2};
    if (extensions) {
      out.emplace_back(value.begin_extension ? json::value(*value.begin_extension) : json::value(nullptr));
      out.emplace_back(value.end_extension ? json::value(*value.end_extension) : json::value(nullptr));
    }
    return out;
  }
  if (value.kind == "via") return json::array{"via", value.layer, value.via, value.x1, value.y1, value.tech_via};
  if (value.kind == "patch") return json::array{"patch", value.layer, value.x1, value.y1, value.x2, value.y2};
  throw std::runtime_error("unsupported trace shape: " + value.kind);
}

inline json::array canonical(json::array values)
{
  std::vector<std::pair<std::string, json::value>> ordered;
  ordered.reserve(values.size());
  for (auto& value : values) ordered.emplace_back(json::serialize(value), std::move(value));
  std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
  for (std::size_t index = 0; index < ordered.size(); ++index) values[index] = std::move(ordered[index].second);
  return values;
}

inline json::value viaValue(std::string_view name, const std::vector<ViaRect>& rectangles)
{
  json::array values;
  values.reserve(rectangles.size());
  for (const auto& rectangle : rectangles) {
    values.emplace_back(json::array{rectangle.layer, rectangle.x1, rectangle.y1, rectangle.x2, rectangle.y2});
  }
  return json::array{std::string(name), canonical(std::move(values))};
}

inline json::array shapeProjection(const std::vector<Shape>& shapes, bool extensions)
{
  json::array values;
  values.reserve(shapes.size());
  for (const auto& shape : shapes) values.emplace_back(shapeJson(shape, extensions));
  return values;
}

inline json::array shapeValues(const std::vector<Shape>& shapes, bool extensions)
{
  return canonical(shapeProjection(shapes, extensions));
}

inline json::object shapeRecord(const Shape& shape)
{
  return {{"kind", shape.kind}, {"layer", shape.layer}, {"via", shape.via},
          {"x1", shape.x1}, {"y1", shape.y1}, {"x2", shape.x2}, {"y2", shape.y2},
          {"begin_extension", shape.begin_extension ? json::value(*shape.begin_extension) : json::value(nullptr)},
          {"end_extension", shape.end_extension ? json::value(*shape.end_extension) : json::value(nullptr)},
          {"tech_via", shape.tech_via}};
}

inline int64_t integer(const json::value& value)
{
  if (value.is_int64()) return value.as_int64();
  if (value.is_uint64() && value.as_uint64() <= INT64_MAX) return static_cast<int64_t>(value.as_uint64());
  throw std::runtime_error("trace expects a bounded integer");
}

inline int32_t coordinate(const json::value& value)
{
  const auto result = integer(value);
  if (result < INT32_MIN || result > INT32_MAX) throw std::runtime_error("trace coordinate overflow");
  return static_cast<int32_t>(result);
}

inline std::string text(const json::value& value)
{
  return std::string(value.as_string());
}

struct Event
{
  std::string op, source, name, instance, master, term;
  uint32_t object = 0;
  std::optional<uint32_t> ordinal;
  json::value expected, payload;
  std::vector<Shape> shapes;
  std::vector<WireAction> wire_actions;
  std::vector<ViaRect> via_rectangles;
  bool via_default = false;
};

inline bool isMultisetRead(std::string_view op)
{
  return op == "net_pins" || op == "scan_instances" || op == "scan_nets" || op == "read_wire"
         || op == "scan_tracks" || op == "read_instance_pins";
}

inline bool isMutation(std::string_view op)
{
  return op == "define_design_via" || op == "clear_wire" || op == "replace_wire";
}

inline bool isContextMutation(std::string_view op)
{
  return op == "append_track_assignment" || op == "replace_master_pin_access"
         || op == "replace_instance_pin_access" || op == "append_bterm_pin_access"
         || op == "replace_guides" || op == "append_bterm_stack_wire";
}

class TraceReader
{
 public:
  explicit TraceReader(const std::filesystem::path& path) : input_(path)
  {
    if (!input_) throw std::runtime_error("cannot open trace: " + path.string());
    std::string line;
    if (!std::getline(input_, line)) throw std::runtime_error("empty trace");
    header_ = json::parse(line).as_object();
    schema_ = text(header_.at("schema"));
    if (schema_ != "tritonroute_db_trace_v1" && schema_ != "tritonroute_db_trace_v2"
        && schema_ != "tritonroute_db_trace_v3" && schema_ != "tritonroute_db_trace_v4") {
      throw std::runtime_error("unsupported trace schema");
    }
    if (schema_ == "tritonroute_db_trace_v3" || schema_ == "tritonroute_db_trace_v4") {
      const auto& operations = header_.at("supported_operations").as_array();
      for (const auto& operation : operations) {
        if (!supported_.insert(text(operation)).second) {
          throw std::runtime_error("duplicate supported trace operation");
        }
      }
    }
  }

  [[nodiscard]] const json::object& header() const noexcept { return header_; }
  [[nodiscard]] const json::object& footer() const noexcept { return footer_; }

  std::optional<Event> next()
  {
    if (finished_) return std::nullopt;
    std::string line;
    if (!std::getline(input_, line)) throw std::runtime_error("incomplete trace");
    const auto object = json::parse(line).as_object();
    if (object.at("op") == "end") {
      footer_ = object;
      if (!footer_.at("complete").as_bool() || integer(footer_.at("events")) != static_cast<int64_t>(count_)) {
        throw std::runtime_error("incomplete trace");
      }
      if (std::getline(input_, line)) throw std::runtime_error("data after trace footer");
      if (!input_.eof() && input_.fail()) throw std::runtime_error("failed to read trace");
      finished_ = true;
      return std::nullopt;
    }
    Event event;
    event.op = text(object.at("op"));
    if ((schema_ == "tritonroute_db_trace_v3" || schema_ == "tritonroute_db_trace_v4")
        && !supported_.contains(event.op)) {
      throw std::runtime_error("trace event is not declared in supported_operations: " + event.op);
    }
    event.source = text(object.at("source"));
    if (const auto* value = object.if_contains("object")) {
      const auto token = integer(*value);
      if (token < 0 || token > UINT32_MAX) throw std::runtime_error("invalid trace object token");
      event.object = static_cast<uint32_t>(token);
    }
    if (const auto* value = object.if_contains("name")) event.name = text(*value);
    if (const auto* value = object.if_contains("instance")) event.instance = text(*value);
    if (const auto* value = object.if_contains("master")) event.master = text(*value);
    if (const auto* value = object.if_contains("term")) event.term = text(*value);
    if (const auto* value = object.if_contains("ordinal")) {
      const auto ordinal = integer(*value);
      if (ordinal < 0 || ordinal > UINT32_MAX) throw std::runtime_error("invalid trace ordinal");
      event.ordinal = static_cast<uint32_t>(ordinal);
    }
    if (const auto* value = object.if_contains("expected")) event.expected = *value;
    else if (!isMutation(event.op)) throw std::runtime_error("trace read event has no expected value");
    if (const auto* value = object.if_contains("payload")) event.payload = *value;
    if (isContextMutation(event.op)
        && (event.payload.is_null() || event.expected.is_null())) {
      throw std::runtime_error("trace context mutation requires payload and expected post-state: " + event.op);
    }
    if (event.op == "define_design_via") {
      if (schema_ == "tritonroute_db_trace_v1" || event.name.empty()) {
        throw std::runtime_error("invalid design VIA definition event");
      }
      const auto& is_default = object.at("default");
      if (!is_default.is_bool()) throw std::runtime_error("design VIA default flag must be boolean");
      event.via_default = is_default.as_bool();
      const auto& rectangles = object.at("rects").as_array();
      if (rectangles.empty()) throw std::runtime_error("design VIA definition has no rectangles");
      event.via_rectangles.reserve(rectangles.size());
      for (const auto& value : rectangles) {
        if (!value.is_array() || value.as_array().size() != 5) {
          throw std::runtime_error("invalid design VIA rectangle");
        }
        const auto& fields = value.as_array();
        ViaRect rectangle{text(fields[0]), coordinate(fields[1]), coordinate(fields[2]),
                          coordinate(fields[3]), coordinate(fields[4])};
        if (rectangle.layer.empty() || rectangle.x1 >= rectangle.x2 || rectangle.y1 >= rectangle.y2) {
          throw std::runtime_error("invalid design VIA rectangle geometry");
        }
        event.via_rectangles.push_back(std::move(rectangle));
      }
    }
    if (const auto* values = object.if_contains("shapes")) {
      for (const auto& value : values->as_array()) {
        const auto& shape = value.as_object();
        Shape item;
        item.kind = text(shape.at("kind")); item.layer = text(shape.at("layer")); item.via = text(shape.at("via"));
        item.x1 = coordinate(shape.at("x1")); item.y1 = coordinate(shape.at("y1"));
        item.x2 = coordinate(shape.at("x2")); item.y2 = coordinate(shape.at("y2"));
        if (!shape.at("begin_extension").is_null()) item.begin_extension = coordinate(shape.at("begin_extension"));
        if (!shape.at("end_extension").is_null()) item.end_extension = coordinate(shape.at("end_extension"));
        item.tech_via = shape.at("tech_via").as_bool();
        event.shapes.push_back(std::move(item));
      }
    }
    if (const auto* values = object.if_contains("wire_actions")) {
      if (schema_ != "tritonroute_db_trace_v4") {
        throw std::runtime_error("original wire actions require trace schema v4");
      }
      for (const auto& value : values->as_array()) {
        const auto& fields = value.as_object();
        WireAction action;
        action.op = text(fields.at("op"));
        if (action.op == "new_path") {
          action.layer = text(fields.at("layer"));
          if (const auto* rule = fields.if_contains("rule")) action.rule = text(*rule);
        }
        else if (action.op == "point") {
          action.x = coordinate(fields.at("x"));
          action.y = coordinate(fields.at("y"));
          if (!fields.at("extension").is_null()) action.extension = coordinate(fields.at("extension"));
        } else if (action.op == "tech_via" || action.op == "via") action.via = text(fields.at("via"));
        else if (action.op == "rect") {
          action.x = coordinate(fields.at("x"));
          action.y = coordinate(fields.at("y"));
          action.a = coordinate(fields.at("a"));
          action.b = coordinate(fields.at("b"));
        } else throw std::runtime_error("unknown wire encoder action: " + action.op);
        event.wire_actions.push_back(std::move(action));
      }
    }
    if (schema_ == "tritonroute_db_trace_v4" && event.op == "replace_wire"
        && event.wire_actions.empty()) {
      throw std::runtime_error("v4 Wire replacement has no original encoder actions");
    }
    ++count_;
    return event;
  }

 private:
  std::ifstream input_;
  json::object header_, footer_;
  std::string schema_;
  std::set<std::string> supported_;
  std::size_t count_ = 0;
  bool finished_ = false;
};

struct Trace
{
  json::object header, footer;
  std::vector<Event> events;

  static Trace load(const std::filesystem::path& path)
  {
    TraceReader reader(path);
    Trace result;
    result.header = reader.header();
    while (auto event = reader.next()) result.events.push_back(std::move(*event));
    result.footer = reader.footer();
    return result;
  }
};

struct Options
{
  std::filesystem::path trace, lef, def, guides, output;
  std::string backend;
  bool profile_wire_write = false;
};

inline Options options(int argc, char** argv)
{
  Options result;
  for (int index = 1; index < argc; ++index) {
    const std::string argument(argv[index]);
    if (argument == "--profile-wire-write") {
      result.profile_wire_write = true;
      continue;
    }
    if (++index == argc) throw std::runtime_error("missing option value");
    if (argument == "--trace") result.trace = argv[index];
    else if (argument == "--lef") result.lef = argv[index];
    else if (argument == "--def") result.def = argv[index];
    else if (argument == "--guides") result.guides = argv[index];
    else if (argument == "--output") result.output = argv[index];
    else if (argument == "--backend") result.backend = argv[index];
    else throw std::runtime_error("unknown option: " + argument);
  }
  if (result.trace.empty() || result.lef.empty() || result.def.empty() || result.output.empty()) {
    throw std::runtime_error("required: --trace TRACE --lef LEF --def DEF --output JSON --backend BACKEND");
  }
  return result;
}

// Production backends isolate their native API batches with NativeTimer.
// execute() remains the untimed correctness path; backends without executeNative
// are deliberately unmeasured, never reported as zero-cost native APIs.
template <typename Backend, typename Visit>
void replayEvents(Backend& backend, Visit&& visit, const Options& options)
{
  std::map<std::string, uint64_t> timings, counts, measured_counts, unsupported, unmeasured;
  std::map<std::string, NativeOperationTiming> native_operations;
  std::map<std::string, uint64_t> wire_actions;
  std::map<uint32_t, std::vector<Shape>> final_wires;
  std::map<uint32_t, json::value> final_wire_queries;
  std::map<uint32_t, json::value> final_pins;
  std::map<std::string, json::value> final_vias;
  std::set<uint32_t> unvalidated_wire_objects;
  uint64_t seen = 0, checked = 0, extensions = 0;
  uint64_t via_rectangles = 0;
  const auto consume = [&](const Event& event) {
    ++seen;
    ++counts[event.op];
    if constexpr (requires { backend.supports(event); }) {
      if (!backend.supports(event)) {
        ++unsupported[event.op];
        if (event.op == "replace_wire" || event.op == "clear_wire" || event.op == "append_bterm_stack_wire") {
          final_wires.erase(event.object);
          final_wire_queries.erase(event.object);
          unvalidated_wire_objects.insert(event.object);
        }
        return;
      }
    }
    NativeTimer timer;
    auto actual = [&]() -> json::value {
      if constexpr (requires { backend.executeNative(event, timer); }) {
        return backend.executeNative(event, timer);
      } else return backend.execute(event);
    }();
    if (timer.calls != 0u) {
      timings[event.op] += timer.elapsed_ns;
      ++measured_counts[event.op];
    } else ++unmeasured[event.op];
    for (const auto& [name, value] : timer.operations) {
      auto& accumulated = native_operations[name];
      accumulated.calls += value.calls;
      accumulated.items += value.items;
      accumulated.ns += value.ns;
    }
    for (const auto& action : event.wire_actions) ++wire_actions[action.op];
    if (event.op == "define_design_via") {
      if (final_vias.contains(event.name)) {
        throw std::runtime_error("duplicate design VIA definition at event " + std::to_string(seen - 1));
      }
      const auto wanted = viaValue(event.name, event.via_rectangles);
      if (actual != wanted) {
        throw std::runtime_error("design VIA definition mismatch at event " + std::to_string(seen - 1));
      }
      final_vias.emplace(event.name, wanted);
      via_rectangles += event.via_rectangles.size();
    } else if (event.op == "replace_wire" || event.op == "clear_wire") {
      auto shapes = backend.wire(event.object);
      const auto wanted = event.op == "replace_wire" ? event.shapes : std::vector<Shape>{};
      if (shapeValues(shapes, backend.retains_extensions) != shapeValues(wanted, backend.retains_extensions)) {
        throw std::runtime_error("wire replay mismatch at event " + std::to_string(seen - 1) + " object " + std::to_string(event.object));
      }
      final_wires[event.object] = wanted;
      final_wire_queries[event.object] = shapeValues(wanted, false);
      unvalidated_wire_objects.erase(event.object);
      for (const auto& shape : wanted) extensions += shape.begin_extension.has_value() + shape.end_extension.has_value();
    } else if (event.op == "append_bterm_stack_wire") {
      if (!event.expected.is_array()) {
        throw std::runtime_error("BTerm stack-wire event has no projected post-state");
      }
      auto shapes = backend.wire(event.object);
      const auto wanted = canonical(event.expected.as_array());
      if (shapeValues(shapes, false) != wanted || actual != wanted) {
        throw std::runtime_error("BTerm stack-wire replay mismatch at event "
                                 + std::to_string(seen - 1) + " object "
                                 + std::to_string(event.object));
      }
      final_wires[event.object] = std::move(shapes);
      final_wire_queries[event.object] = wanted;
      unvalidated_wire_objects.erase(event.object);
    } else {
      if (isMultisetRead(event.op)) {
        actual = canonical(std::move(actual.as_array()));
      }
      if (actual != event.expected) {
        throw std::runtime_error("read replay mismatch at event " + std::to_string(seen - 1) + " (" + event.op + ") expected "
                                 + json::serialize(event.expected) + " actual " + json::serialize(actual));
      }
      if (event.op == "net_pins") final_pins[event.object] = event.expected;
      if (event.op == "read_wire") final_wire_queries[event.object] = event.expected;
    }
    ++checked;
  };
  visit(consume);
  uint64_t primitives = 0;
  for (const auto& [object, wanted] : final_wires) {
    if (shapeValues(backend.wire(object), backend.retains_extensions) != shapeValues(wanted, backend.retains_extensions)) {
      throw std::runtime_error("final wire mismatch for object " + std::to_string(object));
    }
    primitives += wanted.size();
  }
  for (const auto& [object, wanted] : final_wire_queries) {
    if (!final_wires.contains(object) && shapeValues(backend.wire(object), false) != wanted) {
      throw std::runtime_error("route replay changed an unmodified wire for object " + std::to_string(object));
    }
  }
  for (const auto& [object, wanted] : final_pins) {
    Event query;
    query.op = "net_pins"; query.object = object;
    if (canonical(backend.execute(query).as_array()) != wanted) {
      throw std::runtime_error("route replay changed pin connectivity for object " + std::to_string(object));
    }
  }
  for (const auto& [name, wanted] : final_vias) {
    if (backend.viaDefinition(name) != wanted) {
      throw std::runtime_error("final design VIA definition mismatch for " + name);
    }
  }
  json::object measured, operations, measured_operations, unsupported_operations, unmeasured_operations;
  json::object native_api_batches, encoder_actions;
  uint64_t total = 0, unsupported_count = 0;
  for (const auto& [op, ns] : timings) { measured[op] = ns; total += ns; }
  for (const auto& [op, count] : counts) operations[op] = count;
  for (const auto& [op, count] : measured_counts) measured_operations[op] = count;
  for (const auto& [op, count] : unmeasured) unmeasured_operations[op] = count;
  for (const auto& [name, value] : native_operations) {
    native_api_batches[name] = json::object{{"calls", value.calls}, {"items", value.items}, {"ns", value.ns}};
  }
  for (const auto& [name, count] : wire_actions) encoder_actions[name] = count;
  for (const auto& [op, count] : unsupported) {
    unsupported_operations[op] = count;
    unsupported_count += count;
  }
  json::object output{{"schema", "tritonroute_db_trace_replay_v3"}, {"backend", options.backend},
                      {"events", seen}, {"validated_events", checked}, {"operations", operations},
                      {"measured_operations", measured_operations}, {"unsupported_operations", unsupported_operations},
                      {"unmeasured_operations", unmeasured_operations},
                      {"native_api_batches", native_api_batches}, {"wire_encoder_actions", encoder_actions},
                      {"timing_ns", measured}, {"measured_native_total_ns", total},
                      {"wire_write_input", options.backend == "odb" ? "encoder_actions_or_legacy_shapes"
                                                                   : "normalized_shape_batch"},
                      {"wire_write_equivalence", "geometry and supported endpoint extensions; not encoder opcode/path identity"},
                      {"validation", json::object{{"all_events", unsupported.empty()},
                                                  {"all_supported_events", checked + unsupported_count == seen},
                                                  {"final_wires", final_wires.size()},
                                                  {"final_primitives", primitives}, {"shared_geometry", true},
                                                  {"final_net_pin_checks", final_pins.size()},
                                                  {"final_wire_checks", final_wire_queries.size()},
                                                  {"unvalidated_wire_objects", unvalidated_wire_objects.size()},
                                                  {"endpoint_extensions", backend.retains_extensions},
                                                  {"trace_endpoint_extension_fields", extensions},
                                                  {"validated_endpoint_extension_fields", backend.retains_extensions ? extensions : 0u},
                                                  {"final_vias", final_vias.size()},
                                                  {"final_via_rectangles", via_rectangles}}},
                      {"scope", "recorded TritonRoute database API groups; unsupported backend groups are explicit"},
                      {"timing_scope", "native database/service API batches only; trace parsing, binding, adaptor input conversion and result JSON projection/validation excluded; unmeasured groups explicitly listed"}};
  if (options.profile_wire_write) {
    if constexpr (requires(Backend& candidate) { candidate.wireWriteProfile(); }) {
      output["wire_write_profile"] = backend.wireWriteProfile();
    }
  }
  std::ofstream file(options.output);
  file << json::serialize(output) << '\n';
  if (!file) throw std::runtime_error("cannot write replay result");
}

template <typename Backend>
void replay(Backend& backend, const Trace& trace, const Options& options)
{
  replayEvents(backend, [&](const auto& consume) {
    for (const auto& event : trace.events) consume(event);
  }, options);
}

template <typename Backend>
void replayStream(Backend& backend, TraceReader& reader, const Options& options)
{
  replayEvents(backend, [&](const auto& consume) {
    while (auto event = reader.next()) consume(*event);
  }, options);
}
}  // namespace drt_trace
