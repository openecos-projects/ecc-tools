// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file layer_property_parser.h
 * @brief Common LEF58 layer property parser helpers.
 */

#pragma once

#include <boost/spirit/include/qi.hpp>

#include <iostream>
#include <string>

namespace idb::layer_property {
namespace qi = boost::spirit::qi;

template <typename Iterator>
bool parse_lef58_type(Iterator beg, Iterator end, std::string& type)
{
  const static qi::rule<Iterator, std::string(), qi::ascii::space_type> value_string = qi::lexeme[+(qi::char_ - qi::char_(" ;\n"))];
  const static qi::rule<Iterator, std::string(), qi::ascii::space_type> type_rule = qi::lit("TYPE") >> value_string >> qi::lit(";");
  bool ok = qi::phrase_parse(beg, end, type_rule, qi::ascii::space, type);
  if (not ok || beg != end) {
    std::cout << "Parse \"" << std::string(beg, end) << "\" failed" << std::endl;
    return false;
  }
  return true;
}

template <typename Iterator>
bool parse_lef58_backside(Iterator beg, Iterator end)
{
  const static qi::rule<Iterator, qi::ascii::space_type> rule = qi::lit("BACKSIDE") >> qi::lit(";");
  bool ok = qi::phrase_parse(beg, end, rule, qi::ascii::space);
  if (not ok || beg != end) {
    std::cout << "Parse \"" << std::string(beg, end) << "\" failed" << std::endl;
    return false;
  }
  return true;
}

template <typename Iterator>
bool parse_lef58_rectonly(Iterator beg, Iterator end, bool& except_non_core_pins)
{
  except_non_core_pins = false;
  const static qi::rule<Iterator, bool(), qi::ascii::space_type> except_rule = qi::matches[qi::lit("EXCEPTNONCOREPINS")];
  const static qi::rule<Iterator, bool(), qi::ascii::space_type> rule = qi::lit("RECTONLY") >> except_rule >> qi::lit(";");
  bool ok = qi::phrase_parse(beg, end, rule, qi::ascii::space, except_non_core_pins);
  if (not ok || beg != end) {
    std::cout << "Parse \"" << std::string(beg, end) << "\" failed" << std::endl;
    return false;
  }
  return true;
}

template <typename Iterator>
bool parse_lef58_rightwayongridonly(Iterator beg, Iterator end, bool& check_mask)
{
  check_mask = false;
  const static qi::rule<Iterator, bool(), qi::ascii::space_type> check_mask_rule = qi::matches[qi::lit("CHECKMASK")];
  const static qi::rule<Iterator, bool(), qi::ascii::space_type> rule = qi::lit("RIGHTWAYONGRIDONLY") >> check_mask_rule >> qi::lit(";");
  bool ok = qi::phrase_parse(beg, end, rule, qi::ascii::space, check_mask);
  if (not ok || beg != end) {
    std::cout << "Parse \"" << std::string(beg, end) << "\" failed" << std::endl;
    return false;
  }
  return true;
}

}  // namespace idb::layer_property
