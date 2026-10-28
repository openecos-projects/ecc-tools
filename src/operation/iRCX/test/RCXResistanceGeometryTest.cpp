#include "ResExtractor.hpp"
#include "Utility.hpp"
#include <iostream>
#include <cmath>
int main(int argc,char**argv){using namespace ircx;try{
 const double scale=argc>1?std::stod(argv[1]):.9;
 Logger::initInst();Utility::initInst();DataManager::initInst();ResExtractor::initInst();
 auto& db=RCXDM.getDatabase();RCXDM.getConfig().thread_number=1;
 db.get_layout_data().set_dbu_per_micron(1000);db.get_layout_data().get_net_list().resize(1);
 auto& map=db.get_layer_table();map.register_design_layer(0,"M1");map.register_process_layer(0,"M1");map.register_mapping("M1","M1");
 std::vector<TopoNode> nodes{TopoNode(0),TopoNode(0)};nodes[0].set_point(GTLPointInt(0,0));nodes[1].set_point(GTLPointInt(10000,0));
 TopoEdge edge(0);edge.set_start_node_idx(0);edge.set_end_node_idx(1);edge.set_layer_idx(0);edge.set_shape(GTLRectInt(0,-100,10000,100));db.get_topo_pool().add_net(std::move(nodes),{edge});
 CornerData corner;corner.set_half_node_scale_factor(scale);corner.set_tmpr(25);corner.set_global_tmpr(25);
 ProcessConductor metal;metal.set_layer_name("M1");metal.set_thickness(.1);metal.set_resistivity(.02);
 ProcessEtchTable etch;etch.get_table().set_row_list({.05,.4});etch.get_table().set_column_list({.05,.5});etch.get_table().set_value_list({.02,.02,.02,.02});metal.get_etch_table_list().push_back(etch);corner.get_process_conductor_list().push_back(metal);db.get_corner_data_list().push_back(corner);
 EdgeEtchInterval interval;interval.set_start_coord(0);interval.set_end_coord(10);interval.set_width(.16);interval.set_thickness(.1);interval.set_lower_spacing(.1);interval.set_upper_spacing(-1);
 auto& profiles=db.get_corner_net_etch_profile_pool();profiles.init(1,1);profiles.get_item(CornerNetIdx(0,0)).append_edge_interval_list({interval});
 bool invalid=false;try{RCXRE.extract();}catch(const std::runtime_error&){invalid=true;}
 if(!std::isfinite(scale)||scale<=0){if(!invalid)throw std::runtime_error("invalid scale accepted");return 0;}
 if(invalid)throw std::runtime_error("valid scale rejected");
 const auto actual=db.get_rc_data().get_corner_net_resistance_list(CornerNetIdx(0,0)).at(0);
 const double expected=.02*(10*scale)/((.2*scale-.04)*.1);
 std::cout<<"R="<<actual<<" expected="<<expected<<'\n';
 if(std::abs(actual-expected)>1e-9)throw std::runtime_error("wrong physical resistance");
 const auto& after=profiles.get_item(CornerNetIdx(0,0)).get_edge_interval_list(0)[0];if(after.get_width()!=.16||after.get_end_coord()!=10||after.get_upper_spacing()!=-1)throw std::runtime_error("capacitance profile changed");
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
