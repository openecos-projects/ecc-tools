%code requires {
#include <cstdio>
#include <cstdlib>

#include "SpefParser.hh"
}

%code provides {
int spef_lex(void);
void spef_error(spef::ParserContext* context, const char* message);
extern int spef_lineno;
}

%union {
  char* str;
  int ival;
}

%define api.prefix {spef_}
%parse-param { spef::ParserContext* context }

%token EOL
%token K_NAME_MAP K_PORTS K_PHYSICAL_PORTS K_CONN K_CAP K_RES K_INDUC K_END
%token K_D_NET K_D_PNET K_R_NET K_R_PNET
%token K_DRIVER K_CELL K_C2_R1_C1 K_LOADS K_RC
%token K_COORD K_LOAD K_DRIVE K_SLEW K_LL K_UR K_LAYER K_IGNORE_ATTR
%token <str> HEADER_KEY CONN_TYPE DIRECTION NAME_REF SPEF_NAME QUOTED_STRING NUMBER

%type <str> name_token name_value_token
%type <ival> direction_opt direction conn_type

%destructor { std::free($$); } <str>

%start spef_file

%%

spef_file:
    lines
  ;

lines:
    /* empty */
  | lines line
  ;

line:
    EOL
  | section_line
  | header_line
  | name_map_entry
  | port_entry
  | dnet_entry
  | rnet_entry
  | reduced_driver_entry
  | reduced_cell_entry
  | reduced_pi_entry
  | reduced_loads_entry
  | reduced_rc_entry
  | conn_entry
  | cap_res_entry
  ;

section_line:
    K_NAME_MAP { context->setSection(spef::SectionType::kNameMap); }
  | K_PORTS { context->setSection(spef::SectionType::kPorts); }
  | K_PHYSICAL_PORTS { context->setSection(spef::SectionType::kPhysicalPorts); }
  | K_CONN { context->setSection(spef::SectionType::kConn); }
  | K_CAP { context->setSection(spef::SectionType::kCap); }
  | K_RES { context->setSection(spef::SectionType::kRes); }
  | K_INDUC { context->setSection(spef::SectionType::kInduc); }
  | K_END { context->setSection(spef::SectionType::kEnd); }
  ;

header_line:
    HEADER_KEY
    {
      context->startHeader(spef::tokenToString($1));
      std::free($1);
    }
    header_values
    EOL
    {
      context->finishHeader();
    }
  ;

header_values:
    header_value
  | header_values header_value
  ;

header_value:
    name_value_token
    {
      context->addHeaderValue(spef::stripQuotes(spef::tokenToString($1)));
      std::free($1);
    }
  ;

name_map_entry:
    NAME_REF name_value_token EOL
    {
      if (context->section() == spef::SectionType::kNameMap) {
        context->addNameMap(spef::tokenToString($1), spef::stripQuotes(spef::tokenToString($2)));
      }
      std::free($1);
      std::free($2);
    }
  ;

port_entry:
    port_start port_attrs EOL
    {
      context->finishPort();
    }
  ;

port_start:
    name_token direction
    {
      const bool physical = context->section() == spef::SectionType::kPhysicalPorts;
      if (context->section() == spef::SectionType::kPorts || physical) {
        context->startPort(spef::tokenToString($1),
                           static_cast<spef::ConnectionDirection>($2),
                           physical);
      }
      std::free($1);
    }
  ;

port_attrs:
    /* empty */
  | port_attrs port_attr
  ;

port_attr:
    K_COORD NUMBER NUMBER
    {
      context->setPortCoordinate(spef::Coord{spef::toDouble($2), spef::toDouble($3)});
      std::free($2);
      std::free($3);
    }
  | K_LOAD NUMBER
    {
      std::free($2);
    }
  | K_SLEW NUMBER NUMBER
    {
      std::free($2);
      std::free($3);
    }
  | K_SLEW NUMBER NUMBER NUMBER NUMBER
    {
      std::free($2);
      std::free($3);
      std::free($4);
      std::free($5);
    }
  | K_DRIVE name_token
    {
      std::free($2);
    }
  | K_IGNORE_ATTR
  ;

dnet_entry:
    K_D_NET name_token NUMBER EOL
    {
      context->startNet(spef::tokenToString($2), spef::parseParValue($3), false, 0);
      std::free($2);
      std::free($3);
    }
  | K_D_PNET name_token NUMBER EOL
    {
      context->startNet(spef::tokenToString($2), spef::parseParValue($3), true, 0);
      std::free($2);
      std::free($3);
    }
  ;

