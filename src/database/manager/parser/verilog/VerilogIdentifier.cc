// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogIdentifier.hh"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
namespace idb::verilog {
namespace {
// Keywords cannot be used as simple identifiers (IEEE 1364-2005 Annex B).
const std::unordered_set<std::string_view> keywords = {"always",
                                                       "and",
                                                       "assign",
                                                       "automatic",
                                                       "begin",
                                                       "buf",
                                                       "bufif0",
                                                       "bufif1",
                                                       "case",
                                                       "casex",
                                                       "casez",
                                                       "cell",
                                                       "cmos",
                                                       "config",
                                                       "deassign",
                                                       "default",
                                                       "defparam",
                                                       "design",
                                                       "disable",
                                                       "edge",
                                                       "else",
                                                       "end",
                                                       "endcase",
                                                       "endconfig",
                                                       "endfunction",
                                                       "endgenerate",
                                                       "endmodule",
                                                       "endprimitive",
                                                       "endspecify",
                                                       "endtable",
                                                       "endtask",
                                                       "event",
                                                       "for",
                                                       "force",
                                                       "forever",
                                                       "fork",
                                                       "function",
                                                       "generate",
                                                       "genvar",
                                                       "highz0",
                                                       "highz1",
                                                       "if",
                                                       "ifnone",
                                                       "incdir",
                                                       "include",
                                                       "initial",
                                                       "inout",
                                                       "input",
                                                       "instance",
                                                       "integer",
                                                       "join",
                                                       "large",
                                                       "liblist",
                                                       "library",
                                                       "localparam",
                                                       "macromodule",
                                                       "medium",
                                                       "module",
                                                       "nand",
                                                       "negedge",
                                                       "nmos",
                                                       "nor",
                                                       "noshowcancelled",
                                                       "not",
                                                       "notif0",
                                                       "notif1",
                                                       "or",
                                                       "output",
                                                       "parameter",
                                                       "pmos",
                                                       "posedge",
                                                       "primitive",
                                                       "pull0",
                                                       "pull1",
                                                       "pulldown",
                                                       "pullup",
                                                       "pulsestyle_onevent",
                                                       "pulsestyle_ondetect",
                                                       "rcmos",
                                                       "real",
                                                       "realtime",
                                                       "reg",
                                                       "release",
                                                       "repeat",
                                                       "rnmos",
                                                       "rpmos",
                                                       "rtran",
                                                       "rtranif0",
                                                       "rtranif1",
                                                       "scalared",
                                                       "showcancelled",
                                                       "signed",
                                                       "small",
                                                       "specify",
                                                       "specparam",
                                                       "strong0",
                                                       "strong1",
                                                       "supply0",
                                                       "supply1",
                                                       "table",
                                                       "task",
                                                       "time",
                                                       "tran",
                                                       "tranif0",
                                                       "tranif1",
                                                       "tri",
                                                       "tri0",
                                                       "tri1",
                                                       "triand",
                                                       "trior",
                                                       "trireg",
                                                       "unsigned",
                                                       "use",
                                                       "uwire",
                                                       "vectored",
                                                       "wait",
                                                       "wand",
                                                       "weak0",
                                                       "weak1",
                                                       "while",
                                                       "wire",
                                                       "wor",
                                                       "xnor",
                                                       "xor"};

bool letter(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
}  // namespace
bool isKeyword(std::string_view name)
{
  return keywords.count(name) != 0;
}
bool isSimpleIdentifier(std::string_view name)
{
  return !name.empty() && letter(name.front()) && !isKeyword(name)
         && std::all_of(name.begin() + 1, name.end(), [](char c) { return letter(c) || (c >= '0' && c <= '9') || c == '$'; });
}
std::string encodeIdentifier(std::string_view name)
{
  if (isSimpleIdentifier(name))
    return std::string(name);
  if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 33 && c <= 126; }))
    throw std::invalid_argument("name is not a printable Verilog identifier: " + std::string(name));
  return "\\" + std::string(name) + " ";
}
}  // namespace idb::verilog
