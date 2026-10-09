// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <unistd.h>
#include <zlib.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "VerilogElaborator.hh"
#include "VerilogParser.hh"

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
  auto flat = elaborate(*compressed.design);
  require(flat && flat.design->ports.size() == 2, "gzip AST invalid");
  std::filesystem::resize_file(fixture.path, std::filesystem::file_size(fixture.path) - 6);
  auto truncated = readFile(fixture.path.string());
  require(!truncated, "truncated gzip accepted");
  require(truncated.diagnostics[0].file == fixture.path.string(), "gzip diagnostic lost original source path");
  std::filesystem::remove(fixture.path);
  require(!readFile(fixture.path.string()), "missing input accepted");
}
void independentResults()
{
  for (unsigned i = 0; i < 100; ++i) {
    auto valid = parse("module child(input a); CELL u(.A(a)); endmodule module top(input x); child c(x); endmodule");
    auto invalid = parse("module broken;");
    require(valid && !invalid, "one result changed another result");
    auto moved = std::move(valid);
    auto first = elaborate(*moved.design), second = elaborate(*moved.design, "child");
    require(first && second && first.design->instances[0].name == "c/u" && second.design->instances[0].name == "u",
            "repeated elaboration mutated owned AST");
  }
}
}  // namespace
int main()
{
  try {
    files();
    independentResults();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
