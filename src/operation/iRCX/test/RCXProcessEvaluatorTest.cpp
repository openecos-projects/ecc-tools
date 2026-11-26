#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

#include "ProcessEvaluator.hpp"
#include "ProcessReader.hpp"
#include "ProcessSpatialEvaluator.hpp"
#include "ProcessThicknessEvaluator.hpp"

using namespace ircx;
namespace {
void near(double actual, double expected)
{
  if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-10)
    throw std::runtime_error("expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}
template <class T>
const T& checked(const ProcessResult<T>& result)
{
  if (!result)
    throw std::runtime_error(result.error);
  return *result;
}
void near(const ProcessResult<double>& result, double expected)
{
  near(checked(result), expected);
}
ProcessTechnology parse(const std::string& text)
{
  auto result = parseProcess(text);
  if (!result) throw std::runtime_error(result.diagnostics.back().message);
  return std::move(*result.technology);
}
template <class Operation>
void rejects(Operation operation)
{
  auto result = operation();
  if constexpr (requires {
                  result.error;
                  static_cast<bool>(result);
                }) {
    if (result || result.error.empty())
      throw std::runtime_error("invalid evaluation accepted or diagnostic missing");
  } else {
    throw std::runtime_error("evaluation must return a checked result");
  }
}
}
int main()
{
  using Test = std::pair<std::string, std::function<void()>>;
  const std::vector<Test> tests{
      {"side tangent preserves wire resistance and center width", [] {
         for (const std::string input : {
                "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 SIDE_TANGENT=.1}",
                "metal M1 {Thickness .2 Resistance .12 SIDE_TANGENT -.1}"}) {
           const auto model = parse(input);
           near(evaluateWireResistance(model.conductors[0], {.2}, 1, 25, 25), .6);
           const auto geometry = evaluateWireGeometry(model.conductors[0], {.2}, ProcessEffectType::kCapacitance);
           near(checked(geometry).width, .2);
           near(checked(geometry).thickness, .2);
         }
       }},
      {"constant polynomial changes thickness and includes the width threshold on its left", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12
           POLYNOMIAL_BASED_THICKNESS_VARIATION {
             DENSITY_POLYNOMIAL_ORDERS {0} WIDTH_POLYNOMIAL_ORDERS {0}
             WIDTH_RANGES {.1} POLYNOMIAL_COEFFICIENTS {.1} POLYNOMIAL_COEFFICIENTS {.2}
           }})");
         const auto& conductor = model.conductors[0];
         near(checked(evaluateWireGeometry(conductor, {.1}, ProcessEffectType::kResistance)).thickness, .22);
         near(checked(evaluateWireGeometry(conductor, {.100001}, ProcessEffectType::kCapacitance)).thickness, .24);
         near(evaluateWireResistance(conductor, {.2}, 1, 25, 25), .5);
       }},
      {"ordered profiles update both walls and are independent of wire order", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.2} VALUES {.01}}
           ETCH_VS_WIDTH_AND_SPACING {SPACINGS {.1 .12} VALUES {0 .01}}})");
         const auto* conductor = &model.conductors[0];
         std::vector<WireGeometryInput> wires{{conductor, 0, .2, {{0, 1, kNoWireNeighbor, 1}}},
                                              {conductor, .3, .2, {{0, 1, 0, kNoWireNeighbor}}}};
         for (bool reverse : {false, true}) {
           if (reverse) {
             std::swap(wires[0], wires[1]);
             wires[0].spans[0].lower_neighbor = 1;
             wires[1].spans[0].upper_neighbor = 0;
           }
           const auto result = evaluateWireGeometryProfiles(wires, ProcessEffectType::kResistance);
           const auto& profiles = checked(result);
           for (const auto& profile : profiles) near(profile[0].geometry.width, .16);
           near(profiles[reverse ? 1 : 0][0].upper_spacing, .14);
           near(profiles[reverse ? 0 : 1][0].lower_spacing, .14);
         }
       }},
      {"non-square polynomial uses declared density rows width columns and fabricated density", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 ETCH=.02 SIDE_TANGENT=.01
           POLYNOMIAL_BASED_THICKNESS_VARIATION {
             DENSITY_POLYNOMIAL_ORDERS {2 0} WIDTH_POLYNOMIAL_ORDERS {3 1 0}
             POLYNOMIAL_COEFFICIENTS {1 2 3 4 5 6} DENSITY_BOUNDS_VS_WIDTH {(.2,0,1)}
           }} USE_SI_DENSITY=YES)");
         const auto geometry = evaluateWireGeometry(model.conductors[0], {.2, -1, -1, .8, .5}, ProcessEffectType::kCapacitance);
         // r=.25*(.008+.4+3)+(.032+1+6)=7.884, with drawn W=.2.
         near(checked(geometry).thickness, 1.7768);
         near(checked(geometry).width, .16);
       }},
      {"fabricated width controls both coefficient selection and powers", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 ETCH=.025
           POLYNOMIAL_BASED_THICKNESS_VARIATION {
             DENSITY_POLYNOMIAL_ORDERS {0} SI_WIDTH_POLYNOMIAL_ORDERS {1}
             WIDTH_RANGES {.16} POLYNOMIAL_COEFFICIENTS {1} POLYNOMIAL_COEFFICIENTS {2}
           }})");
         near(checked(evaluateWireGeometry(model.conductors[0], {.2}, ProcessEffectType::kResistance)).thickness, .23);
       }},
      {"computed fabricated width stays in the lower interval at a threshold", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 ETCH=.025
           POLYNOMIAL_BASED_THICKNESS_VARIATION {
             DENSITY_POLYNOMIAL_ORDERS {0} SI_WIDTH_POLYNOMIAL_ORDERS {0}
             WIDTH_RANGES {.15} POLYNOMIAL_COEFFICIENTS {.1} POLYNOMIAL_COEFFICIENTS {.2}
           }})");
         for (double width : {.2-1e-12, .2})
           near(checked(evaluateWireGeometry(model.conductors[0], {width}, ProcessEffectType::kResistance)).thickness, .22);
         near(checked(evaluateWireGeometry(model.conductors[0], {.2+1e-12}, ProcessEffectType::kResistance)).thickness, .24);
       }},
      {"exact width threshold takes precedence over nearby roundoff snapping", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12
           POLYNOMIAL_BASED_THICKNESS_VARIATION {
             DENSITY_POLYNOMIAL_ORDERS {0} WIDTH_POLYNOMIAL_ORDERS {0}
             WIDTH_RANGES {1 1.0000000000000002} POLYNOMIAL_COEFFICIENTS {.1}
             POLYNOMIAL_COEFFICIENTS {.2} POLYNOMIAL_COEFFICIENTS {.3}
           }})");
         near(checked(evaluateWireGeometry(model.conductors[0], {1.0000000000000002}, ProcessEffectType::kResistance)).thickness, .24);
       }},
      {"finalized trapezoids cannot introduce overlap between separated wires", [] {
         const auto model = parse("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 SIDE_TANGENT=.1}");
         const auto* c = &model.conductors[0];
         std::vector<WireGeometryInput> wires{{c, 0, .2, {{0,1,kNoWireNeighbor,1}}}, {c, .21, .2, {{0,1,0,kNoWireNeighbor}}}};
         rejects([&] { return evaluateWireGeometryProfiles(wires, ProcessEffectType::kCapacitance); });
         wires[1].center = .22;
         const auto touching = evaluateWireGeometryProfiles(wires, ProcessEffectType::kCapacitance);
         near(checked(touching)[0][0].geometry.top_width(), .22);
         wires[1].center = .19;  // Existing drawn overlap retains the contact convention.
         checked(evaluateWireGeometryProfiles(wires, ProcessEffectType::kCapacitance));
       }},
      {"standalone trapezoids respect fixed neighboring sidewalls", [] {
         for (const std::string tangent : {".1", "-.1"}) {
           const auto model = parse("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 SIDE_TANGENT=" + tangent + "}");
           rejects([&] { return evaluateWireGeometry(model.conductors[0], {.2,.005,.005}, ProcessEffectType::kCapacitance); });
           checked(evaluateWireGeometry(model.conductors[0], {.2,.01,.01}, ProcessEffectType::kCapacitance));
           checked(evaluateWireGeometry(model.conductors[0], {.2}, ProcessEffectType::kCapacitance));
         }
       }},
      {"density bounds interpolate by width before clipping input density", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12
           POLYNOMIAL_BASED_THICKNESS_VARIATION {
             DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {1}
             DENSITY_BOUNDS_VS_WIDTH {(.1,.05,1) (.2,.07,.5)}
           }})");
         for (const auto& [width, density, thickness] : std::vector<std::tuple<double, double, double>>{
                  {.15, 0, .212}, {.15, 1, .35}, {.05, 0, .21}, {.3, 1, .3}})
           near(checked(evaluateWireGeometry(model.conductors[0], {width, -1, -1, density}, ProcessEffectType::kResistance)).thickness, thickness);
         rejects([&] { return evaluateWireGeometry(model.conductors[0], {.15}, ProcessEffectType::kResistance); });
         rejects([&] { return evaluateWireGeometry(model.conductors[0], {.15,-1,-1,1.1}, ProcessEffectType::kResistance); });
       }},
      {"default density bounds use the query width for both limits", [] {
         const std::string polynomial = R"(POLYNOMIAL_BASED_THICKNESS_VARIATION {
             DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {1}})";
         const std::string table = "ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.1 .3} SPACINGS {.1 .4} VALUES {0 0 0 0}}";
         const auto one = parse("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 " + polynomial + table + "}");
         near(checked(evaluateWireGeometry(one.conductors[0], {.2,-1,-1,0}, ProcessEffectType::kResistance)).thickness, .2666666666666667);
         near(checked(evaluateWireGeometry(one.conductors[0], {.2,-1,-1,1}, ProcessEffectType::kResistance)).thickness, .3333333333333333);
         const auto repeated = parse("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 " + polynomial + table + table + "}");
         near(checked(evaluateWireGeometry(repeated.conductors[0], {.2,-1,-1,.5}, ProcessEffectType::kResistance)).thickness, .3);
       }},
      {"default density bounds combine source axes independently of table order", [] {
         const std::string polynomial = R"(POLYNOMIAL_BASED_THICKNESS_VARIATION {
             DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {1}})";
         const std::string first = "ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.1 .3} SPACINGS {.1 .4} VALUES {0 0 0 0}}";
         const std::string second = "ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.05 .2} SPACINGS {.2 .8} VALUES {0 0 0 0}}";
         for (const auto& tables : {first + second, second + first}) {
           const auto model = parse("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 " + polynomial + tables + "}");
           for (const auto& [width, density, thickness] : std::vector<std::tuple<double, double, double>>{
                    {.025, 0, .2117647058823529}, {.025, 1, .2666666666666667}, {.2, 0, .24},
                    {.2, .5, .3}, {.2, 1, .3333333333333333}, {2, 0, .3428571428571429}, {2, 1, .3904761904761905}})
             near(checked(evaluateWireGeometry(model.conductors[0], {width,-1,-1,density}, ProcessEffectType::kResistance)).thickness, thickness);
         }
       }},
      {"bound width floor does not replace the polynomial width", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.05 .3} SPACINGS {.1 .8} VALUES {0 0 0 0}}
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {1 0} WIDTH_POLYNOMIAL_ORDERS {0 1}
             POLYNOMIAL_COEFFICIENTS {1 0 0 1}}})");
         near(checked(evaluateWireGeometry(model.conductors[0], {.025,-1,-1,1}, ProcessEffectType::kResistance)).thickness, .2716666666666667);
       }},
      {"default bounds follow the selected width basis and preserve original etch axes", [] {
         const std::string body = R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.1 .3} SPACINGS {.1 .4} VALUES {.01 .01 .01 .01}}
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.2 .4} SPACINGS {.2 .3} VALUES {.01 .01 .01 .01}}
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {1} )";
         for (bool fabricated_width : {false, true}) {
           const auto model = parse(body + (fabricated_width ? "SI_WIDTH_POLYNOMIAL_ORDERS" : "WIDTH_POLYNOMIAL_ORDERS")
                                    + " {0} POLYNOMIAL_COEFFICIENTS {1}}}");
           const auto geometry = evaluateWireGeometry(model.conductors[0], {.2,-1,-1,1}, ProcessEffectType::kResistance);
           near(checked(geometry).width, .16);
           near(checked(geometry).thickness, fabricated_width ? .3230769230769231 : .3333333333333333);
         }
         const auto model = parse(body + "WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {1}}} USE_SI_DENSITY=YES");
         near(checked(evaluateWireGeometry(model.conductors[0], {.2,-1,-1,.1,1}, ProcessEffectType::kResistance)).thickness, .3333333333333333);
       }},
      {"explicit bounds override defaults and incomplete or qualified defaults remain checked", [] {
         const std::string start = "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 ";
         const std::string polynomial = R"(POLYNOMIAL_BASED_THICKNESS_VARIATION {
           DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {1} )";
         for (const std::string table : {"", "ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.1 .3} VALUES {0 0}}",
                  "ETCH_VS_WIDTH_AND_SPACING RESISTIVE_ONLY {WIDTHS {.1 .3} SPACINGS {.1 .4} VALUES {0 0 0 0}}",
                  "ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.1 .3} SPACINGS {.1 .4} VALUES {0 0 0 0}}"}) {
           const auto explicit_model = parse(start + table + polynomial + "DENSITY_BOUNDS_VS_WIDTH {(.1,.1,.2)}}}");
           near(checked(evaluateWireGeometry(explicit_model.conductors[0], {.2,-1,-1,1}, ProcessEffectType::kResistance)).thickness, .24);
         }
         for (const std::string table : {"", "ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.1 .3} VALUES {0 0}}",
                  "ETCH_VS_WIDTH_AND_SPACING RESISTIVE_ONLY {WIDTHS {.1 .3} SPACINGS {.1 .4} VALUES {0 0 0 0}}"}) {
           const auto model = parse(start + table + polynomial + "}}");
           rejects([&] { return evaluateWireGeometry(model.conductors[0], {.2,-1,-1,.5}, ProcessEffectType::kResistance); });
         }
       }},
      {"native table defaults use physical units before effect normalization", [] {
         const auto model = parse(R"(units {length 1nm} metal M1 {Thickness 200 Resistance .12
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {100 300} SPACINGS {100 400} VALUES {{0 0} {0 0}}}
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0}
             WIDTH_RANGES {200} POLYNOMIAL_COEFFICIENTS {{1}} POLYNOMIAL_COEFFICIENTS {{1}}}})");
         near(checked(evaluateWireGeometry(model.conductors[0], {.2,-1,-1,1}, ProcessEffectType::kResistance)).thickness, .3333333333333333);
       }},
      {"spatial resistance resolves width dependent density clipping", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=2 RHO=2
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.1 .5} SPACINGS {.5 1} VALUES {0 0 0 0}}
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0}
             POLYNOMIAL_COEFFICIENTS {1}}})");
         EffectiveWireSegment segment{0, 0, 1, {1, 2}};
         for (const auto endpoints : {std::pair{0.0, 1.0}, std::pair{1.0, 0.0}}) {
           segment.drawn_density = endpoints;
           near(integrateWireResistance(model.conductors[0], {1}, segment, 0, 1, 25, 25), .6386938489911597);
         }
       }},
      {"absolute thickness bounds apply after polynomial and side tangent uses final thickness", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 SIDE_TANGENT=-.1
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {0} WIDTH_POLYNOMIAL_ORDERS {0}
             WIDTH_RANGES {.1} POLYNOMIAL_COEFFICIENTS {-2} POLYNOMIAL_COEFFICIENTS {3} THICKNESS_BOUNDS {.1 .3}}})");
         near(checked(evaluateWireGeometry(model.conductors[0], {.1}, ProcessEffectType::kCapacitance)).thickness, .1);
         const auto geometry = evaluateWireGeometry(model.conductors[0], {.2}, ProcessEffectType::kCapacitance);
         near(checked(geometry).thickness, .3);
         near(checked(geometry).top_width(), .17);
         near(checked(geometry).bottom_width(), .23);
         near(evaluateWireResistance(model.conductors[0], {.2}, 1, 25, 25), .4);
       }},
      {"polynomial arithmetic preserves finite values before physical geometry validation", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0}
             POLYNOMIAL_COEFFICIENTS {-2} DENSITY_BOUNDS_VS_WIDTH {(.1,0,1)}}})");
         const auto& conductor = model.conductors[0];
         const WireGeometryQuery query{.2,-1,-1,.75};
         near(evaluatePolynomialThickness(*conductor.get_thickness_polynomial(), .1, query, {.2,.1}), -.05);
         rejects([&] { return finalizeWireGeometry(conductor, query, {.2,.1}, ProcessEffectType::kResistance); });
         rejects([&] { return evaluateWireResistance(conductor, query, 1, 25, 25); });
         rejects([&] { return thicknessDensityBreakpoints(*conductor.get_thickness_polynomial(), .1, query, {.2,.1}, .75, .9); });
       }},
      {"resistivity axis clipping does not clamp the geometric thickness", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.1
           RHO_VS_SI_WIDTH_AND_THICKNESS {WIDTH {.2} THICKNESS {.08 .12} VALUES {2 3}}
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {0} WIDTH_POLYNOMIAL_ORDERS {0}
             WIDTH_RANGES {.2} POLYNOMIAL_COEFFICIENTS {1} POLYNOMIAL_COEFFICIENTS {-.5}}})");
         near(checked(evaluateWireGeometry(model.conductors[0], {.2}, ProcessEffectType::kResistance)).thickness, .2);
         near(evaluateWireResistance(model.conductors[0], {.2}, 1, 25, 25), 75);
         near(checked(evaluateWireGeometry(model.conductors[0], {.4}, ProcessEffectType::kResistance)).thickness, .05);
         near(evaluateWireResistance(model.conductors[0], {.4}, 1, 25, 25), 100);
       }},
      {"polynomial units normalize each width power", [] {
         const auto model = parse(R"(units {length 1nm} metal M1 {Thickness 200 Resistance .12
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {0} WIDTH_POLYNOMIAL_ORDERS {2 0}
             WIDTH_RANGES {200} POLYNOMIAL_COEFFICIENTS {{.00001} {.1}} POLYNOMIAL_COEFFICIENTS {{.00001} {.1}}}})");
         near(checked(evaluateWireGeometry(model.conductors[0], {.2}, ProcessEffectType::kResistance)).thickness, .3);
       }},
      {"etch snapshot defers density dependent thickness until density is supplied", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 ETCH=.01 SIDE_TANGENT=.1
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0}
             POLYNOMIAL_COEFFICIENTS {1} DENSITY_BOUNDS_VS_WIDTH {(.1,0,1)}}})");
         const std::vector<WireGeometryInput> wires{{&model.conductors[0], 0, .2, {{0,1}}}};
         const auto snapshot = evaluateWireEtchProfiles(wires, ProcessEffectType::kResistance);
         near(checked(snapshot)[0][0].geometry.width, .18);
         const auto geometry = finalizeWireGeometry(model.conductors[0], {.2,-1,-1,.5}, checked(snapshot)[0][0].geometry, ProcessEffectType::kResistance);
         near(checked(geometry).thickness, .3);
         near(checked(geometry).top_width(), .21);
         rejects([&] { return evaluateWireGeometryProfiles(wires, ProcessEffectType::kResistance); });
       }},
      {"successive etch stages finalize polynomial thickness from the final width", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.1 .2} SPACINGS {.1} VALUES {.005 .01}}
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.18 .2} SPACINGS {.1} VALUES {.01 .02}}
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {0} SI_WIDTH_POLYNOMIAL_ORDERS {1}
             POLYNOMIAL_COEFFICIENTS {1}}})");
         const auto geometry = evaluateWireGeometry(model.conductors[0], {.2,.1,.1}, ProcessEffectType::kResistance);
         near(checked(geometry).width, .16);
         near(checked(geometry).thickness, .232);
       }},
      {"native coefficient rows are normalized into the common density row convention", [] {
         const auto model = parse(R"(metal M1 {Thickness .2 Resistance .12
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.1 .3} SPACINGS {.1 .4} VALUES {{0 0} {0 0}}}
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {1 0} WIDTH_POLYNOMIAL_ORDERS {2 1 0}
             WIDTH_RANGES {.3} POLYNOMIAL_COEFFICIENTS {{1 4} {2 5} {3 6}} POLYNOMIAL_COEFFICIENTS {{1 4} {2 5} {3 6}}}})");
         // r=.5*(.04+.4+3)+(.16+1+6)=8.88.
         near(checked(evaluateWireGeometry(model.conductors[0], {.2,-1,-1,.5}, ProcessEffectType::kResistance)).thickness, 1.976);
       }},
      {"top etch overrides tangent without changing the resistance cross section", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 SIDE_TANGENT=10
           ETCH_VS_WIDTH_AND_SPACING ETCH_FROM_TOP {WIDTHS {.1} VALUES {.01}}})");
         near(evaluateWireResistance(model.conductors[0], {.2}, 1, 25, 25), .6);
         rejects([&] { return evaluateWireGeometry(model.conductors[0], {.2}, ProcessEffectType::kCapacitance); });
       }},
      {"negative polynomial powers check singular inputs", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {-1} WIDTH_POLYNOMIAL_ORDERS {-2}
             POLYNOMIAL_COEFFICIENTS {.02} DENSITY_BOUNDS_VS_WIDTH {(.1,0,1)}}})");
         near(checked(evaluateWireGeometry(model.conductors[0], {.2,-1,-1,.5}, ProcessEffectType::kResistance)).thickness, .4);
         rejects([&] { return evaluateWireGeometry(model.conductors[0], {.2,-1,-1,0}, ProcessEffectType::kResistance); });
       }},
      {"finalization rejects deferred domains and invalid sidewall dimensions", [] {
         const auto model = parse("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 SIDE_TANGENT=.2}");
         rejects([&] { return evaluateWireGeometry(model.conductors[0], {.03}, ProcessEffectType::kCapacitance); });
         rejects([&] { return finalizeWireGeometry(model.conductors[0], {.2}, {.2,.2}, ProcessEffectType::kBoth); });
         const auto deferred = parse("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 THICKNESS_VS_DENSITY CAPACITIVE_ONLY {(.1,.2)}}");
         rejects([&] { return finalizeWireGeometry(deferred.conductors[0], {.2}, {.2,.2}, ProcessEffectType::kCapacitance); });
         near(checked(finalizeWireGeometry(deferred.conductors[0], {.2}, {.2,.2}, ProcessEffectType::kResistance)).thickness, .2);
       }},
      {"neighbor span boundaries propagate through successive etch stages", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12
           ETCH_VS_WIDTH_AND_SPACING {SPACINGS {.1 .3} VALUES {.01 .03}}
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.16 .18} VALUES {.01 .02}}
           ETCH_VS_WIDTH_AND_SPACING {SPACINGS {.15 .16} VALUES {0 .01}}})");
         const auto* c = &model.conductors[0];
         const std::vector<WireGeometryInput> wires{
             {c, 0, .2, {{0, 1, 1, 2}}},
             {c, -.3, .2, {{0, .5, kNoWireNeighbor, 0}, {.5, 1, 3, 0}}},
             {c, .3, .2, {{0, 1, 0, kNoWireNeighbor}}},
             {c, -.6, .2, {{.5, 1, kNoWireNeighbor, 1}}}};
         const auto result = evaluateWireGeometryProfiles(wires, ProcessEffectType::kResistance);
         const auto& master = checked(result)[0];
         if (master.size() != 2 || master[0].source_span != 0 || master[1].source_span != 0)
           throw std::runtime_error("effective spans lost their original environment ownership");
         near(master[0].start, 0); near(master[0].end, .5);
         near(master[1].start, .5); near(master[1].end, 1);
         near(master[0].geometry.width, .14); near(master[1].geometry.width, .13);
         near(master[1].geometry.center_shift, .005);
       }},
      {"invalid neighboring geometry cannot be treated as an absent neighbor", [] {
         const auto model = parse("CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12 ETCH=-.06}");
         const auto* c = &model.conductors[0];
         std::vector<WireGeometryInput> wires{{c, 0, .2, {{0, 1, kNoWireNeighbor, 1}}},
                                              {c, .3, .2, {{0, 1, 0, kNoWireNeighbor}}}};
         rejects([&] { return evaluateWireGeometryProfiles(wires, ProcessEffectType::kResistance); });
         wires[0].spans[0].upper_neighbor = 2;
         rejects([&] { return evaluateWireGeometryProfiles(wires, ProcessEffectType::kResistance); });
       }},
      {"existing overlapping rectangles use the contact table boundary", [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12
           ETCH_VS_WIDTH_AND_SPACING {SPACINGS {.01 1} VALUES {0 .01}}})");
         const auto* c = &model.conductors[0];
         const std::vector<WireGeometryInput> wires{{c, 0, .2, {{0, 1, kNoWireNeighbor, 1}}},
                                                    {c, .05, .2, {{0, 1, 0, kNoWireNeighbor}}}};
         const auto result = evaluateWireGeometryProfiles(wires, ProcessEffectType::kResistance);
         near(checked(result)[0][0].geometry.width, .19);
         near(checked(result)[0][0].upper_spacing, 0);
       }},
      {"existing touching rectangles retain a contact boundary during expansion", [] {
         const auto model = parse("CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12 ETCH=-.02}");
         const auto* c = &model.conductors[0];
         const std::vector<WireGeometryInput> wires{{c, 0, .2, {{0, 1, kNoWireNeighbor, 1}}},
                                                    {c, .2, .2, {{0, 1, 0, kNoWireNeighbor}}}};
         const auto result = evaluateWireGeometryProfiles(wires, ProcessEffectType::kResistance);
         near(checked(result)[0][0].geometry.width, .24);
         near(checked(result)[0][0].upper_spacing, 0);
       }},
      {"empty etch and thickness tables cannot silently become zero variation", [] {
         for (bool thickness : {false, true}) {
           auto model = parse("CONDUCTOR M1 { THICKNESS=.1 RPSQ=.12 }");
           auto& conductor = model.conductors[0];
           ProcessEtchTable table;
           table.set_effect_type(ProcessEffectType::kBoth);
           (thickness ? conductor.get_thickness_change_table_list() : conductor.get_etch_table_list()).push_back(table);
           rejects([&] { return evaluateWireGeometry(conductor, {.2, .1, .1}, ProcessEffectType::kResistance); });
         }
       }},
      {"readable physical models cannot silently use nominal geometry",
       [] {
         for (const auto& input : {"CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 THICKNESS_VS_DENSITY {(.1,.2)}}",
                                   "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {1} "
                                   "WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {.1}}}"}) {
           const auto model = parse(input);
           rejects([&] { return evaluateWireResistance(model.conductors[0], {.2}, 1, 25, 25); });
           rejects([&] { return evaluateWireGeometry(model.conductors[0], {.2}, ProcessEffectType::kCapacitance); });
         }
       }},
      {"via capacitance etch queries cannot bypass deferred model checks",
       [] {
         for (const std::string field : {"CAPACITIVE_ONLY_ETCH=.002",
                                         "ETCH_VS_CONTACT_AND_GATE_SPACINGS CAPACITIVE_ONLY {"
                                         "CONTACT_TO_CONTACT_SPACINGS {.1} GATE_TO_CONTACT_SPACINGS {.1} VALUES {0}}"}) {
           const auto model = parse("VIA V1 {AREA=.01 RPV=4 " + field + "}");
           rejects([&] { return model.vias[0].query_etch(ProcessEffectType::kCapacitance, .1, .1); });
           near(checked(model.vias[0].query_etch(ProcessEffectType::kResistance, .1, .1)).get_width(), 0);
           near(evaluateViaResistance(model.vias[0], .1, .1, 25, 25), 4);
         }
       }},
      {"top etch restricts capacitance without changing resistance geometry",
       [] {
         for (const std::string input : {"CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 ETCH_VS_WIDTH_AND_SPACING ETCH_FROM_TOP {WIDTHS {.1} VALUES {.01}}}",
                                         "metal M1 {Thickness .2 Resistance .12 ETCH_VS_WIDTH_AND_SPACING ETCH_FROM_TOP {WIDTHS {.1} VALUES {{.01}}}}"}) {
           const auto model = parse(input);
           near(evaluateWireResistance(model.conductors[0], {.2}, 1, 25, 25), .6);
           rejects([&] { return evaluateWireGeometry(model.conductors[0], {.2}, ProcessEffectType::kCapacitance); });
         }
       }},
      {"missing optional reference area is checked when evaluated",
       [] {
         auto model = parse("VIA V1 {RPV=4}");
         rejects([&] { return evaluateViaResistance(model.vias[0], .1, .1, 25, 25); });
         rejects([&] { return model.vias[0].query_resistance(.01); });
         model.vias[0].set_area(.01);
         near(evaluateViaResistance(model.vias[0], .1, .1, 25, 25), 4);
       }},
      {"unsupported capacitance models do not block resistance",
       [] {
         const auto model = parse(R"(VIA V1 {AREA=.01 RPV=4 CAPACITIVE_ONLY_ETCH=.002
        ETCH_VS_CONTACT_AND_GATE_SPACINGS CAPACITIVE_ONLY {NUMBER_OF_TABLES=1
          T1 {CONTACT_TO_CONTACT_SPACINGS {.1} GATE_TO_CONTACT_SPACINGS {.1} VALUES {.002}}}
      })");
         near(evaluateViaResistance(model.vias[0], .1, .1, 25, 25), 4);
         const auto wire = parse("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 THICKNESS_VS_DENSITY CAPACITIVE_ONLY {(.1,.2)}}");
         near(evaluateWireResistance(wire.conductors[0], {.2}, 1, 25, 25), .6);
         rejects([&] { return evaluateWireGeometry(wire.conductors[0], {.2}, ProcessEffectType::kCapacitance); });
       }},
      {"thickness-width resistivity table takes precedence",
       [] {
         auto m = parse(R"(metal M1 {Thickness .2 Resistance 99
        RHO_VS_WIDTH_AND_SPACING {WIDTHS {.2} VALUES {{.02}}}
        RHO_VS_SI_WIDTH_AND_THICKNESS {WIDTH {.2} THICKNESS {.2} VALUES {{.04}}} })");
         near(evaluateWireResistance(m.conductors[0], {.2, .1, .3}, 1, 25, 25), 1);
       }},
      {"nominal resistivity and ten-percent thickness change",
       [] {
         auto a = parse("CONDUCTOR M1 {THICKNESS=.2 RHO=.02}");
         auto b = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RHO=.02
        THICKNESS_VS_WIDTH_AND_SPACING {WIDTHS {.2} VALUES {.1}} })");
         near(evaluateWireResistance(a.conductors[0], {.2}, 1, 25, 25), .5);
         near(evaluateWireResistance(b.conductors[0], {.2}, 1, 25, 25), .02 / (.2 * .22));
       }},
      {"equivalent etch tables in both syntaxes",
       [] {
         for (const auto& input : {"CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12 ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.2} VALUES {.02}}}",
                                   "metal M1 {Thickness .1 Resistance .12 ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.2} VALUES {{.02}}}}"}) {
           auto m = parse(input);
           near(evaluateWireResistance(m.conductors[0], {.2}, 1, 25, 25), .75);
         }
       }},
      {"equivalent inputs and domain-specific etch",
       [] {
         auto a = parse("CONDUCTOR M1 { THICKNESS=.1 RPSQ=.12 RESISTIVE_ONLY_ETCH=.02 CAPACITIVE_ONLY_ETCH=.01 }");
         auto b = parse("metal M1 { Thickness .1 Resistance .12 RESISTIVE_ONLY_ETCH .04 CAPACITIVE_ONLY_ETCH .02 }");
         for (const auto* p : {&a, &b}) {
           near(evaluateWireResistance(p->conductors[0], {.2, .1, .3}, 1, 25, 25), .75);
           near(checked(evaluateWireGeometry(p->conductors[0], {.2, .1, .3}, ProcessEffectType::kCapacitance)).width, .18);
         }
       }},
      {"priority is resolved separately for resistance and capacitance",
       [] {
         auto m = parse(R"(metal M1 { Thickness .1 Resistance .12 RESISTIVE_ONLY_ETCH .04 CAPACITIVE_ONLY_ETCH .09 ETCH .06
        ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.2} VALUES {{.01}}} })");
         near(checked(evaluateWireGeometry(m.conductors[0], {.2, .1, .3}, ProcessEffectType::kResistance)).width, .16);
         near(checked(evaluateWireGeometry(m.conductors[0], {.2, .1, .3}, ProcessEffectType::kCapacitance)).width, .18);
       }},
      {"fractional thickness affects only the selected domain",
       [] {
         auto m = parse(R"(metal M1 { Thickness .1 RHO .012
        THICKNESS_VS_WIDTH_AND_SPACING RESISTIVE_ONLY {WIDTHS {.2} VALUES {{1}}} })");
         near(evaluateWireResistance(m.conductors[0], {.2, .1, .3}, 1, 25, 25), .3);
         near(checked(evaluateWireGeometry(m.conductors[0], {.2, .1, .3}, ProcessEffectType::kCapacitance)).thickness, .1);
       }},
      {"width-spacing resistance and asymmetric etch",
       [] {
         auto m = parse(R"(metal M1 { Thickness .1 RHO_VS_WIDTH_AND_SPACING { WIDTHS {.1 .3} SPACINGS {.1 .3}
        VALUES {{.01 .02} {.03 .04}} }
        ETCH_VS_WIDTH_AND_SPACING {SPACINGS {.1 .3} VALUES {{.01 .03}}} })");
         const auto g = evaluateWireGeometry(m.conductors[0], {.2, .1, .3}, ProcessEffectType::kResistance);
         near(checked(g).width, .16);
         near(checked(g).center_shift, -.01);
         near(evaluateWireResistance(m.conductors[0], {.2, .1, .3}, 1, 25, 25), 1.0);
       }},
      {"temperature coefficient interpolation",
       [] {
         auto m = parse(R"(metal M1 { Thickness .1 Resistance .12 Tnom 25
        CRT_VS_SI_WIDTH {{.1 .002 0} {.3 .004 0}} })");
         near(evaluateWireResistance(m.conductors[0], {.2, .1, .3}, 1, 125, 25), .78);
       }},
      {"via area table interpolation and reference-cut limits",
       [] {
         auto table = parse("via V1 { Area .01 RPV_VS_AREA {{.01 4} {.03 2}} }");
         near(evaluateViaResistance(table.vias[0], .1, .2, 25, 25), 3);
         rejects([&] { return evaluateViaResistance(table.vias[0], .1, .4, 25, 25); });
         auto scalar = parse("via V1 { Area .01 Resistance 4 }");
         near(evaluateViaResistance(scalar.vias[0], .1, .1, 25, 25), 4);
         rejects([&] { return evaluateViaResistance(scalar.vias[0], .1, .2, 25, 25); });
       }},
      {"invalid resulting geometry and temperature are errors",
       [] {
         auto m = parse("metal M1 { Thickness .1 Resistance .12 RESISTIVE_ONLY_ETCH .2 Coeff_RT1 -.02 }");
         rejects([&] { return evaluateWireResistance(m.conductors[0], {.2, .1, .3}, 1, 25, 25); });
         rejects([&] { return evaluateWireResistance(m.conductors[0], {.4, .1, .3}, 1, 125, 25); });
         const auto expansion = parse("CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12 ETCH=-.02}");
         rejects([&] { return evaluateWireGeometry(expansion.conductors[0], {.2, .01, .01}, ProcessEffectType::kResistance); });
       }},
  };
  int failures = 0;
  for (const auto& [name, test] : tests) {
    try { test(); std::cout << "PASS: " << name << '\n'; }
    catch (const std::exception& e) { ++failures; std::cerr << "FAIL: " << name << ": " << e.what() << '\n'; }
  }
  return failures ? 1 : 0;
}
