#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../source/process/parser/itf/syntax/ItfDocument.hpp"
#include "../source/process/parser/tech/syntax/TechDocument.hpp"
#include "ProcessReader.hpp"

namespace {
void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected)
{
  require(std::isfinite(actual) && std::abs(actual - expected) < 1e-10, "unexpected normalized value");
}
ircx::ProcessTechnology parse(const std::string& text)
{
  auto result = ircx::parseProcess(text, "fixture");
  if (!result) {
    std::string message;
    for (const auto& d : result.diagnostics) message += d.code + ": " + d.message + "\n";
    throw std::runtime_error(message);
  }
  return std::move(*result.technology);
}
void rejected(const std::string& text, const std::string& code = {})
{
  auto result = ircx::parseProcess(text, "bad.fixture");
  require(!result, "invalid process accepted");
  require(!result.diagnostics.empty(), "missing diagnostic");
  require(result.diagnostics.front().source_name == "bad.fixture", "missing source name");
  if (!code.empty()) {
    bool found = false;
    for (const auto& d : result.diagnostics) found |= d.code == code;
    require(found, "expected diagnostic " + code + ", got " + result.diagnostics.front().code);
  }
}
}

int main(int argc, char** argv)
{
  if (argc > 1) {
    int failures = 0;
    for (int i = 1; i < argc; ++i) {
      auto result = ircx::readProcessFile(argv[i]);
      for (const auto& d : result.diagnostics)
        std::cerr << d.source_name << ':' << d.begin.line << ':' << d.begin.column << ": " << d.code << ": " << d.message << '\n';
      if (!result) ++failures;
      else std::cout << "loaded " << result.technology->conductors.size() << " conductors, " << result.technology->vias.size() << " vias\n";
    }
    return failures ? 1 : 0;
  }
  using Test = std::pair<std::string, std::function<void()>>;
  const std::vector<Test> tests{
      {"file read errors return diagnostics without exceptions",
       [] {
         const auto directory = std::filesystem::temp_directory_path();
         const auto process = ircx::readProcessFile(directory);
         require(!process && !process.diagnostics.empty() && process.diagnostics.front().code == "file_io", "directory did not produce file_io");
       }},
      {"syntax file read errors return diagnostics without exceptions",
       [] {
         const auto syntax = ircx::tech::syntax::parse_file(std::filesystem::temp_directory_path().string());
         require(!syntax && !syntax.diagnostics.empty(), "syntax file read did not produce diagnostics");
         const auto itf = ircx::itf::syntax::parse_file(std::filesystem::temp_directory_path().string());
         require(!itf && !itf.diagnostics.empty() && itf.diagnostics.front().code == "file_io", "ITF syntax read did not produce diagnostics");
       }},
      {"ITF normalized parameters own their data",
       [] {
         auto model = parse("CONDUCTOR M1 { THICKNESS=.2 RPSQ=.12 RESISTIVE_ONLY_ETCH=.02 }");
         require(model.conductors.size() == 1, "missing conductor");
         near(model.conductors[0].get_thickness(), .2);
         near(model.conductors[0].get_sheet_resistance(), .12);
         near(model.conductors[0].get_resistive_only_etch(), .02);
       }},
      {"ITF comments cannot select another frontend",
       [] {
         const auto model = parse(
             "\xef\xbb\xbf$ metal fake { Thickness 3 Resistance 4 } \"ignored\r\n"
             "cOnDuCtOr \"MiXeD\" { THICKNESS=+2.e-1$ inline comment\n"
             "RPSQ=1.2E-1 } $ trailing garbage {}()\n");
         require(model.conductors[0].get_layer_name() == "MiXeD", "name spelling changed");
         near(model.conductors[0].get_thickness(), .2);
         near(model.conductors[0].get_sheet_resistance(), .12);
         const auto tech = parse("metal $wire {Thickness .2 Resistance .12}");
         require(tech.conductors[0].get_layer_name() == "$wire", "ITF comment rule changed Tech identifiers");
       }},
      {"ITF block modifiers retain table effect and order",
       [] {
         const auto model = parse(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12
           ETCH_VS_WIDTH_AND_SPACING {
             WIDTHS {.1 .2} RESISTIVE_ONLY SPACINGS {.1 .2 .3}
             VALUES {( .001, .002, .003 ) { .004 .005 .006 }}
           }
           ETCH_VS_WIDTH_AND_SPACING CAPACITIVE_ONLY {WIDTHS {.1} VALUES {.007}}
         })");
         const auto& tables = model.conductors[0].get_etch_table_list();
         require(tables.size() == 2, "table count changed");
         require(tables[0].get_effect_type() == ircx::ProcessEffectType::kResistance, "inner modifier lost");
         require(tables[1].get_effect_type() == ircx::ProcessEffectType::kCapacitance, "prefix modifier lost");
         near(tables[0].get_table().query(.2, .3).value(), .006);
         near(tables[0].get_table().query(.15, .15).value(), .003);
       }},
      {"ITF end-of-file diagnostic points past trailing comments",
       [] {
         const std::string text
             = "\xef\xbb\xbf"
               "CONDUCTOR M1 {\r\nTHICKNESS=.2 RPSQ=.12 // missing brace\r\n";
         const auto result = ircx::parseProcess(text, "itf-eof.fixture");
         require(!result && result.diagnostics.size() == 1, "missing syntax diagnostic");
         const auto& diagnostic = result.diagnostics.front();
         require(diagnostic.code == "syntax_error", "wrong error classification");
         require(diagnostic.begin.line == 3 && diagnostic.begin.column == 1 && diagnostic.begin.offset == text.size(), "incorrect EOF location");
       }},
      {"ITF nested tables enforce the configured delimiter limit",
       [] {
         ircx::ProcessParseOptions options;
         options.max_nesting = 3;
         const std::string text = "CONDUCTOR M1 {THICKNESS=.2 RPSQ_VS_SI_WIDTH {(.1,.12) (.2,.24)}}";
         require(static_cast<bool>(ircx::parseProcess(text, "nesting.fixture", options)), "allowed nesting rejected");
         options.max_nesting = 2;
         const auto result = ircx::parseProcess(text, "nesting.fixture", options);
         require(!result && result.diagnostics.front().begin.offset == text.find('('), "nesting error is not at opening delimiter");
       }},
      {"ITF numeric tokens are consumed completely",
       [] {
         for (const std::string number : {"1e+", "1e999", "1.2.3", ".", "+", "0.12ohm", "0x1p-1"})
           rejected("CONDUCTOR M1 {THICKNESS=.2 RPSQ=" + number + "}", "invalid_number");
         std::string nul = "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12";
         nul.push_back('\0');
         nul += "9}";
         rejected(nul);
       }},
      {"ITF syntax failures retain ranges and publish no partial document",
       [] {
         const std::string text
             = "\xef\xbb\xbf"
               "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12}\r\n"
               "CONDUCTOR M2 {THICKNESS=.2 RPSQ=1e+}";
         const auto result = ircx::itf::syntax::parse(text, "range.fixture");
         require(!result && result.diagnostics.size() == 1, "failed parse published a document");
         const auto& diagnostic = result.diagnostics.front();
         require(diagnostic.code == "invalid_number" && diagnostic.source_name == "range.fixture", "lost lexical diagnostic");
         require(diagnostic.range.begin.line == 2 && diagnostic.range.begin.column == 33 && diagnostic.range.begin.offset == text.find("1e+")
                     && diagnostic.range.end.offset == text.find("1e+") + 3,
                 "numeric diagnostic range changed");
         const auto valid = ircx::itf::syntax::parse("CONDUCTOR \"CAPACITIVE_ONLY\" {RPSQ=.12}");
         require(static_cast<bool>(valid), "failed parse contaminated the next call");
         require(valid.document->statements.front().name == "CAPACITIVE_ONLY", "keyword name changed");
       }},
      {"ITF malformed structure cannot be ignored",
       [] {
         for (const std::string text : {
                  "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 RESISTIVE_ONLY}",
                  "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 VALUES {(RPSQ=.1)}}",
                  "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12}}",
                  "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12} \"unterminated",
              }) {
           rejected(text);
         }
         rejected("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 ETCH_VS_WIDTH_AND_SPACING {RESISTIVE_ONLY CAPACITIVE_ONLY WIDTHS {.1} VALUES {.01}}}", "invalid_option");
       }},
      {"ITF syntax retains unsupported table declarations for model diagnostics",
       [] {
         const std::string text
             = "VIA V1 {AREA=.01 RPV=4 ETCH_VS_CONTACT_AND_GATE_SPACINGS CAPACITIVE_ONLY {"
               "NUMBER_OF_TABLES=1 T1 {CONTACT_TO_CONTACT_SPACINGS {.1} GATE_TO_CONTACT_SPACINGS {.1} VALUES {0}}}}";
         const auto syntax = ircx::itf::syntax::parse(text);
         require(static_cast<bool>(syntax), "unsupported table was rejected as malformed syntax");
         const auto result = ircx::parseProcess(text);
         require(static_cast<bool>(result), "known via geometry prevented document loading");
         require(!result.diagnostics.empty(), "unavailable evaluation model was not diagnosed");
       }},
      {"Tech units and reference temperature",
       [] {
         auto model = parse("units { length 1nm resistance 1mohm } metal M1 { Thickness 200 Resistance 120 Tnom 25 Coeff_RT1 .003 }");
         near(model.conductors[0].get_thickness(), .2);
         near(model.conductors[0].get_sheet_resistance(), .12);
         near(model.conductors[0].get_tmpr_coefficient1(), .003);
       }},
      {"BOM comments and keyword layer names",
       [] {
         auto model = parse("\xef\xbb\xbf# CONDUCTOR X { RPSQ=2 }\r\nMeTaL CONDUCTOR { Thickness .2 Resistance .12 }");
         require(model.conductors[0].get_layer_name() == "CONDUCTOR", "layer case changed");
         auto itf = parse("// metal X { Resistance 5 }\nCONDUCTOR \"METAL\" { THICKNESS=.2 RPSQ=.12 }");
         require(itf.conductors[0].get_layer_name() == "METAL", "quoted name changed");
       }},
      {"via-only documents",
       [] {
         near(parse("VIA V1 { AREA=.01 RPV=4 }").vias[0].get_area(), .01);
         near(parse("via V1 { Area .01 Resistance 4 }").vias[0].get_resistance(), 4.0);
       }},
      {"ITF dollar comments cannot override weak via-only signatures",
       [] {
         for (const std::string prefix : {"$ \"unmatched\n", "$ metal fake {Thickness 3 Resistance 4}\n"}) {
           const auto table = parse(prefix + "VIA V1 {RPV_VS_AREA {(.01,4)(.02,8)}}");
           require(table.vias.size() == 1 && table.vias[0].get_layer_name() == "V1", "comment changed via declarations");
           near(table.vias[0].get_resistance_by_area_table().query(.02).value(), 8);
           const auto scalar = parse(prefix + "VIA V1 {WIDTH=.1 RESISTANCE=4}");
           near(scalar.vias[0].get_resistance(), 4);
           near(scalar.vias[0].get_area(), .01);
         }
       }},
      {"keyword reference names are not format signatures",
       [] {
         auto m = parse("TECHNOLOGY THICKNESS VIA V1 { FROM=AREA TO=THICKNESS AREA=.01 RPV=4 }");
         require(m.vias[0].get_from_layer_name() == "AREA", "reference name lost");
         auto c = parse("DIELECTRIC AREA {THICKNESS=1 ER=4} CONDUCTOR M1 {THICKNESS=.2 RPSQ=.1 MEASURED_FROM=AREA}");
         near(c.conductors[0].get_sheet_resistance(), .1);
       }},
      {"area resistivity and case-sensitive units",
       [] {
         auto m = parse("units { length 1nm resistance 1mohm } via V1 { Area 10000 Resistance 4000 }");
         near(m.vias[0].get_area(), .01);
         near(m.vias[0].get_resistance(), 4);
         auto r = parse("units {length 1nm resistance 1mohm} metal M1 { Thickness 100 RHO 12000 }");
         near(r.conductors[0].get_resistivity(), .012);
         near(parse("metal M1 {Thickness .1 Resistance 1Mohm}").conductors[0].get_sheet_resistance(), 1e6);
         near(parse("metal M1 {Thickness .1 Resistance 1mohm}").conductors[0].get_sheet_resistance(), .001);
       }},
      {"dielectric surface fields load with default options",
       [] {
         const std::string input = "dielectric D1 { Thickness .1 Constant 3.9 SW_T .01 TW_T .02 } metal M1 { Thickness .1 Resistance .12 }";
         auto m = ircx::parseProcess(input, "surface.fixture");
         require(static_cast<bool>(m), "default surface fields rejected");
         near(m.technology->conductors[0].get_etch(), 0);
         rejected("dielectric D1 {SW_T broken} metal M1 {Thickness .1 Resistance .12}");
       }},
      {"dielectric scalar fields cannot be repeated",
       [] {
         for (const std::string field : {"SW_T", "TW_T", "Thickness", "Constant", "Height"})
           rejected("dielectric D1 {" + field + " .1 " + field + " .2} metal M1 {Thickness .1 Resistance .12}");
       }},
      {"deferred polynomial models still validate their fields",
       [] {
         const std::string prefix = "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 POLYNOMIAL_BASED_THICKNESS_VARIATION {";
         for (const std::string body : {
                  "BOGUS=1",
                  "DENSITY_POLYNOMIAL_ORDERS {0} DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {.1}",
                  "DENSITY_POLYNOMIAL_ORDERS {0 0} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {.1 .2}",
                  "DENSITY_POLYNOMIAL_ORDERS {1.5} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {.1}",
                  "DENSITY_POLYNOMIAL_ORDERS {0 1} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {.1}",
                  "DENSITY_POLYNOMIAL_ORDERS {0} WIDTH_POLYNOMIAL_ORDERS {0} WIDTH_RANGES {.1} POLYNOMIAL_COEFFICIENTS {.1}",
                  "DENSITY_POLYNOMIAL_ORDERS=broken WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {.1}",
              })
           rejected(prefix + body + "}}");
       }},
      {"deferred tables reject malformed numbers and shapes",
       [] {
         for (const std::string field :
              {"RPV_VS_NUM=broken", "RPV_VS_NUM {1}", "RPV_VS_NUM {BOGUS=1}", "ETCH_VS_CONTACT_AND_GATE_SPACINGS CAPACITIVE_ONLY {BOGUS=1}",
               "ETCH_VS_CONTACT_AND_GATE_SPACINGS CAPACITIVE_ONLY {NUMBER_OF_TABLES=2 T1 {"
               "CONTACT_TO_CONTACT_SPACINGS {.1} GATE_TO_CONTACT_SPACINGS {.1} VALUES {0}}}"})
           rejected("VIA V1 {AREA=.01 RPV=4 " + field + "}");
         for (const std::string field : {"THICKNESS_VS_DENSITY=broken", "THICKNESS_VS_DENSITY {(.1,.2,.3)}", "BOTTOM_THICKNESS_VS_SI_WIDTH {BOGUS=1}",
                                         "DENSITY_BOX_WEIGHTING_FACTOR {1}", "WIDTH_SI_VS_WIDTH_AND_SPACING {WIDTHS {.1} VALUES {broken=1}}"})
           rejected("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 " + field + "}");
       }},
      {"top etch declarations load with explicit evaluation restrictions",
       [] {
         for (const std::string input : {"CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 ETCH_VS_WIDTH_AND_SPACING ETCH_FROM_TOP {WIDTHS {.1} VALUES {.01}}}",
                                         "metal M1 {Thickness .2 Resistance .12 ETCH_VS_WIDTH_AND_SPACING ETCH_FROM_TOP {WIDTHS {.1} VALUES {{.01}}}}"}) {
           const auto model = parse(input);
           require(model.conductors[0].get_evaluation_issues().size() == 1, "top etch lost its evaluation restriction");
         }
         rejected("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 ETCH_VS_WIDTH_AND_SPACING ETCH_FROM_TOP {WIDTHS {.1 .2} VALUES {.01}}}");
         rejected("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 ETCH_VS_WIDTH_AND_SPACING ETCH_FROM_TOP CAPACITIVE_ONLY {WIDTHS {.1} VALUES {.01}}}", "invalid_option");
       }},
      {"valid deferred tables retain optional fields and repeated records",
       [] {
         parse(
             "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 POLYNOMIAL_BASED_THICKNESS_VARIATION {"
             "SI_DENSITY_POLYNOMIAL_ORDERS {1 0} SI_WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {.1 .2} "
             "DENSITY_BOUNDS_VS_WIDTH {(.1 .2 .8)} THICKNESS_BOUNDS {.1 .3}}}");
         const auto via = parse("VIA V1 {RPV_VS_NUM {(1,4) (2,2)}}");
         require(via.vias[0].get_evaluation_issues().size() == 1, "via count model restriction lost");
       }},
      {"ITF layer metadata and density settings do not prevent loading",
       [] {
         auto model = parse(
             "USE_SI_DENSITY=YES CONDUCTOR G1 {LAYER_TYPE=GATE THICKNESS=.1 RPSQ=4 GATE_TO_CONTACT_SMIN=.05}"
             "CONDUCTOR D1 {LAYER_TYPE=DIFFUSION THICKNESS=.2 RPSQ=5}");
         require(model.conductors.size() == 2, "device conductor lost");
         require(model.use_si_density == true, "density setting lost");
         require(model.conductors[0].get_layer_type() == "GATE" && model.conductors[1].get_layer_type() == "DIFFUSION", "layer types lost");
         near(model.conductors[0].get_sheet_resistance(), 4);
         rejected("USE_SI_DENSITY=MAYBE CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12}", "invalid_value");
         rejected("USE_SI_DENSITY=YES USE_SI_DENSITY=NO CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12}", "duplicate_field");
       }},
      {"ITF optional via reference area survives loading",
       [] {
         const auto model = parse("VIA contact {FROM=SUBSTRATE TO=M1 RPV=.001}");
         require(model.vias.size() == 1 && model.vias[0].get_area() < 0, "absent area was invented");
         near(model.vias[0].get_resistance(), .001);
         rejected("VIA contact {AREA=0 RPV=.001}", "invalid_value");
         rejected("VIA contact {AREA=-1 RPV=.001}", "invalid_value");
       }},
      {"polynomial declarations retain coefficients and geometry without a blanket rejection",
       [] {
         const auto result = ircx::parseProcess(R"(CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 SIDE_TANGENT=.1
           POLYNOMIAL_BASED_THICKNESS_VARIATION {
             DENSITY_POLYNOMIAL_ORDERS {0 1} WIDTH_POLYNOMIAL_ORDERS {0}
             WIDTH_RANGES {.1} POLYNOMIAL_COEFFICIENTS {0 .1} POLYNOMIAL_COEFFICIENTS {0 .2}
           }
         })");
         require(static_cast<bool>(result), "known physical records rejected while reading");
         std::size_t limitations = 0;
         for (const auto& diagnostic : result.diagnostics)
           limitations += diagnostic.code == "unevaluated_model" && diagnostic.severity == ircx::ProcessSeverity::kWarning;
         require(limitations == 0, "implemented geometry was marked unevaluated");
         require(result.technology->conductors[0].get_thickness_polynomial().has_value(), "normalized polynomial missing");
         near(result.technology->conductors[0].get_side_tangent(), .1);
         const auto& properties = result.technology->properties;
         require(properties.size() == 1 && properties[0].key == "CONDUCTOR", "source declaration lost");
         const auto& polynomial = properties[0].children.back();
         require(polynomial.key == "POLYNOMIAL_BASED_THICKNESS_VARIATION" && polynomial.children.size() == 5, "polynomial records lost");
         require(polynomial.children[3].key == "POLYNOMIAL_COEFFICIENTS" && polynomial.children[4].key == "POLYNOMIAL_COEFFICIENTS",
                 "repeated coefficient records lost");
         near(polynomial.children[3].values.back().value, .1);
         near(polynomial.children[4].values.back().value, .2);
       }},
      {"global density basis is resolved after every conductor declaration",
       [] {
         const std::string body = "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 POLYNOMIAL_BASED_THICKNESS_VARIATION {"
                                  "DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {.1}}}";
         for (const std::string& input : {"USE_SI_DENSITY=YES " + body, body + " USE_SI_DENSITY=YES"}) {
           const auto model = parse(input);
           const auto& polynomial = *model.conductors[0].get_thickness_polynomial();
           require(polynomial.density_basis == ircx::GeometryBasis::kFabricated && polynomial.width_basis == ircx::GeometryBasis::kDrawn,
                   "global density switch also changed width or depended on declaration order");
         }
         for (const std::string prefix : {"SI_DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0}",
                                          "DENSITY_POLYNOMIAL_ORDERS {1} SI_WIDTH_POLYNOMIAL_ORDERS {0}"}) {
           const auto local = "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 POLYNOMIAL_BASED_THICKNESS_VARIATION {" + prefix
                            + " POLYNOMIAL_COEFFICIENTS {.1}}}";
           parse(local);
           parse(local + " USE_SI_DENSITY=NO");
           rejected(local + " USE_SI_DENSITY=YES", "conflicting_geometry_basis");
         }
       }},
      {"polynomial constraints reject invalid bounds orders and competing thickness models",
       [] {
         const std::string base = "CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 POLYNOMIAL_BASED_THICKNESS_VARIATION {"
                                  "DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {.1} ";
         for (const std::string bound : {"DENSITY_BOUNDS_VS_WIDTH {(.1,-.1,.9)}", "DENSITY_BOUNDS_VS_WIDTH {(.1,0,1.1)}",
                                         "THICKNESS_BOUNDS {-.1 .3}", "THICKNESS_BOUNDS {.2 .3}", "THICKNESS_BOUNDS {.1 .2}"})
           rejected(base + bound + "}}");
         for (const std::string field : {"THICKNESS_VS_DENSITY {(.1,.2)}",
                                         "THICKNESS_VS_WIDTH_AND_SPACING {WIDTHS {.1} VALUES {.1}}"})
           rejected(base + "} " + field + "}", "conflicting_thickness");
         rejected("CONDUCTOR M1 {THICKNESS=.2 RPSQ=.12 POLYNOMIAL_BASED_THICKNESS_VARIATION {"
                  "DENSITY_POLYNOMIAL_ORDERS {1e20} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {.1}}}");
       }},
      {"polynomial density accepts one physical window with unit weight",
       [] {
         const std::string polynomial = R"(POLYNOMIAL_BASED_THICKNESS_VARIATION {
           DENSITY_POLYNOMIAL_ORDERS {1} WIDTH_POLYNOMIAL_ORDERS {0} POLYNOMIAL_COEFFICIENTS {1}
           DENSITY_BOUNDS_VS_WIDTH {(.1,0,1)}})";
         for (const std::string& fields : {"DENSITY_BOX_WEIGHTING_FACTOR {(20,1)} " + polynomial,
                                          polynomial + " DENSITY_BOX_WEIGHTING_FACTOR {(20,1)}"}) {
           const auto model = parse("HALF_NODE_SCALE_FACTOR=.9 CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12 " + fields + "}");
           const auto& conductor = model.conductors[0];
           near(conductor.get_thickness_polynomial()->density_window, 20);
           require(conductor.get_evaluation_issues().empty(), "supported density window is still deferred");
         }
         const auto nominal = parse("CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12 " + polynomial + "}");
         near(nominal.conductors[0].get_thickness_polynomial()->density_window, 50);
         const auto native = parse(R"(units {length 1nm} metal M1 {Thickness 100 Resistance .12
           DENSITY_BOX_WEIGHTING_FACTOR {{20000 1}}
           POLYNOMIAL_BASED_THICKNESS_VARIATION {DENSITY_POLYNOMIAL_ORDERS {0} WIDTH_POLYNOMIAL_ORDERS {0}
             WIDTH_RANGES {100} POLYNOMIAL_COEFFICIENTS {{0}} POLYNOMIAL_COEFFICIENTS {{0}}}})");
         near(native.conductors[0].get_thickness_polynomial()->density_window, 20);
         for (const std::string rows : {"(20,.5)", "(20,1) (30,1)", "(0,1)", "(-1,1)", "(501,1)"})
           rejected("CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12 " + polynomial + " DENSITY_BOX_WEIGHTING_FACTOR {" + rows + "}}");
       }},
      {"loaded documents own their original source and property values",
       [] {
         std::string input = "units {length 1nm} dielectric D1 {SW_T 10 TW_T 20} metal M1 {Thickness 100 Resistance .12}";
         const auto original = input;
         const auto result = ircx::parseProcess(input);
         require(static_cast<bool>(result), "source document rejected");
         input.assign(input.size(), 'x');
         require(result.technology->source_text == original, "source text borrowed input storage");
         const auto& dielectric = result.technology->properties[1];
         require(dielectric.children.size() == 2 && dielectric.children[0].key == "SW_T", "surface metadata lost");
         near(dielectric.children[0].values[0].value, 10);
         near(result.technology->conductors[0].get_thickness(), .1);
       }},
      {"validated metadata retains its parent-specific numeric shape",
       [] {
         auto model = parse("metal M1 {Thickness .1 Resistance .12 WIDTH_BASED_EM {WIDTH {.1 .2} EM {1 2 3}}}");
         near(model.conductors[0].get_sheet_resistance(), .12);
       }},
      {"non-square matrix preserves row direction",
       [] {
         auto model = parse(R"(metal M1 { Thickness .2 RHO_VS_SI_WIDTH_AND_THICKNESS {
           WIDTH {.1 .2 .3} THICKNESS {.1 .2} VALUES {{1 2 3} {4 5 6}} } })");
         near(model.conductors[0].get_resistivity_by_width_thickness_table().query(.2, .3).value(), 6.0);
         near(model.conductors[0].get_resistivity_by_width_thickness_table().query(.15, .15).value(), 3.0);
       }},
      {"single-axis tables remain usable",
       [] {
         auto model = parse(R"(metal M1 { Thickness .2 RPSQ_VS_WIDTH_AND_SPACING {
           SPACINGS {.1 .3} VALUES {{.1 .3}} } })");
         near(model.conductors[0].get_sheet_resistance_by_width_spacing_table().query(200, .2).value(), .2);
       }},
      {"fractional thickness values have no length units",
       [] {
         auto model = parse(R"(units {length 1nm} metal M1 { Thickness 200 Resistance .12
           THICKNESS_VS_WIDTH_AND_SPACING {WIDTHS {100} VALUES {{.1}}} })");
         near(model.conductors[0].get_thickness_change_table_list()[0].get_table().query(.1, 0).value(), .1);
       }},
      {"file extension does not select syntax",
       [] {
         auto directory = (std::filesystem::temp_directory_path() / "ircx-parser-XXXXXX").string();
         require(mkdtemp(directory.data()) != nullptr, "cannot create fixture directory");
         for (const auto& input : {"metal M1 { Thickness .2 Resistance .12 }", "CONDUCTOR M1 { THICKNESS=.2 RPSQ=.12 }"}) {
           for (const auto& filename : {"process.itf", "process.tech", "process.txt", "process"}) {
             const auto path = std::filesystem::path(directory) / filename;
             { std::ofstream stream(path);
               stream << input;
             }
             const auto result = ircx::readProcessFile(path);
             require(static_cast<bool>(result), "extension selected wrong parser");
           }
         }
         std::filesystem::remove_all(directory);
       }},
      {"diagnostics retain source positions after BOM",
       [] {
         const std::string text = "\xef\xbb\xbfmetal M1 {\n  Thickness 2ohm\n  Resistance .12\n}";
         const auto result = ircx::parseProcess(text, "position.fixture");
         require(!result && result.diagnostics.back().code == "invalid_unit", "missing unit diagnostic");
         const auto& begin = result.diagnostics.back().begin;
         require(begin.line == 2 && begin.column == 13 && begin.offset == text.find("2ohm"), "incorrect source position");
       }},
      {"concurrent parsers have independent state",
       [] {
         std::vector<std::future<void>> tasks;
         for (int i = 0; i < 12; ++i)
           tasks.push_back(std::async(std::launch::async, [i] {
             for (int n = 0; n < 20; ++n) {
               if (i % 2)
                 rejected("metal M1 { Thickness .1 Resistance broken }");
               else
                 rejected("CONDUCTOR M1 { THICKNESS=.1 RPSQ=1e+ }");
               const auto m = parse(i % 2 ? "metal M1 {Thickness .1 Resistance .12}" : "CONDUCTOR M1 {THICKNESS=.1 RPSQ=.12}");
               near(m.conductors[0].get_sheet_resistance(), .12);
             }
           }));
         for (auto& task : tasks)
           task.get();
       }},
      {"empty comment-only and irrelevant documents rejected",
       [] {
         rejected("");
         rejected("# technology\n");
         rejected("dielectric D0 { thickness .1 constant 3.9 }");
       }},
      {"broken selected syntax cannot fall back",
       [] {
         rejected("metal M1 { Thickness .2 Resistance .12");
         rejected("CONDUCTOR M1 { THICKNESS=broken RPSQ=.12 }");
         rejected("CONDUCTOR M1 { THICKNESS=.2 RPSQ=.12 } trailing_garbage");
       }},
      {"shared syntax without identifying fields is diagnosed", [] { rejected("VIA V1 { RPV_VS_AREA {{.01 4} {.03 2}} }", "ambiguous_process_syntax"); }},
      {"mixed syntax rejected", [] { rejected("CONDUCTOR M1 { THICKNESS=.2 RPSQ=.12 } metal M2 { Thickness .2 Resistance .12 }", "mixed_process_syntax"); }},
      {"duplicate declarations and invalid tables rejected",
       [] {
         rejected("metal M1 { Thickness .2 Resistance .12 } metal M1 { Thickness .2 Resistance .12 }");
         rejected("CONDUCTOR M1 { THICKNESS=.2 RPSQ=.1 RPSQ=.2 }");
         rejected("CONDUCTOR M1 { THICKNESS=.2 RPSQ_VS_WIDTH_AND_SPACING { WIDTHS {.1 .2} SPACINGS {.1 .2} VALUES {1 2 3} } }");
         rejected("CONDUCTOR M1 { THICKNESS=.2 RPSQ_VS_SI_WIDTH { (.2,.1) (.1,.2) } }");
         rejected("metal M1 { Thickness .2 RPSQ_VS_SI_WIDTH {{.1 .1} {.1 .2}} }");
         rejected("metal M1 { Thickness .2 RPSQ_VS_SI_WIDTH {{100nm .1} {.1um .2}} }");
         rejected("metal M1 {Thickness .2 RPSQ_VS_WIDTH_AND_SPACING {WIDTHS {} VALUES {}}}");
       }},
      {"dimensional and finite value validation",
       [] {
         rejected("metal M1 { Thickness 2ohm Resistance .12 }");
         rejected("metal M1 { Thickness .2 Resistance 1e999 }");
         rejected("CONDUCTOR M1 { THICKNESS=.2 RPSQ=NaN }");
         rejected("metal M1 { Thickness .2 Resistance -1 }");
       }},
      {"invalid resistance declarations remain read errors",
       [] {
         rejected("metal M1 {Thickness .2}", "missing_resistance");
         rejected("CONDUCTOR M1 {THICKNESS=.2 RHO=.02 RPSQ=.1}", "conflicting_resistance");
         rejected("via V1 {Area .01 Resistance 4 LowerLayer M1 M2 UpperLayer M3}", "unsupported_physics");
       }},
      {"unknown properties cannot silently disappear", [] { rejected("CONDUCTOR M1 { THICKNESS=.2 RPSQ=.12 TYPO_ETCH=.02 }"); }},
      {"valid parse after rejected input",
       [] {
         rejected("metal M1 {");
         near(parse("metal M1 { Thickness .2 Resistance .12 }").conductors[0].get_sheet_resistance(), .12);
       }},
  };
  int failures = 0;
  for (const auto& [name, test] : tests) {
    try { test(); std::cout << "PASS: " << name << '\n'; }
    catch (const std::exception& e) { ++failures; std::cerr << "FAIL: " << name << ": " << e.what() << '\n'; }
  }
  return failures ? 1 : 0;
}
