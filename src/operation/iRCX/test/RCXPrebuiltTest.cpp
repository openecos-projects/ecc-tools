#include "MultiNeighborCache.hpp"
#include <iostream>

int main(int argc,char** argv) {
  try {
    if(argc!=3)throw std::runtime_error("expected mode and model/table");
    setenv("IRCX_MULTI_MODE",argv[1],1);
    setenv("IRCX_MULTI_RANK","3",1);
    setenv("IRCX_PATTERN_GRID","unused_in_lookup_unit_test",1);
    unsetenv("IRCX_MULTI_TABLE");
    setenv("IRCX_PATTERN_MODEL",argv[2],1);
    if(std::string(argv[1])=="apply")setenv("IRCX_MULTI_TABLE",argv[2],1);
    ircx::TopoPool pool;
    ircx::pattern::MultiNeighborCache cache(pool);
    if(!cache.active()||cache.collecting())throw std::runtime_error("wrong mode");
    if(std::string(argv[1])=="apply") {
      const auto& known=cache.caps("M1 SUBSTRATE - 4000 200 0 0");
      if(known.size()!=1||known[0]!=.1)throw std::runtime_error("exact lookup changed");
      bool failed=false;
      try {cache.caps("M1 SUBSTRATE - 4000 220 0 0");}
      catch(const std::runtime_error& e) {failed=std::string(e.what()).find("missing multi geometry")!=std::string::npos;}
      if(!failed)throw std::runtime_error("exact mode must reject a missing key");
    } else {
      const auto& first=cache.caps("M1 SUBSTRATE - 4000 200 1 0 -500 -300",100);
      if(first.size()!=2||std::abs(first[0]-.1)>1e-12)throw std::runtime_error("bad model coefficients");
      // A new geometry has no pre-enumerated exact entry. Its width changes Cg.
      const auto& second=cache.caps("M1 SUBSTRATE - 4000 400 0 0",200);
      if(second.size()!=1||std::abs(second[0]-.2)>1e-12)throw std::runtime_error("novel geometry query failed");
      if(first.size()!=2||std::abs(first[0]-.1)>1e-12)throw std::runtime_error("query invalidated a live result");
      if(!cache.caps("M1 SUBSTRATE - 4000 80000 0 0",30).empty())throw std::runtime_error("domain fallback missing");
      if(!cache.caps("M1 SUBSTRATE - 4000 200 1 0 -100 100",40).empty())throw std::runtime_error("geometry fallback missing");
    }
    std::cout<<"integration lookup passed\n";
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
