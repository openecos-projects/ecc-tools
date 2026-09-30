// SPDX-License-Identifier: Apache-2.0
#include "DrtTrace.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace {
void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void rejects(Function&& action)
{
  bool rejected = false;
  try { action(); }
  catch (const std::exception&) { rejected = true; }
  require(rejected, "invalid trace was accepted");
}

struct Backend
{
  static constexpr bool retains_extensions = true;
  bool corrupt_connectivity = false;
  bool corrupted = false;
  bool corrupt_written_wire = false;
  bool corrupt_other_wire = false;
  bool corrupt_via = false;
  bool corrupt_via_after_wire = false;
  bool skip_mutation = false;
  bool skip_stack = false;
  std::map<uint32_t, std::vector<drt_trace::Shape>> wires;
  std::map<std::string, std::vector<drt_trace::ViaRect>> vias;

  bool supports(const drt_trace::Event& event) const
  {
    return event.op != "read_tech_header"
           && !(skip_mutation && event.op == "define_design_via")
           && !(skip_stack && event.op == "append_bterm_stack_wire");
  }

  drt_trace::json::value execute(const drt_trace::Event& event)
  {
    if (event.op == "net_pins") {
      return drt_trace::json::array{drt_trace::json::array{"I", "U1", corrupted ? "wrong" : "A"}};
    }
    if (event.op == "clear_wire") {
      corrupted = corrupt_connectivity;
      wires[event.object].clear();
      return nullptr;
    }
    if (event.op == "define_design_via") {
      if (!vias.emplace(event.name, event.via_rectangles).second) throw std::runtime_error("duplicate VIA");
      if (corrupt_via) ++vias[event.name][0].x1;
      return viaDefinition(event.name);
    }
    if (event.op == "read_wire") return drt_trace::shapeProjection(wire(event.object), false);
    if (event.op == "append_bterm_stack_wire") {
      wires[event.object].push_back({.kind = "via", .layer = "Metal1", .via = "Via1",
                                     .x1 = 30, .y1 = 20, .x2 = 30, .y2 = 20});
      return drt_trace::shapeValues(wire(event.object), false);
    }
    if (event.op == "replace_wire") {
      for (const auto& shape : event.shapes) {
        if (shape.kind == "via" && !shape.tech_via && !vias.contains(shape.via)) {
          throw std::runtime_error("wire references undefined design VIA");
        }
      }
      wires[event.object] = event.shapes;
      if (corrupt_written_wire && !wires[event.object].empty()) ++wires[event.object][0].x1;
      if (corrupt_other_wire) wires[2] = event.shapes;
      if (corrupt_via_after_wire && !vias.empty()) ++vias.begin()->second[0].x1;
      return nullptr;
    }
    throw std::runtime_error("unknown operation");
  }

  std::vector<drt_trace::Shape> wire(uint32_t object) { return wires[object]; }

  drt_trace::json::value viaDefinition(std::string_view name) const
  {
    const auto found = vias.find(std::string(name));
    if (found == vias.end()) throw std::runtime_error("unknown design VIA");
    return drt_trace::viaValue(name, found->second);
  }
};
}  // namespace

