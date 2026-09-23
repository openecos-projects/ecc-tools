// White-box boundary tests supplement the independent C consumer.
#include "../eccdb_c.cpp"
#include <cstdlib>
#define CHECK(x) do { if(!(x)) std::abort(); } while(0)

int main() {
  eccdb_net_options_t input{},output{};
  input.has_pattern=1;input.pattern=0;
  input.has_frequency=1;input.frequency=0;
  auto options=to_options(input);
  CHECK(options.pattern.has_value() && *options.pattern==eccdb::NetPattern::kNone);
  from_options(options,output);
  CHECK(output.has_pattern && output.pattern==0 && output.has_frequency && output.frequency==0);
  eccdb_net_options_free(&output);
  input.has_pattern=0;
  CHECK(!to_options(input).pattern.has_value());
  const char* lefs[]={"technology.lef","cells.lef"};
  eccdb_config_t cfg{};
  cfg.lef_files=lefs;cfg.lef_count=2;cfg.def_file="design.def";
  cfg.polygon_mode=1;cfg.has_binary_cache=1;
  cfg.binary_cache={"t.bin","l.bin","d.bin"};
  auto config=to_config(cfg);
  CHECK(std::get<eccdb::LefDefInput>(config.input).lef_files.size()==2);
  CHECK(config.runtime.polygon_mode==eccdb::PolygonMode::kRectangularized);
  CHECK(config.binary_cache->library=="l.bin");
  cfg.lef_files=nullptr;
  eccdb_database_t* db=nullptr;
  CHECK(eccdb_open(&cfg,&db)==ECCDB_STATUS_INVALID_ARGUMENT && !db);
  cfg.input_kind=2;
  CHECK(eccdb_open(&cfg,&db)==ECCDB_STATUS_INVALID_ARGUMENT);
  eccdb_generated_via_data_t g{};
  g.via_rule=11;g.bottom_layer=12;g.cut_layer=13;g.top_layer=14;
  auto generated=to_generated(g);
  CHECK(!generated.offsets && !generated.cut_pattern && !generated.row_column && !generated.origin);
  eccdb_wire_point_t point{{1,2},0,1,1};
  eccdb_via_placement_t placements[2]{};
  placements[0].definition=20;
  placements[0].has_mask=1;placements[0].mask={1,2,3};
  placements[0].has_rows_columns=1;placements[0].rows=2;placements[0].columns=4;
  placements[0].step_x=8;placements[0].step_y=10;
  placements[1].definition_kind=1;placements[1].definition=21;
  eccdb_wire_rectangle_t rectangle{0,{-1,-2,3,4}};
  eccdb_wire_path_t path{};
  path.layer.value=12;path.has_width=1;path.width=0;
  path.has_mask=1;path.mask=0;path.taper=1;
  char shape[]="STRIPE";char taper[]="RULE";
  path.has_shape=1;path.shape=shape;path.has_taper_rule=1;path.taper_rule=taper;
  path.has_style=1;path.style=0;path.points=&point;path.point_count=1;
  path.vias=placements;path.via_count=2;path.rectangles=&rectangle;path.rectangle_count=1;
  eccdb_wire_routing_t routing{&path,1},snapshot{};
  from_routing(to_routing(routing),snapshot);
  CHECK(snapshot.paths[0].has_width && snapshot.paths[0].width==0);
  CHECK(snapshot.paths[0].has_style && snapshot.paths[0].has_mask);
  CHECK(snapshot.paths[0].points[0].virtual_point && snapshot.paths[0].points[0].has_extension);
  CHECK(snapshot.paths[0].vias[0].mask.bottom==3 && snapshot.paths[0].vias[0].columns==4);
  CHECK(snapshot.paths[0].vias[1].definition_kind==1 && snapshot.paths[0].vias[1].definition==21);
  CHECK(snapshot.paths[0].rectangles[0].delta.ur_y==4);
  CHECK(!std::strcmp(snapshot.paths[0].taper_rule,"RULE"));
  eccdb_wire_routing_free(&snapshot);
  placements[0].definition_kind=2;
  CHECK(invoke([&]{(void)to_routing(routing);})==ECCDB_STATUS_INVALID_ARGUMENT);
  CHECK(invoke([]{throw std::out_of_range("missing");})==ECCDB_STATUS_NOT_FOUND);
  CHECK(invoke([]{throw std::bad_alloc();})==ECCDB_STATUS_INTERNAL_ERROR);
  CHECK(invoke([]{throw 42;})==ECCDB_STATUS_INTERNAL_ERROR);
  return 0;
}