rnet_entry:
    K_R_NET name_token NUMBER EOL
    {
      context->startReducedNet(spef::tokenToString($2), spef::parseParValue($3), false, 0);
      std::free($2);
      std::free($3);
    }
  | K_R_PNET name_token NUMBER EOL
    {
      context->startReducedNet(spef::tokenToString($2), spef::parseParValue($3), true, 0);
      std::free($2);
      std::free($3);
    }
  ;

reduced_driver_entry:
    K_DRIVER name_token EOL
    {
      context->startReducedDriver(spef::tokenToString($2));
      std::free($2);
    }
  ;

reduced_cell_entry:
    K_CELL name_token EOL
    {
      context->setReducedDriverCell(spef::stripQuotes(spef::tokenToString($2)));
      std::free($2);
    }
  ;

reduced_pi_entry:
    K_C2_R1_C1 NUMBER NUMBER NUMBER EOL
    {
      context->setReducedPi(spef::parseParValue($2),
                            spef::parseParValue($3),
                            spef::parseParValue($4));
      std::free($2);
      std::free($3);
      std::free($4);
    }
  ;

reduced_loads_entry:
    K_LOADS EOL
  ;

reduced_rc_entry:
    K_RC name_token NUMBER EOL
    {
      context->addReducedLoad(spef::tokenToString($2), spef::parseParValue($3));
      std::free($2);
      std::free($3);
    }
  ;

conn_entry:
    conn_start conn_attrs EOL
    {
      context->finishConn();
    }
  ;

conn_start:
    conn_type name_token direction_opt
    {
      context->startConn(static_cast<spef::ConnectionType>($1), spef::tokenToString($2),
                         static_cast<spef::ConnectionDirection>($3));
      std::free($2);
    }
  ;

conn_attrs:
    /* empty */
  | conn_attrs conn_attr
  ;

conn_attr:
    K_COORD NUMBER NUMBER
    {
      context->setConnCoordinate(spef::Coord{spef::toDouble($2), spef::toDouble($3)});
      std::free($2);
      std::free($3);
    }
  | K_LOAD NUMBER
    {
      context->setConnLoad(spef::parseParValue($2));
      std::free($2);
    }
  | K_DRIVE name_token
    {
      context->setConnDrivingCell(spef::stripQuotes(spef::tokenToString($2)));
      std::free($2);
    }
  | K_LL NUMBER NUMBER
    {
      context->setConnLowerLeft(spef::Coord{spef::toDouble($2), spef::toDouble($3)});
      std::free($2);
      std::free($3);
    }
  | K_UR NUMBER NUMBER
    {
      context->setConnUpperRight(spef::Coord{spef::toDouble($2), spef::toDouble($3)});
      std::free($2);
      std::free($3);
    }
  | K_LAYER NUMBER
    {
      context->setConnLayer(spef::toInt($2));
      std::free($2);
    }
  | K_SLEW NUMBER NUMBER
    {
      std::free($2);
      std::free($3);
    }
  | K_SLEW NUMBER NUMBER NUMBER NUMBER
    {
      std::free($2);
      std::free($3);
      std::free($4);
      std::free($5);
    }
  | K_IGNORE_ATTR
  ;

cap_res_entry:
    NUMBER name_token NUMBER EOL
    {
      if (context->section() == spef::SectionType::kCap) {
        context->addCap(spef::toSize($1),
                        spef::tokenToString($2),
                        "",
                        spef::parseParValue($3));
      }
      std::free($1);
      std::free($2);
      std::free($3);
    }
  | NUMBER name_token name_token NUMBER EOL
    {
      context->addCapOrRes(spef::toSize($1),
                           spef::tokenToString($2),
                           spef::tokenToString($3),
                           spef::parseParValue($4));
      std::free($1);
      std::free($2);
      std::free($3);
      std::free($4);
    }
  ;

direction_opt:
    /* empty */ { $$ = static_cast<int>(spef::ConnectionDirection::kUninitialized); }
  | direction { $$ = $1; }
  ;

direction:
    DIRECTION
    {
      $$ = static_cast<int>(spef::parseDirection($1));
      std::free($1);
    }
  ;

conn_type:
    CONN_TYPE
    {
      $$ = static_cast<int>(spef::parseConnectionType($1));
      std::free($1);
    }
  ;

name_token:
    NAME_REF { $$ = $1; }
  | SPEF_NAME { $$ = $1; }
  | QUOTED_STRING { $$ = $1; }
  | DIRECTION { $$ = $1; }
  ;

name_value_token:
    NAME_REF { $$ = $1; }
  | SPEF_NAME { $$ = $1; }
  | QUOTED_STRING { $$ = $1; }
  | NUMBER { $$ = $1; }
  ;

%%

void spef_error(spef::ParserContext* context, const char* message)
{
  if (context != nullptr) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s at line %d",
                  message == nullptr ? "parse error" : message, spef_lineno);
    context->setError(buffer);
  }
}