int main()
{
  using namespace drt_trace;
  const auto path = std::filesystem::temp_directory_path() / ("drt-trace-test-" + std::to_string(getpid()) + ".jsonl");
  try {
    const auto write = [&](std::string_view body) {
      std::ofstream file(path);
      file << body;
      require(static_cast<bool>(file), "cannot write trace test input");
    };
    const std::string header = "{\"schema\":\"tritonroute_db_trace_v1\",\"instances\":[],\"nets\":[]}\n";
    const std::string via_header = "{\"schema\":\"tritonroute_db_trace_v2\",\"instances\":[],\"nets\":[]}\n";
    const std::string v3_header = "{\"schema\":\"tritonroute_db_trace_v3\",\"instances\":[],\"nets\":[],"
                                  "\"supported_operations\":[\"read_tech_header\"]}\n";
    const std::string via_event = "{\"op\":\"define_design_via\",\"source\":\"test\",\"name\":\"V_RUNTIME\","
                                  "\"default\":true,\"rects\":[[\"Metal1\",-5,-5,5,5],[\"Via1\",-2,-2,2,2],"
                                  "[\"Metal2\",-5,-5,5,5]]}\n";
    write(header + "{\"op\":\"scan_nets\",\"source\":\"test\",\"expected\":[]}\n"
          "{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    require(Trace::load(path).events.size() == 1, "valid trace failed");
    write(header);
    rejects([&] { Trace::load(path); });
    write(header + "{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    rejects([&] { Trace::load(path); });
    write(header + "{\"op\":\"end\",\"complete\":false,\"events\":0}\n");
    rejects([&] { Trace::load(path); });
    write(header + "{\"op\":\"end\",\"complete\":true,\"events\":0}\n{}\n");
    rejects([&] { Trace::load(path); });
    write(header + "{\"op\":\"read_instance\",\"source\":\"test\",\"object\":-1}\n"
          "{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    rejects([&] { Trace::load(path); });
    write(via_header + via_event + "{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    const auto defined = Trace::load(path);
    require(defined.events.size() == 1 && defined.events[0].via_rectangles.size() == 3
                && defined.events[0].via_default,
            "valid design VIA trace failed");
    write(v3_header + "{\"op\":\"read_tech_header\",\"source\":\"test\",\"expected\":[1000,5,false]}\n"
          "{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    const auto v3 = Trace::load(path);
    require(v3.events.size() == 1 && v3.events[0].op == "read_tech_header", "valid v3 read trace failed");
    write(v3_header + "{\"op\":\"read_die\",\"source\":\"test\",\"expected\":null}\n"
                      "{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    rejects([&] { Trace::load(path); });
    const std::string context_header = "{\"schema\":\"tritonroute_db_trace_v3\",\"instances\":[],\"nets\":[],"
                                       "\"supported_operations\":[\"append_bterm_stack_wire\"]}\n";
    write(context_header + "{\"op\":\"append_bterm_stack_wire\",\"source\":\"test\",\"object\":1,"
                           "\"expected\":[]}\n{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    rejects([&] { Trace::load(path); });
    write(context_header + "{\"op\":\"append_bterm_stack_wire\",\"source\":\"test\",\"object\":1,"
                           "\"payload\":{}}\n{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    rejects([&] { Trace::load(path); });
    write(v3_header + "{\"op\":\"read_tech_header\",\"source\":\"test\",\"expected\":[1000,5,false]}\n"
                      "{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    TraceReader stream(path);
    const auto streamed_event = stream.next();
    require(streamed_event && streamed_event->op == v3.events[0].op && !stream.next(),
            "streaming trace reader did not reach a validated footer");
    Backend unsupported_backend;
    Options unsupported_options{.output = path, .backend = "test"};
    replay(unsupported_backend, v3, unsupported_options);
    std::ifstream unsupported_file(path);
    std::string unsupported_line;
    std::getline(unsupported_file, unsupported_line);
    const auto unsupported_result = json::parse(unsupported_line).as_object();
    require(unsupported_result.at("events") == 1 && unsupported_result.at("validated_events") == 0,
            "unsupported read event counts are wrong");
    require(unsupported_result.at("unsupported_operations").as_object().at("read_tech_header") == 1
                && !unsupported_result.at("validation").as_object().at("all_events").as_bool(),
            "unsupported read event was reported as validated");
    Backend unsupported_mutation_backend;
    unsupported_mutation_backend.skip_mutation = true;
    Trace mutation_trace;
    mutation_trace.events.push_back(Event{.op = "define_design_via", .name = "V_RUNTIME"});
    replay(unsupported_mutation_backend, mutation_trace, unsupported_options);
    require(unsupported_mutation_backend.vias.empty(), "unsupported mutation changed backend state");
    write(header + via_event + "{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    rejects([&] { Trace::load(path); });
    write(via_header + "{\"op\":\"define_design_via\",\"source\":\"test\",\"name\":\"V_RUNTIME\","
                       "\"default\":true,\"rects\":[[\"Metal1\",5,-5,5,5]]}\n"
                       "{\"op\":\"end\",\"complete\":true,\"events\":1}\n");
    rejects([&] { Trace::load(path); });
    rejects([&] { coordinate(json::value(INT64_C(2147483648))); });
    rejects([&] { integer(json::value(UINT64_MAX)); });
    Shape forward{"segment", "M1", "", 10, 20, 30, 20, 0, 7};
    Shape backward{"segment", "M1", "", 30, 20, 10, 20, 7, 0};
    require(shapeJson(forward, true) == shapeJson(backward, true), "direction/extension normalization failed");
    backward.begin_extension = 9;
    require(shapeJson(forward, false) == shapeJson(backward, false), "shared geometry normalization failed");
    require(shapeJson(forward, true) != shapeJson(backward, true), "extension mismatch hidden");
    require(shapeValues({forward, forward}, false).size() == 2, "duplicate geometry disappeared");
    Trace trace;
    trace.events.push_back(Event{.op = "net_pins", .object = 1,
                                 .expected = json::array{json::array{"I", "U1", "A"}}});
    trace.events.push_back(Event{.op = "clear_wire", .object = 1});
    Options configuration{.output = path, .backend = "test"};
    Backend backend;
    replay(backend, trace, configuration);
    trace.events[0].expected = json::array{json::array{"I", "U1", "B"}};
    rejects([&] { replay(backend, trace, configuration); });
    trace.events[0].expected = json::array{json::array{"I", "U1", "A"}};
    backend.corrupt_connectivity = true;
    rejects([&] { replay(backend, trace, configuration); });
    trace.events = {Event{.op = "read_wire", .object = 2, .expected = json::array{}},
                    Event{.op = "replace_wire", .object = 1, .shapes = {forward}}};
    backend = Backend{};
    replay(backend, trace, configuration);
    backend = Backend{.corrupt_written_wire = true};
    rejects([&] { replay(backend, trace, configuration); });
    backend = Backend{.corrupt_other_wire = true};
    rejects([&] { replay(backend, trace, configuration); });
    const Shape stacked_via{"via", "Metal1", "Via1", 30, 20, 30, 20};
    const Event append_stack{.op = "append_bterm_stack_wire", .object = 1,
                             .expected = shapeValues({forward, stacked_via}, false),
                             .payload = json::object{{"layer", "Metal1"}, {"x", 30}, {"y", 20},
                                                     {"vias", json::array{"Via1"}}}};
    trace.events = {Event{.op = "replace_wire", .object = 1, .shapes = {forward}}, append_stack};
    backend = Backend{};
    replay(backend, trace, configuration);
    require(backend.wire(1).size() == 2, "stack-wire append did not retain earlier geometry");
    backend = Backend{.skip_stack = true};
    replay(backend, trace, configuration);
    std::ifstream skipped_stack_file(path);
    std::string skipped_stack_line;
    std::getline(skipped_stack_file, skipped_stack_line);
    const auto skipped_stack_result = json::parse(skipped_stack_line).as_object();
    require(skipped_stack_result.at("validation").as_object().at("final_wires") == 0
                && skipped_stack_result.at("validation").as_object().at("final_wire_checks") == 0
                && skipped_stack_result.at("validation").as_object().at("unvalidated_wire_objects") == 1,
            "unsupported stack append was counted as validated final wire state");
    trace.events[1].expected = json::array{};
    backend = Backend{};
    rejects([&] { replay(backend, trace, configuration); });
    const ViaRect bottom{"Metal1", -5, -5, 5, 5};
    const ViaRect cut{"Via1", -2, -2, 2, 2};
    const ViaRect top{"Metal2", -5, -5, 5, 5};
    require(viaValue("V_RUNTIME", {cut, cut}) != viaValue("V_RUNTIME", {cut}), "duplicate VIA geometry disappeared");
    const Event define{.op = "define_design_via", .name = "V_RUNTIME", .via_rectangles = {bottom, cut, top}, .via_default = true};
    const Shape placed_via{"via", "Metal1", "V_RUNTIME", 10, 20, 10, 20, std::nullopt, std::nullopt, false};
    trace.events = {define, Event{.op = "replace_wire", .object = 1, .shapes = {placed_via}}};
    backend = Backend{};
    replay(backend, trace, configuration);
    trace.events = {Event{.op = "replace_wire", .object = 1, .shapes = {placed_via}}, define};
    backend = Backend{};
    rejects([&] { replay(backend, trace, configuration); });
    trace.events = {define, define};
    backend = Backend{};
    rejects([&] { replay(backend, trace, configuration); });
    trace.events = {define, Event{.op = "replace_wire", .object = 1, .shapes = {placed_via}}};
    backend = Backend{.corrupt_via = true};
    rejects([&] { replay(backend, trace, configuration); });
    backend = Backend{.corrupt_via_after_wire = true};
    rejects([&] { replay(backend, trace, configuration); });
    std::filesystem::remove(path);
    std::cout << "Trace schema and canonical geometry tests passed.\n";
    return 0;
  } catch (const std::exception& error) {
    std::filesystem::remove(path);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
