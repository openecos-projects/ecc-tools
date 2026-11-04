// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <unistd.h>
#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "VerilogFrontend.hh"
#include "VerilogSyntax.hh"

using namespace idb::verilog;
namespace {
void require(bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error(message);
}
struct Fixture
{
  std::filesystem::path path;
  Fixture()
  {
    auto pattern = (std::filesystem::temp_directory_path() / "verilog-input-XXXXXX").string();
    const auto descriptor = mkstemp(pattern.data());
    require(descriptor >= 0, "cannot create fixture");
    close(descriptor);
    path = pattern;
  }
  ~Fixture()
  {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
};
void files()
{
  Fixture fixture;
  const std::string source = "module top(input a,output y); assign y=a; endmodule\n";
  {
    std::ofstream file(fixture.path, std::ios::binary);
    file << source;
  }
  auto plain = readFile(fixture.path.string());
  require(bool(plain), "plain file failed");
  gzFile file = gzopen(fixture.path.c_str(), "wb");
  require(file != nullptr, "cannot open gzip fixture");
  require(gzwrite(file, source.data(), source.size()) == int(source.size()), "gzip write failed");
  require(gzclose(file) == Z_OK, "gzip close failed");
  auto compressed = readFile(fixture.path.string());
  require(bool(compressed), "gzip magic without filename extension was not detected");
  auto flat = compile(*compressed.design);
  require(flat && flat.design->ports.size() == 2, "gzip AST invalid");
  std::filesystem::resize_file(fixture.path, std::filesystem::file_size(fixture.path) - 6);
  auto truncated = readFile(fixture.path.string());
  require(!truncated, "truncated gzip accepted");
  require(truncated.diagnostics[0].file == fixture.path.string(), "gzip diagnostic lost original source path");
  std::filesystem::remove(fixture.path);
  require(!readFile(fixture.path.string()), "missing input accepted");
}
void preprocessing()
{
  auto parsed = parse(R"V(`timescale 1ns/1ps
`celldefine
`define WIDTH 4
`define CONNECT(dst, src) assign dst = src;
`ifdef ABSENT
invalid Verilog `UNDEFINED
`elsif WIDTH
module top(input [`WIDTH-1:0] a,output [`WIDTH-1:0] y);
`CONNECT(y, a)
endmodule
`else
invalid
`endif
`endcelldefine
)V");
  require(bool(parsed), parsed ? "" : parsed.diagnostics.front().text());
  auto flat = compile(*parsed.design);
  require(flat && flat.design->ports[0].signal.bits.size() == 4 && flat.design->assignments.size() == 4,
          "macro expansion or conditional branch changed connectivity");
  auto protected_text = parse("// `NO_MACRO\n(* note=\"`NO_MACRO\" *) module top; wire \\`NO_MACRO ; endmodule");
  require(bool(protected_text), "macro expanded inside comment, string or escaped identifier");
  require(!parse("`define A `A\nmodule top; wire [`A:0] a; endmodule"), "recursive macro was accepted");
  require(!parse("`ifdef FLAG\nmodule top; endmodule"), "unterminated conditional accepted");
  require(!parse("`else\nmodule top; endmodule"), "unmatched else accepted");
  auto nested = parse("`define ID(x) x\n`define W `ID(`ID(4))\nmodule top(input [`W-1:0] a); endmodule");
  require(bool(nested), nested ? "" : nested.diagnostics.front().text());
  auto comment = parse("`define N 4 /* width */\n`ifdef N /* enabled */\nmodule top(input [`N-1:0] a); endmodule\n`endif");
  require(bool(comment), comment ? "" : comment.diagnostics.front().text());
  auto numeric = parse("`define F(b0) 1'b0\nmodule top(output y);assign y=`F(1);endmodule");
  require(bool(numeric), "macro formal substituted inside a numeric token");
  auto commented_args = parse("`define F(x) x\nmodule top(output y);assign y=`F(1 // ignored ) ,\n);endmodule");
  require(bool(commented_args), "macro argument comment changed delimiters");
  auto continued = parse("`define CONNECT(a,b) assign a= \\\n b;\nmodule top(input x,output y);`CONNECT(y,x) endmodule");
  require(bool(continued), continued ? "" : continued.diagnostics.front().text());
  require(!parse("`timescale 1ps/1ns\nmodule top;endmodule"), "invalid timescale accepted");
  require(!parse("`define ifdef 1\nmodule top;endmodule"), "reserved compiler directive redefined");
  SourceOptions options;
  options.defines.emplace("WIDTH", "3");
  auto configured = parse("module top(input [`WIDTH-1:0] a); endmodule", "configured.v", options);
  require(configured && compile(*configured.design).design->ports[0].signal.bits.size() == 3, "external defines not applied");
  Fixture header, source;
  {
    std::ofstream out(header.path);
    out << "`ifndef GUARD\n`define GUARD\n`define N 3\n`endif\n";
  }
  {
    std::ofstream out(source.path);
    out << "`include \"" << header.path.filename().string() << "\"\n"
        << "module top(input [`N-1:0] a); endmodule\n";
  }
  options.include_paths.push_back(header.path.parent_path().string());
  auto searched = parse("`include \"" + header.path.filename().string() + "\"\nmodule top(input [`N-1:0] a);endmodule",
                        "/nonexistent/source/top.v", options);
  require(bool(searched), "configured include path not searched");
  auto quoted = parse("`define NOTE \"https://example.invalid/a\"\n(* note=`NOTE *) module top;endmodule");
  require(bool(quoted), "directive scanner treated quoted // as a comment");
  auto included = readFile(source.path.string());
  require(included && compile(*included.design).design->ports[0].signal.bits.size() == 3, "relative include failed");
  {
    std::ofstream out(header.path);
    out << "module broken;\nwire ;\nendmodule\n";
  }
  {
    std::ofstream out(source.path);
    out << "`include \"" << header.path.filename().string() << "\"\nmodule top; endmodule\n";
  }
  auto broken = readFile(source.path.string());
  require(!broken && broken.diagnostics.front().file == header.path.string() && broken.diagnostics.front().location.line == 2,
          "include diagnostic lost filename/line");
}
void compilationUnit()
{
  require(!readFiles({}), "empty compilation unit accepted");
  Fixture definitions, child, top;
  {
    std::ofstream f(definitions.path);
    f << "`define WIDTH 3\n`default_nettype none\n";
  }
  {
    std::ofstream f(child.path);
    f << "module child(input [`WIDTH-1:0] a,output [`WIDTH-1:0] y);assign y=a;endmodule";
  }
  {
    std::ofstream f(top.path);
    f << "module top(input [`WIDTH-1:0] a,output [`WIDTH-1:0] y);child u(a,y);endmodule";
  }
  auto unit = [&](const std::vector<std::string>& files) { return readFiles(files); };
  auto parsed = unit({definitions.path.string(), child.path.string(), top.path.string()});
  require(bool(parsed), parsed ? "" : parsed.diagnostics.front().text());
  auto flat = compile(*parsed.design, "top");
  require(flat && flat.design->ports[0].signal.bits.size() == 3 && flat.design->assignments.size() == 9,
          "ordered source files lost macros or hierarchy connectivity");
  auto duplicate = unit({definitions.path.string(), child.path.string(), child.path.string()});
  require(!duplicate && duplicate.diagnostics[0].file == child.path.string()
              && duplicate.diagnostics[0].message.find("duplicate module") != std::string::npos,
          "cross-file duplicate not diagnosed");
  {
    std::ofstream f(top.path);
    f << "module top;\nCELL u(.A(missing));\nendmodule";
  }
  auto implicit = unit({definitions.path.string(), top.path.string()});
  require(bool(implicit), "cannot parse cross-file directive fixture");
  auto failure = compile(*implicit.design, "top");
  require(!failure && failure.diagnostics[0].file == top.path.string() && failure.diagnostics[0].location.line == 2,
          "default_nettype or source location lost across files");
  {
    std::ofstream f(top.path);
    f << "module top;\nwire ;\nendmodule";
  }
  auto syntax = unit({definitions.path.string(), top.path.string()});
  require(!syntax && syntax.diagnostics[0].file == top.path.string() && syntax.diagnostics[0].location.line == 2,
          "syntax diagnostic lost source file/line");
  std::filesystem::remove(top.path);
  auto missing = unit({definitions.path.string(), top.path.string()});
  require(!missing && missing.diagnostics[0].file == top.path.string(), "missing later input not diagnosed");
}
void scannerBoundaries()
{
  const std::string long_name = "escaped." + std::string(70000, 'q') + "[0]";
  const std::string source = std::string(65530, ' ') + "module top; wire \\" + long_name
                             + " ; CELL u(.A(8 /* size */ 'h /* base */ a5), .B(\\" + long_name + " )); endmodule";
  auto result = compile(source, "scanner-boundary.v", "top");
  require(bool(result), result ? "" : result.diagnostics[0].text());
  const auto& ports = result.design->instances[0].ports;
  require(ports[0].signal.bits == std::vector<NetId>{oneBit, zeroBit, oneBit, zeroBit, zeroBit, oneBit, zeroBit, oneBit},
          "based number split by trivia or scanner refill changed value");
  require(result.design->nets[ports[1].signal.bits[0]].name == long_name, "escaped identifier spanning scanner refills was truncated");
  const std::string comment(131072, 'x');
  auto trivia = compile("/*" + comment + "*/module top; CELL u(.A(8//" + comment + "\n'h/*" + comment + "*/a5)); endmodule//" + comment,
                        "trivia.v", "top");
  require(trivia && trivia.design->instances[0].ports[0].signal.bits == ports[0].signal.bits,
          "long comments changed scanner state or based number value");
  auto broken = parse("module top; wire a; /* unfinished", "comment.v");
  require(!broken && broken.diagnostics[0].file == "comment.v", "scanner error lost source location");
  require(bool(parse("module top;endmodule")), "scanner state survived a failed parse");
  require(!parse("module top;" + std::string(1, static_cast<char>(0xff)) + "endmodule"), "non-ASCII byte bypassed scanner diagnostics");

  auto located = parse("module top;\nbegin: b wire a;end\nif(1) CELL u();\nendmodule");
  require(bool(located), "generate location fixture failed");
  const auto& statements = located.design->modules[0].scope.statements;
  const auto& block = *std::get<std::unique_ptr<Generate>>(statements[0]);
  const auto& conditional = *std::get<std::unique_ptr<Generate>>(statements[1]);
  require(block.location.line == 2 && block.location.column == 1 && block.location.order > 0,
          "generate block lost its opening token location");
  const auto location = conditional.branches[0].body->scope.location;
  require(location.line == 3 && location.column == 7 && location.order > conditional.location.order,
          "anonymous generate scope lost its first token location");
}
void boundedExpressions()
{
  const auto check = [](const std::string& expression) {
    auto result = parse("module top; parameter P=" + expression + ";endmodule");
    require(!result && result.diagnostics[0].message.find("depth") != std::string::npos, "excessive expression nesting accepted");
    require(result.diagnostics[0].location.column < 4096, "expression nesting rejected only after reading the entire chain");
  };
  check(std::string(10000, '~') + "0");
  check(std::string(10000, '{') + "0" + std::string(10000, '}'));
  std::string ternary, select;
  for (unsigned i = 0; i < 10000; ++i) {
    ternary += "1?0:";
    select += "a[";
  }
  check(ternary + "0");
  check(select + "0" + std::string(10000, ']'));
}
void independentResults()
{
  NetlistResult owned;
  {
    std::string source = "module top(input a); CELL u(.A(a)); endmodule";
    LibraryCell cell{{{"A", PortShape{Direction::input, 1}}}};
    owned = compile(source, "owned.v", "top", {}, [&](auto) { return &cell; });
  }
  require(owned && owned.design->source == "owned.v" && owned.design->instances[0].ports[0].name == "A"
              && owned.design->nets[owned.design->instances[0].ports[0].signal.bits[0]].name == "a",
          "compile result borrows source, syntax or library storage");
  for (unsigned i = 0; i < 100; ++i) {
    auto valid = parse("module child(input a); CELL u(.A(a)); endmodule module top(input x); child c(x); endmodule");
    auto invalid = parse("module broken;");
    require(valid && !invalid, "one result changed another result");
    auto moved = std::move(valid);
    auto first = compile(*moved.design), second = compile(*moved.design, "child");
    require(first && second && first.design->instances[0].name == "c/u" && second.design->instances[0].name == "u",
            "repeated elaboration mutated owned AST");
  }
}
}  // namespace
int main()
{
  try {
    boundedExpressions();
    scannerBoundaries();
    compilationUnit();
    preprocessing();
    files();
    independentResults();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
