// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#pragma once

#include <istream>

#ifndef yyFlexLexer
#define yyFlexLexer IdbVerilogFlexLexer
#include <FlexLexer.h>
#undef yyFlexLexer
#endif

#include "VerilogParser.hh"

namespace idb::verilog {

class Scanner : public IdbVerilogFlexLexer
{
 public:
  explicit Scanner(std::istream* input) : IdbVerilogFlexLexer(input) {}

  int lex(Parser::semantic_type* yylval, Parser::location_type* yylloc);
};

}  // namespace idb::verilog
