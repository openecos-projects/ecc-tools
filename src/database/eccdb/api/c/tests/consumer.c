#include "eccdb_c/eccdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"%s:%d: %s (%s)\n",__FILE__,__LINE__,#x,eccdb_last_error_message());exit(1);} } while(0)
#define OK(x) CHECK((x)==ECCDB_STATUS_OK)

int main(void) {
  uint64_t bottom,cut,top,rule,master;
  FILE* file=fopen("ids.txt","r");
  CHECK(file);
  CHECK(fscanf(file,"%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
               &bottom,&cut,&top,&rule,&master)==5);
  fclose(file);
  eccdb_database_t* db=NULL;
  eccdb_config_t cfg;
  eccdb_config_init(&cfg);
  cfg.input_kind=1;
  cfg.binary=(eccdb_binary_files_t){"tech.bin","library.bin","design.bin"};
  OK(eccdb_open(&cfg,&db));
  uint8_t found=0;
  eccdb_net_data_t nd;eccdb_net_data_init(&nd);nd.name="signal";
  eccdb_net_id_t net; OK(eccdb_net_create(db,&nd,&net));
  eccdb_net_id_t power; nd.name="VDD";nd.signal_use=3;
  OK(eccdb_net_create_special(db,&nd,&power));
  eccdb_net_id_t lookup;
  OK(eccdb_net_find_regular(db,"signal",&lookup,&found));CHECK(found&&lookup.value==net.value);
  OK(eccdb_net_find_special(db,"VDD",&lookup,&found));CHECK(found&&lookup.value==power.value);
  OK(eccdb_net_contains(db,net,&found));CHECK(found);
  OK(eccdb_net_is_special(db,power,&found));CHECK(found);
  eccdb_net_id_list_t nets={0};
  OK(eccdb_net_ids(db,&nets));CHECK(nets.count==2);eccdb_net_id_list_free(&nets);
  OK(eccdb_net_regular_ids(db,&nets));CHECK(nets.count==1);eccdb_net_id_list_free(&nets);
  OK(eccdb_net_special_ids(db,&nets));CHECK(nets.count==1);eccdb_net_id_list_free(&nets);
  eccdb_net_data_t net_snapshot={0};
  OK(eccdb_net_get(db,net,&net_snapshot,&found));CHECK(found);
  CHECK(net_snapshot.tech_non_default_rule==ECCDB_INVALID_ID);
  net_snapshot.has_weight=1;net_snapshot.weight=7;
  OK(eccdb_net_update(db,net,&net_snapshot));eccdb_net_data_free(&net_snapshot);
  eccdb_net_options_t opt={0},outopt={0};
  opt.has_pattern=1;opt.pattern=1;opt.has_frequency=1;opt.frequency=1;
  OK(eccdb_net_options_set(db,net,&opt));
  OK(eccdb_net_options_get(db,net,&outopt,&found));
  CHECK(found&&outopt.has_pattern&&outopt.pattern==1&&outopt.has_frequency);
  eccdb_net_options_free(&outopt);
  opt.spacing_rule_count=1;
  CHECK(eccdb_net_options_set(db,net,&opt)==ECCDB_STATUS_INVALID_ARGUMENT);
  opt.spacing_rule_count=0;
  eccdb_instance_data_t inst;eccdb_instance_data_init(&inst);
  inst.name="u1";inst.master=master;
  eccdb_instance_id_t iid;OK(eccdb_instance_create(db,&inst,&iid));
  OK(eccdb_instance_contains(db,iid,&found));CHECK(found);
  eccdb_instance_id_list_t instances={0};
  OK(eccdb_instance_ids(db,&instances));CHECK(instances.count==1);eccdb_instance_id_list_free(&instances);
  eccdb_instance_data_t inst_snapshot={0};
  OK(eccdb_instance_get(db,iid,&inst_snapshot,&found));CHECK(found);
  inst_snapshot.origin_x=100;inst_snapshot.origin_y=200;
  OK(eccdb_instance_update(db,iid,&inst_snapshot));eccdb_instance_data_free(&inst_snapshot);
  eccdb_instance_pin_id_t pin;OK(eccdb_instance_pin_find(db,iid,"A",&pin,&found));CHECK(found);
  OK(eccdb_instance_pin_connect(db,pin,net));
  OK(eccdb_instance_pin_contains(db,pin,&found));CHECK(found);
  eccdb_instance_pin_data_t pin_snapshot={0};
  OK(eccdb_instance_pin_get(db,pin,&pin_snapshot,&found));CHECK(found&&pin_snapshot.net.value==net.value);
  OK(eccdb_instance_pin_disconnect(db,pin));OK(eccdb_instance_pin_connect(db,pin,net));
  eccdb_instance_pin_id_list_t instance_pins={0};
  OK(eccdb_instance_pin_ids(db,iid,&instance_pins));CHECK(instance_pins.count==1);
  eccdb_instance_pin_id_list_free(&instance_pins);
  eccdb_io_pin_data_t io;eccdb_io_pin_data_init(&io);io.name="IN";
  eccdb_io_pin_id_t ioid;OK(eccdb_io_pin_create(db,&io,&ioid));OK(eccdb_io_pin_connect(db,ioid,net));
  OK(eccdb_io_pin_contains(db,ioid,&found));CHECK(found);
  eccdb_io_pin_data_t io_snapshot={0};
  OK(eccdb_io_pin_get(db,ioid,&io_snapshot,&found));CHECK(found&&io_snapshot.net.value==net.value);
  io_snapshot.direction=ECCDB_DIRECTION_INPUT;
  OK(eccdb_io_pin_update(db,ioid,&io_snapshot));eccdb_io_pin_data_free(&io_snapshot);
  OK(eccdb_io_pin_disconnect(db,ioid));OK(eccdb_io_pin_connect(db,ioid,net));
  eccdb_io_pin_id_list_t io_list={0};
  OK(eccdb_io_pin_ids(db,&io_list));CHECK(io_list.count==1);eccdb_io_pin_id_list_free(&io_list);
  eccdb_instance_pin_id_list_t pins={0};eccdb_io_pin_id_list_t ios={0};
  OK(eccdb_net_pin_ids(db,net,&pins,&ios));CHECK(pins.count==1&&ios.count==1);
  eccdb_instance_pin_id_list_free(&pins);eccdb_io_pin_id_list_free(&ios);
  eccdb_design_via_data_t via={0},snapshot={0};
  via.name="generated";via.has_generated=1;
  via.generated=(eccdb_generated_via_data_t){
    .via_rule=rule,.bottom_layer=bottom,.cut_layer=cut,.top_layer=top,
    .cut_size_x=2,.cut_size_y=3,.cut_spacing_x=4,.cut_spacing_y=5,
    .bottom_enclosure_x=6,.bottom_enclosure_y=7,.top_enclosure_x=8,.top_enclosure_y=9,
    .has_row_column=1,.rows=2,.columns=3,.has_origin=1,.origin={4,5},
    .has_offsets=1,.bottom_offset={6,7},.top_offset={8,9},
    .has_cut_pattern=1,.cut_pattern="pattern"
  };
  eccdb_via_id_t vid;OK(eccdb_design_via_create(db,&via,&vid));
  OK(eccdb_design_via_contains(db,vid,&found));CHECK(found);
  eccdb_via_id_list_t via_ids={0};
  OK(eccdb_design_via_ids(db,&via_ids));CHECK(via_ids.count==1);eccdb_via_id_list_free(&via_ids);
  OK(eccdb_design_via_get(db,vid,&snapshot,&found));
  CHECK(found&&snapshot.has_generated&&snapshot.generated.has_offsets);
  CHECK(snapshot.generated.top_offset.y==9&&snapshot.generated.columns==3);
  CHECK(!strcmp(snapshot.generated.cut_pattern,"pattern"));
  snapshot.generated.origin.x=42;
  OK(eccdb_design_via_update(db,vid,&snapshot));
  eccdb_design_via_data_free(&snapshot);
  eccdb_wire_point_t points[2]={{{0,0},0,1,0},{{100,0},0,0,0}};
  eccdb_via_placement_t placement={.point_index=1,.definition_kind=1,.definition=vid.value};
  eccdb_wire_path_t path={.layer={bottom},.has_width=1,.width=20,.points=points,.point_count=2,.vias=&placement,.via_count=1};
  eccdb_wire_routing_t routing={&path,1},out={0};
  eccdb_wire_metadata_t meta={.net=power,.status=1};
  eccdb_wire_id_t wire;OK(eccdb_wire_create(db,&meta,&routing,&wire));
  OK(eccdb_wire_contains(db,wire,&found));CHECK(found);
  eccdb_wire_metadata_t meta_snapshot={0};
  OK(eccdb_wire_metadata_get(db,wire,&meta_snapshot,&found));
  CHECK(found&&meta_snapshot.net.value==power.value&&meta_snapshot.status==1);
  eccdb_wire_metadata_free(&meta_snapshot);
  eccdb_wire_id_list_t net_wires={0};
  OK(eccdb_net_wire_ids(db,power,&net_wires));CHECK(net_wires.count==1);eccdb_wire_id_list_free(&net_wires);
  OK(eccdb_wire_ids_for_net(db,power,&net_wires));CHECK(net_wires.count==1);eccdb_wire_id_list_free(&net_wires);
  OK(eccdb_wire_routing_get(db,wire,&out,&found));CHECK(found&&out.path_count==1);
  CHECK(out.paths[0].points[0].has_extension&&out.paths[0].vias[0].definition==vid.value);
  out.paths[0].points[1].position.x=200;
  OK(eccdb_wire_update(db,wire,&meta,&out));eccdb_wire_routing_free(&out);
  path.points=NULL;
  CHECK(eccdb_wire_update(db,wire,&meta,&routing)==ECCDB_STATUS_INVALID_ARGUMENT);
  path.points=points;
  eccdb_via_rectangle_t rect={{bottom},{-2,-2,2,2},0};
  eccdb_point_t vertices[4]={{-3,-3},{3,-3},{3,3},{-3,3}};
  eccdb_via_polygon_t poly={{top},vertices,4,0};
  eccdb_design_via_data_t fixed={.name="fixed",.rectangles=&rect,.rectangle_count=1,.polygons=&poly,.polygon_count=1};
  eccdb_via_id_t fixed_id;
  OK(eccdb_design_via_create(db,&fixed,&fixed_id));
  OK(eccdb_design_via_get(db,fixed_id,&snapshot,&found));
  CHECK(found&&!snapshot.has_generated&&snapshot.rectangle_count==1&&snapshot.polygon_count==1);
  CHECK(snapshot.polygons[0].points[2].y==3);
  OK(eccdb_design_via_update(db,fixed_id,&snapshot));eccdb_design_via_data_free(&snapshot);
  OK(eccdb_design_via_destroy(db,fixed_id,&found));CHECK(found);
  OK(eccdb_write_binary(db,"out-tech.bin","out-library.bin","out-design.bin"));
  eccdb_database_free(db);db=NULL;
  OK(eccdb_open_binary("out-tech.bin","out-library.bin","out-design.bin",&db));
  OK(eccdb_design_via_find(db,"generated",&vid,&found));CHECK(found);
  OK(eccdb_design_via_get(db,vid,&snapshot,&found));CHECK(found&&snapshot.generated.origin.x==42);
  eccdb_design_via_data_free(&snapshot);
  eccdb_wire_id_list_t wires={0};OK(eccdb_wire_ids(db,&wires));CHECK(wires.count==1);
  wire=wires.items[0];eccdb_wire_id_list_free(&wires);
  OK(eccdb_wire_routing_get(db,wire,&out,&found));CHECK(found&&out.paths[0].points[1].position.x==200);
  eccdb_wire_routing_free(&out);
  OK(eccdb_write_def(db,"output.def"));
  OK(eccdb_wire_destroy(db,wire,&found));CHECK(found);
  OK(eccdb_design_via_destroy(db,vid,&found));CHECK(found);
  OK(eccdb_net_find(db,"signal",&net,&found));CHECK(found);
  OK(eccdb_instance_find(db,"u1",&iid,&found));CHECK(found);
  OK(eccdb_instance_pin_find(db,iid,"A",&pin,&found));CHECK(found);
  OK(eccdb_io_pin_find(db,"IN",&ioid,&found));CHECK(found);
  OK(eccdb_instance_pin_disconnect_from_net(db,pin,net));
  OK(eccdb_io_pin_disconnect_from_net(db,ioid,net));
  OK(eccdb_instance_destroy(db,iid,&found));CHECK(found);
  OK(eccdb_io_pin_destroy(db,ioid,&found));CHECK(found);
  OK(eccdb_net_destroy(db,net,&found));CHECK(found);
  eccdb_import_diagnostic_t* diagnostics=NULL;size_t count=0;
  OK(eccdb_diagnostics(db,&diagnostics,&count));eccdb_diagnostics_free(diagnostics,count);
  eccdb_database_free(db);
  return 0;
}
