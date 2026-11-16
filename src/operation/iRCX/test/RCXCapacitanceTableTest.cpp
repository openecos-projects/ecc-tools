#include "PatternCapacitanceTable.hpp"
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void near(double a,double b,double eps=1e-10){check(std::abs(a-b)<eps,"coefficient mismatch");}
int main(int argc,char**argv){try{
 const std::string file=argc>1?argv[1]:"fixture.captab";
 {std::ofstream f(file);f<<"IRCX_CAP_TABLE_V1\n8 1\nM1 .1 .1 .1 0 .1 0\n5\n"
 <<"M1 SUBSTRATE - 1000 50 0 0\t.1\n"
 <<"M1 SUBSTRATE - 1000 100 0 0\t.2\n"
 <<"M1 SUBSTRATE - 1000 200 0 0\t.4\n"
 <<"M1 SUBSTRATE - 1000 100 1 0 -250 -150\t.3 .04\n"
 <<"M1 SUBSTRATE - 1000 200 1 0 -300 -200\t.6 .05\nEND\n";}
 captable::Library lib(file);
 near(lib.query("M1 SUBSTRATE - 1000 100 0 0").caps.at(0),.2);
 auto q=lib.query("M1 SUBSTRATE - 1000 150 0 0");
 check(q.status==captable::Status::ok && q.caps.size()==1,"unseen geometry failed");near(q.caps[0],.3,2e-4);
 auto left=lib.query("M1 SUBSTRATE - 1000 100 1 0 -250 -150");
 auto right=lib.query("M1 SUBSTRATE - 1000 100 0 1 150 250");
 near(left.caps.at(0),.3);near(left.caps.at(1),.04);check(left.caps==right.caps,"reflection mismatch");
 check(lib.query("M1 SUBSTRATE - 1000 9000 0 0").status==captable::Status::domain_fallback,"domain status");
 check(lib.query("M1 SUBSTRATE - 1000 100 1 0 -100 100").status==captable::Status::geometry_fallback,"overlap status");
 check(lib.query("M1 SUBSTRATE - 1000 100 3 3").status==captable::Status::geometry_fallback,"truncated query");
 bool bad=false;try{lib.query("BAD SUBSTRATE - 1000 100 0 0");}catch(const std::runtime_error&){bad=true;}check(bad,"unknown layer must fail");
 std::vector<std::future<void>> jobs;for(int i=0;i<8;++i)jobs.emplace_back(std::async(std::launch::async,[&]{for(int j=0;j<1000;++j)near(lib.query("M1 SUBSTRATE - 1000 150 0 0").caps.at(0),q.caps[0]);}));for(auto& j:jobs)j.get();
 near(left.caps[0],.3);
 {std::ofstream f(file+".bad");f<<"IRCX_CAP_TABLE_V1\n8 1\nM1 .1 .1 .1 0 .1 0\n1\nM1 SUBSTRATE - 1000 100 0 0\t-.1\nEND\n";}
 bad=false;try{captable::Library b(file+".bad");}catch(const std::runtime_error&){bad=true;}check(bad,"negative table must fail");
 std::cout<<"table exact, unseen, reflection, fallback, malformed and concurrent queries passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
