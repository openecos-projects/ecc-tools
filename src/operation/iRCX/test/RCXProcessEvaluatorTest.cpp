#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>

#include "ProcessEvaluator.hpp"
#include "ProcessReader.hpp"

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
      {"readable physical models cannot silently use nominal geometry",
       [] {
         for (const auto& input : {"CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 SIDE_TANGENT=.1}", "metal M1 {Thickness .2 Resistance .12 SIDE_TANGENT .1}",
                                   "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 THICKNESS_VS_DENSITY {(.1,.2)}}",
                                   "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {0} "
                                   "WIDTH_POLYNOMIAL_ORDERS {0} WIDTH_RANGES {.1} POLYNOMIAL_COEFFICIENTS {.1} POLYNOMIAL_COEFFICIENTS {.2}}}"}) {
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
       }},
  };
  int failures = 0;
  for (const auto& [name, test] : tests) {
    try { test(); std::cout << "PASS: " << name << '\n'; }
    catch (const std::exception& e) { ++failures; std::cerr << "FAIL: " << name << ": " << e.what() << '\n'; }
  }
  return failures ? 1 : 0;
}
