#include "eccdb_c/eccdb.h"
#include "eccdb/Database.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ios>
#include <stdexcept>
#include <string>
#include <new>
#include <utility>
#include <vector>
struct eccdb_database { eccdb::Database database; explicit eccdb_database(eccdb::Database v):database(std::move(v)){} };
namespace {
thread_local char last_error[2048] = {};
eccdb_status_t fail(eccdb_status_t s,const char* m) noexcept {std::snprintf(last_error,sizeof(last_error),"%s",m?m:"");return s;}
template<class F> eccdb_status_t invoke(F&& f){try{f();last_error[0]=0;return ECCDB_STATUS_OK;}catch(const std::invalid_argument&e){return fail(ECCDB_STATUS_INVALID_ARGUMENT,e.what());}catch(const std::out_of_range&e){return fail(ECCDB_STATUS_NOT_FOUND,e.what());}catch(const std::logic_error&e){return fail(ECCDB_STATUS_INVALID_STATE,e.what());}catch(const std::ios_base::failure&e){return fail(ECCDB_STATUS_IO_ERROR,e.what());}catch(const std::exception&e){return fail(ECCDB_STATUS_INTERNAL_ERROR,e.what());}catch(...){return fail(ECCDB_STATUS_INTERNAL_ERROR,"unknown eccdb exception");}}
void* checked_calloc(size_t n,size_t size) {
 if(n && size>SIZE_MAX/n) throw std::bad_alloc();
 auto* p=std::calloc(n,size); if(n&&!p) throw std::bad_alloc(); return p;
}
template<class T> void check_array(const T* p,size_t n) {
 if(n&&!p) throw std::invalid_argument("nonempty array has null pointer");
 if(n>SIZE_MAX/sizeof(T)) throw std::invalid_argument("array size overflow");
}
char* copy_string(const std::string& s){auto*p=static_cast<char*>(std::malloc(s.size()+1));if(!p)throw std::bad_alloc();std::memcpy(p,s.c_str(),s.size()+1);return p;}
eccdb::NetData to_net(const eccdb_net_data_t&v){if(!v.name)throw std::invalid_argument("net name is null");eccdb::NetData r{.name=v.name,.use=static_cast<eccdb::SignalUse>(v.signal_use),.source=static_cast<eccdb::NetSource>(v.source),.tech_non_default_rule=eccdb::TechRuleId{v.tech_non_default_rule},.design_non_default_rule=eccdb::DesignRuleId{v.design_non_default_rule}};if(v.has_weight)r.weight=v.weight;return r;}
void from_net(const eccdb::NetData&v,eccdb_net_data_t&o){o.name=copy_string(v.name);o.signal_use=static_cast<uint8_t>(v.use);o.source=static_cast<uint8_t>(v.source);o.has_weight=v.weight.has_value();o.weight=v.weight.value_or(0);o.tech_non_default_rule=v.tech_non_default_rule.packed();o.design_non_default_rule=v.design_non_default_rule.packed();}
eccdb::InstanceData to_instance(const eccdb_instance_data_t&v){if(!v.name)throw std::invalid_argument("instance name is null");return {.name=v.name,.master=eccdb::CellMasterId{v.master},.origin={v.origin_x,v.origin_y},.orientation=static_cast<eccdb::Orientation>(v.orientation),.placement_status=static_cast<eccdb::PlacementStatus>(v.placement_status),.source=static_cast<eccdb::InstanceSource>(v.source)};}
void from_instance(const eccdb::InstanceData&v,eccdb_instance_data_t&o){o.name=copy_string(v.name);o.master=v.master.packed();o.origin_x=v.origin.x;o.origin_y=v.origin.y;o.orientation=static_cast<uint8_t>(v.orientation);o.placement_status=static_cast<uint8_t>(v.placement_status);o.source=static_cast<uint8_t>(v.source);}
eccdb::InstancePinData to_instance_pin(const eccdb_instance_pin_data_t&v){return {.instance=eccdb::InstanceId{v.instance.value},.master_term=eccdb::MasterTermId{v.master_term},.net=eccdb::NetId{v.net.value},.special_net=eccdb::NetId{v.special_net.value}};}
void from_instance_pin(const eccdb::InstancePinData&v,eccdb_instance_pin_data_t&o){o.instance={v.instance.packed()};o.master_term=v.master_term.packed();o.net={v.net.packed()};o.special_net={v.special_net.packed()};}
eccdb::IoPinData to_io_pin(const eccdb_io_pin_data_t&v){if(!v.name)throw std::invalid_argument("IO pin name is null");return {.name=v.name,.direction=static_cast<eccdb::IoDirection>(v.direction),.use=static_cast<eccdb::SignalUse>(v.use),.net=eccdb::NetId{v.net.value},.special_net=eccdb::NetId{v.special_net.value}};}
void from_io_pin(const eccdb::IoPinData&v,eccdb_io_pin_data_t&o){o.name=copy_string(v.name);o.direction=static_cast<uint8_t>(v.direction);o.use=static_cast<uint8_t>(v.use);o.net={v.net.packed()};o.special_net={v.special_net.packed()};}
void from_wire_metadata(const eccdb::WireMetadata&v,eccdb_wire_metadata_t&o){o.net={v.net.packed()};o.status=static_cast<uint8_t>(v.status);o.shield_net=copy_string(v.shield_net);}
eccdb::NetOptions to_options(const eccdb_net_options_t&v){check_array(v.spacing_rules,v.spacing_rule_count);eccdb::NetOptions r;if(v.has_original){if(!v.original)throw std::invalid_argument("original is null");r.original=v.original;}if(v.has_pattern)r.pattern=static_cast<eccdb::NetPattern>(v.pattern);if(v.has_estimated_capacitance)r.estimated_capacitance=v.estimated_capacitance;if(v.has_frequency)r.frequency=v.frequency;if(v.has_xtalk)r.xtalk=v.xtalk;if(v.has_style)r.style=v.style;if(v.has_voltage)r.voltage=v.voltage;for(size_t i=0;i<v.spacing_rule_count;++i){auto&s=v.spacing_rules[i];eccdb::NetSpacingRule x{.layer=eccdb::RoutingLayerId{s.layer.value},.spacing=s.spacing};if(s.has_range)x.range=std::pair{s.range_min,s.range_max};r.spacing_rules.push_back(x);}return r;}
void from_options(const eccdb::NetOptions&v,eccdb_net_options_t&o){if(v.original){o.has_original=1;o.original=copy_string(*v.original);}o.has_pattern=v.pattern.has_value();if(v.pattern)o.pattern=static_cast<uint8_t>(*v.pattern);if(v.estimated_capacitance){o.has_estimated_capacitance=1;o.estimated_capacitance=*v.estimated_capacitance;}if(v.frequency){o.has_frequency=1;o.frequency=*v.frequency;}if(v.xtalk){o.has_xtalk=1;o.xtalk=*v.xtalk;}if(v.style){o.has_style=1;o.style=*v.style;}if(v.voltage){o.has_voltage=1;o.voltage=*v.voltage;}o.spacing_rule_count=v.spacing_rules.size();if(o.spacing_rule_count)o.spacing_rules=static_cast<eccdb_net_spacing_rule_t*>(checked_calloc(o.spacing_rule_count,sizeof(*o.spacing_rules)));for(size_t i=0;i<o.spacing_rule_count;++i){auto&s=v.spacing_rules[i];o.spacing_rules[i].layer={s.layer.packed()};o.spacing_rules[i].spacing=s.spacing;if(s.range){o.spacing_rules[i].has_range=1;o.spacing_rules[i].range_min=s.range->first;o.spacing_rules[i].range_max=s.range->second;}}}
eccdb::WireMetadata to_wire_metadata(const eccdb_wire_metadata_t&v){return {.net=eccdb::NetId{v.net.value},.status=static_cast<eccdb::WireStatus>(v.status),.shield_net=v.shield_net?v.shield_net:""};}
eccdb::WireRoutingData to_routing(const eccdb_wire_routing_t&v){check_array(v.paths,v.path_count);eccdb::WireRoutingData r;for(size_t i=0;i<v.path_count;++i){auto&p=v.paths[i];check_array(p.points,p.point_count);check_array(p.vias,p.via_count);check_array(p.rectangles,p.rectangle_count);eccdb::WirePathData x{.layer=eccdb::RoutingLayerId{p.layer.value},.taper=p.taper!=0};if(p.has_width)x.width=p.width;if(p.has_mask)x.mask=p.mask;if(p.has_taper_rule){if(!p.taper_rule)throw std::invalid_argument("taper rule is null");x.taper_rule=p.taper_rule;}if(p.has_shape){if(!p.shape)throw std::invalid_argument("shape is null");x.shape=p.shape;}if(p.has_style)x.style=p.style;for(size_t j=0;j<p.point_count;++j){auto&q=p.points[j];eccdb::WirePoint w{.position={q.position.x,q.position.y},.virtual_point=q.virtual_point!=0};if(q.has_extension)w.extension=q.extension;x.points.push_back(w);}for(size_t j=0;j<p.via_count;++j){auto&q=p.vias[j];if(q.definition_kind>1)throw std::invalid_argument("invalid via definition kind");eccdb::ViaPlacementData w{.point_index=q.point_index,.orientation=static_cast<eccdb::Orientation>(q.orientation),.step_x=q.step_x,.step_y=q.step_y};w.definition=q.definition_kind==0?eccdb::ViaDefinitionId{eccdb::TechViaId{q.definition}}:eccdb::ViaDefinitionId{eccdb::ViaId{q.definition}};if(q.has_mask)w.mask=eccdb::WireMask{q.mask.top,q.mask.cut,q.mask.bottom};if(q.has_rows_columns)w.rows_columns=std::pair{q.rows,q.columns};x.vias.push_back(w);}for(size_t j=0;j<p.rectangle_count;++j){auto&q=p.rectangles[j];x.rectangles.push_back({q.point_index,{q.delta.ll_x,q.delta.ll_y,q.delta.ur_x,q.delta.ur_y}});}r.paths.push_back(std::move(x));}return r;}
void from_routing(const eccdb::WireRoutingData&v,eccdb_wire_routing_t&o){o.path_count=v.paths.size();if(o.path_count)o.paths=static_cast<eccdb_wire_path_t*>(checked_calloc(o.path_count,sizeof(*o.paths)));for(size_t i=0;i<o.path_count;++i){auto&s=v.paths[i];auto&d=o.paths[i];d.layer={s.layer.packed()};d.taper=s.taper;if(s.width){d.has_width=1;d.width=*s.width;}if(s.mask){d.has_mask=1;d.mask=*s.mask;}if(s.taper_rule){d.has_taper_rule=1;d.taper_rule=copy_string(*s.taper_rule);}if(s.shape){d.has_shape=1;d.shape=copy_string(*s.shape);}if(s.style){d.has_style=1;d.style=*s.style;}d.point_count=s.points.size();if(d.point_count)d.points=static_cast<eccdb_wire_point_t*>(checked_calloc(d.point_count,sizeof(*d.points)));for(size_t j=0;j<d.point_count;++j){auto&q=s.points[j];d.points[j].position={q.position.x,q.position.y};d.points[j].virtual_point=q.virtual_point;if(q.extension){d.points[j].has_extension=1;d.points[j].extension=*q.extension;}}d.via_count=s.vias.size();if(d.via_count)d.vias=static_cast<eccdb_via_placement_t*>(checked_calloc(d.via_count,sizeof(*d.vias)));for(size_t j=0;j<d.via_count;++j){auto&q=s.vias[j];auto&w=d.vias[j];w.point_index=q.point_index;w.orientation=static_cast<uint8_t>(q.orientation);w.step_x=q.step_x;w.step_y=q.step_y;if(auto*t=std::get_if<eccdb::TechViaId>(&q.definition)){w.definition_kind=0;w.definition=t->packed();}else{w.definition_kind=1;w.definition=std::get<eccdb::ViaId>(q.definition).packed();}if(q.mask){w.has_mask=1;w.mask={q.mask->top,q.mask->cut,q.mask->bottom};}if(q.rows_columns){w.has_rows_columns=1;w.rows=q.rows_columns->first;w.columns=q.rows_columns->second;}}d.rectangle_count=s.rectangles.size();if(d.rectangle_count)d.rectangles=static_cast<eccdb_wire_rectangle_t*>(checked_calloc(d.rectangle_count,sizeof(*d.rectangles)));for(size_t j=0;j<d.rectangle_count;++j){auto&q=s.rectangles[j];d.rectangles[j].point_index=q.point_index;d.rectangles[j].delta={q.delta.ll_x,q.delta.ll_y,q.delta.ur_x,q.delta.ur_y};}}}

eccdb::GeneratedViaData to_generated(const eccdb_generated_via_data_t& v) {
 eccdb::GeneratedViaData r;
 r.via_rule=eccdb::ViaRuleId{v.via_rule};
 r.bottom_layer=eccdb::RoutingLayerId{v.bottom_layer}; r.cut_layer=eccdb::LayerId{v.cut_layer}; r.top_layer=eccdb::RoutingLayerId{v.top_layer};
 r.cut_size_x=v.cut_size_x;
r.cut_size_y=v.cut_size_y;
r.cut_spacing_x=v.cut_spacing_x;
r.cut_spacing_y=v.cut_spacing_y;
r.bottom_enclosure_x=v.bottom_enclosure_x;
r.bottom_enclosure_y=v.bottom_enclosure_y;
r.top_enclosure_x=v.top_enclosure_x;
r.top_enclosure_y=v.top_enclosure_y;
 if(v.has_row_column)r.row_column=std::pair{v.rows,v.columns};
 if(v.has_origin)r.origin=eccdb::Point{v.origin.x,v.origin.y};
 if(v.has_offsets)r.offsets=std::pair{eccdb::Point{v.bottom_offset.x,v.bottom_offset.y},eccdb::Point{v.top_offset.x,v.top_offset.y}};
 if(v.has_cut_pattern){if(!v.cut_pattern)throw std::invalid_argument("cut pattern is null");r.cut_pattern=v.cut_pattern;}
 return r;
}
void from_generated(const eccdb::GeneratedViaData& v,eccdb_generated_via_data_t& r) {
 r.via_rule=v.via_rule.packed();r.bottom_layer=v.bottom_layer.packed();r.cut_layer=v.cut_layer.packed();r.top_layer=v.top_layer.packed();
 r.cut_size_x=v.cut_size_x;
r.cut_size_y=v.cut_size_y;
r.cut_spacing_x=v.cut_spacing_x;
r.cut_spacing_y=v.cut_spacing_y;
r.bottom_enclosure_x=v.bottom_enclosure_x;
r.bottom_enclosure_y=v.bottom_enclosure_y;
r.top_enclosure_x=v.top_enclosure_x;
r.top_enclosure_y=v.top_enclosure_y;
 if(v.row_column){r.has_row_column=1;r.rows=v.row_column->first;r.columns=v.row_column->second;}
 if(v.origin){r.has_origin=1;r.origin={v.origin->x,v.origin->y};}
 if(v.offsets){r.has_offsets=1;r.bottom_offset={v.offsets->first.x,v.offsets->first.y};r.top_offset={v.offsets->second.x,v.offsets->second.y};}
 if(v.cut_pattern){r.has_cut_pattern=1;r.cut_pattern=copy_string(*v.cut_pattern);}
}
eccdb::BinaryFiles to_files(const eccdb_binary_files_t& v) {
 if(!v.technology||!v.library||!v.design)throw std::invalid_argument("binary file path is null");
 return {v.technology,v.library,v.design};
}
eccdb::Config to_config(const eccdb_config_t& v) {
 if(v.input_kind>1||v.polygon_mode>1)throw std::invalid_argument("invalid config tag");
 eccdb::Config r;
 if(v.input_kind==1)r.input=eccdb::BinaryInput{to_files(v.binary)};
 else {
  check_array(v.lef_files,v.lef_count);
  if(!v.def_file)throw std::invalid_argument("DEF path is null");
  eccdb::LefDefInput input;input.def_file=v.def_file;
  for(size_t i=0;i<v.lef_count;++i){if(!v.lef_files[i])throw std::invalid_argument("LEF path is null");input.lef_files.emplace_back(v.lef_files[i]);}
  r.input=std::move(input);
 }
 r.runtime.polygon_mode=static_cast<eccdb::PolygonMode>(v.polygon_mode);
 if(v.has_binary_cache)r.binary_cache=to_files(v.binary_cache);
 return r;
}

template<class Id,class Out,class Wrap,class Get> void fill_ids(Out&o,Get&&get){const auto ids=get();if(ids.size()>SIZE_MAX/sizeof(Wrap))throw std::bad_alloc();o.items=static_cast<Wrap*>(std::malloc(ids.size()*sizeof(Wrap)));if(!o.items&& !ids.empty())throw std::bad_alloc();o.count=ids.size();for(size_t i=0;i<ids.size();++i)o.items[i]={ids[i].packed()};}
}
extern "C" {
void eccdb_config_init(eccdb_config_t* v){if(v)*v={};}
eccdb_status_t eccdb_open(const eccdb_config_t* v,eccdb_database_t** o){return invoke([&]{if(!v||!o||*o)throw std::invalid_argument("invalid open arguments");*o=new eccdb_database(eccdb::Database::open(to_config(*v)));});}
eccdb_status_t eccdb_supports_lef_def(uint8_t*o){return invoke([&]{if(!o)throw std::invalid_argument("output is null");*o=eccdb::Database::supportsLefDef();});}
eccdb_status_t eccdb_open_binary(const char*t,const char*l,const char*d,eccdb_database_t**o){return invoke([&]{if(!t||!l||!d||!o||*o)throw std::invalid_argument("invalid open arguments");eccdb::BinaryFiles f{t,l,d};*o=new eccdb_database(eccdb::Database::open({.input=eccdb::BinaryInput{.files=std::move(f)}}));});}
eccdb_status_t eccdb_open_lef_def(const char*lef,const char*def,eccdb_database_t**o){return invoke([&]{if(!lef||!def||!o||*o)throw std::invalid_argument("invalid open arguments");eccdb::LefDefInput input;input.lef_files.emplace_back(lef);input.def_file=def;*o=new eccdb_database(eccdb::Database::open({.input=std::move(input)}));});}
void eccdb_database_free(eccdb_database_t*d){delete d;} const char*eccdb_last_error_message(void){return last_error;}
eccdb_status_t eccdb_write_binary(const eccdb_database_t*d,const char*t,const char*l,const char*design){return invoke([&]{if(!d||!t||!l||!design)throw std::invalid_argument("invalid binary output arguments");d->database.writeBinary({t,l,design});});}
eccdb_status_t eccdb_write_def(const eccdb_database_t*d,const char*file){return invoke([&]{if(!d||!file)throw std::invalid_argument("invalid DEF output arguments");d->database.writeDef(file);});}
eccdb_status_t eccdb_net_find(const eccdb_database_t*d,const char*n,eccdb_net_id_t*o,uint8_t*f){return invoke([&]{if(!d||!n||!o||!f)throw std::invalid_argument("invalid net find arguments");auto id=d->database.findNetId(n);*o={id.packed()};*f=id ? 1 : 0;});}
eccdb_status_t eccdb_net_find_regular(const eccdb_database_t*d,const char*n,eccdb_net_id_t*o,uint8_t*f){return invoke([&]{if(!d||!n||!o||!f)throw std::invalid_argument("invalid net find arguments");auto id=d->database.findRegularNetId(n);*o={id.packed()};*f=id ? 1 : 0;});}
eccdb_status_t eccdb_net_find_special(const eccdb_database_t*d,const char*n,eccdb_net_id_t*o,uint8_t*f){return invoke([&]{if(!d||!n||!o||!f)throw std::invalid_argument("invalid net find arguments");auto id=d->database.findSpecialNetId(n);*o={id.packed()};*f=id ? 1 : 0;});}
eccdb_status_t eccdb_net_contains(const eccdb_database_t*d,eccdb_net_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid net contains arguments");*o=d->database.contains(eccdb::NetId{id.value});});}
eccdb_status_t eccdb_net_create(eccdb_database_t*d,const eccdb_net_data_t*v,eccdb_net_id_t*o){return invoke([&]{if(!d||!v||!o)throw std::invalid_argument("invalid net create arguments");*o={d->database.createNet(to_net(*v)).packed()};});}
eccdb_status_t eccdb_net_create_special(eccdb_database_t*d,const eccdb_net_data_t*v,eccdb_net_id_t*o){return invoke([&]{if(!d||!v||!o)throw std::invalid_argument("invalid net create arguments");*o={d->database.createSpecialNet(to_net(*v)).packed()};});}
eccdb_status_t eccdb_net_is_special(const eccdb_database_t*d,eccdb_net_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid net special arguments");*o=d->database.isSpecialNet(eccdb::NetId{id.value});});}
eccdb_status_t eccdb_net_get(const eccdb_database_t*d,eccdb_net_id_t id,eccdb_net_data_t*o,uint8_t*f){return invoke([&]{if(!d||!o||!f)throw std::invalid_argument("invalid net get arguments");auto v=d->database.netData(eccdb::NetId{id.value});*f=v.has_value();if(v)from_net(*v,*o);});}
eccdb_status_t eccdb_net_update(eccdb_database_t*d,eccdb_net_id_t id,const eccdb_net_data_t*v){return invoke([&]{if(!d||!v)throw std::invalid_argument("invalid net update arguments");d->database.updateNet(eccdb::NetId{id.value},to_net(*v));});}
eccdb_status_t eccdb_net_destroy(eccdb_database_t*d,eccdb_net_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid net destroy arguments");*o=d->database.destroyNet(eccdb::NetId{id.value});});}
eccdb_status_t eccdb_net_ids(const eccdb_database_t*d,eccdb_net_id_list_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid net list arguments");*o={};fill_ids<eccdb::NetId,eccdb_net_id_list_t,eccdb_net_id_t>(*(o),[&]{return d->database.netIds();});});}
eccdb_status_t eccdb_net_regular_ids(const eccdb_database_t*d,eccdb_net_id_list_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid net list arguments");*o={};fill_ids<eccdb::NetId,eccdb_net_id_list_t,eccdb_net_id_t>(*(o),[&]{return d->database.regularNetIds();});});}
eccdb_status_t eccdb_net_special_ids(const eccdb_database_t*d,eccdb_net_id_list_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid net list arguments");*o={};fill_ids<eccdb::NetId,eccdb_net_id_list_t,eccdb_net_id_t>(*(o),[&]{return d->database.specialNetIds();});});}
eccdb_status_t eccdb_net_options_get(const eccdb_database_t*d,eccdb_net_id_t id,eccdb_net_options_t*o,uint8_t*f){return invoke([&]{if(!d||!o||!f)throw std::invalid_argument("invalid net options arguments");*f=0;auto v=d->database.netOptions(eccdb::NetId{id.value});if(v){*o={};from_options(*v,*o);*f=1;}});}
eccdb_status_t eccdb_net_options_set(eccdb_database_t*d,eccdb_net_id_t id,const eccdb_net_options_t*v){return invoke([&]{if(!d||!v)throw std::invalid_argument("invalid net options arguments");d->database.setNetOptions(eccdb::NetId{id.value},to_options(*v));});}
eccdb_status_t eccdb_net_pin_ids(const eccdb_database_t*d,eccdb_net_id_t id,eccdb_instance_pin_id_list_t*i,eccdb_io_pin_id_list_t*o){return invoke([&]{if(!d||!i||!o)throw std::invalid_argument("invalid net pin list arguments");*i={};*o={};fill_ids<eccdb::InstancePinId,eccdb_instance_pin_id_list_t,eccdb_instance_pin_id_t>(*i,[&]{return d->database.instancePinIds(eccdb::NetId{id.value});});fill_ids<eccdb::IoPinId,eccdb_io_pin_id_list_t,eccdb_io_pin_id_t>(*o,[&]{return d->database.ioPinIds(eccdb::NetId{id.value});});});}
eccdb_status_t eccdb_net_wire_ids(const eccdb_database_t*d,eccdb_net_id_t id,eccdb_wire_id_list_t*o){return eccdb_wire_ids_for_net(d,id,o);}
void eccdb_net_id_list_free(eccdb_net_id_list_t*o){if(o){std::free(o->items);*o={};}}
void eccdb_net_data_free(eccdb_net_data_t*v){if(v){std::free(v->name);*v={};}}
void eccdb_net_data_init(eccdb_net_data_t*v){if(v){*v={};v->tech_non_default_rule=UINT64_MAX;v->design_non_default_rule=UINT64_MAX;}}
void eccdb_net_options_free(eccdb_net_options_t*o){if(o){std::free(o->original);std::free(o->spacing_rules);*o={};}}
void eccdb_net_options_init(eccdb_net_options_t*o){if(o)*o={};}
eccdb_status_t eccdb_instance_find(const eccdb_database_t*d,const char*n,eccdb_instance_id_t*o,uint8_t*f){return invoke([&]{if(!d||!n||!o||!f)throw std::invalid_argument("invalid instance find arguments");auto id=d->database.findInstanceId(n);*o={id.packed()};*f=id ? 1 : 0;});}
eccdb_status_t eccdb_instance_contains(const eccdb_database_t*d,eccdb_instance_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid instance contains arguments");*o=d->database.contains(eccdb::InstanceId{id.value});});}
eccdb_status_t eccdb_instance_create(eccdb_database_t*d,const eccdb_instance_data_t*v,eccdb_instance_id_t*o){return invoke([&]{if(!d||!v||!o)throw std::invalid_argument("invalid instance create arguments");*o={d->database.createInstance(to_instance(*v)).packed()};});}
eccdb_status_t eccdb_instance_get(const eccdb_database_t*d,eccdb_instance_id_t id,eccdb_instance_data_t*o,uint8_t*f){return invoke([&]{if(!d||!o||!f)throw std::invalid_argument("invalid instance get arguments");auto v=d->database.instanceData(eccdb::InstanceId{id.value});*f=v.has_value();if(v)from_instance(*v,*o);});}
eccdb_status_t eccdb_instance_update(eccdb_database_t*d,eccdb_instance_id_t id,const eccdb_instance_data_t*v){return invoke([&]{if(!d||!v)throw std::invalid_argument("invalid instance update arguments");d->database.updateInstance(eccdb::InstanceId{id.value},to_instance(*v));});}
eccdb_status_t eccdb_instance_destroy(eccdb_database_t*d,eccdb_instance_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid instance destroy arguments");*o=d->database.destroyInstance(eccdb::InstanceId{id.value});});}
eccdb_status_t eccdb_instance_ids(const eccdb_database_t*d,eccdb_instance_id_list_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid instance list arguments");*o={};fill_ids<eccdb::InstanceId,eccdb_instance_id_list_t,eccdb_instance_id_t>(*(o),[&]{return d->database.instanceIds();});});}
void eccdb_instance_id_list_free(eccdb_instance_id_list_t*o){if(o){std::free(o->items);*o={};}}
void eccdb_instance_data_free(eccdb_instance_data_t*v){if(v){std::free(v->name);*v={};}}
void eccdb_instance_data_init(eccdb_instance_data_t*v){if(v){*v={};v->master=UINT64_MAX;}}
eccdb_status_t eccdb_instance_pin_contains(const eccdb_database_t*d,eccdb_instance_pin_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid instance pin contains arguments");*o=d->database.contains(eccdb::InstancePinId{id.value});});}
eccdb_status_t eccdb_instance_pin_get(const eccdb_database_t*d,eccdb_instance_pin_id_t id,eccdb_instance_pin_data_t*o,uint8_t*f){return invoke([&]{if(!d||!o||!f)throw std::invalid_argument("invalid instance pin get arguments");auto v=d->database.instancePinData(eccdb::InstancePinId{id.value});*f=v.has_value();if(v)from_instance_pin(*v,*o);});}
eccdb_status_t eccdb_instance_pin_ids(const eccdb_database_t*d,eccdb_instance_id_t id,eccdb_instance_pin_id_list_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid instance pin list arguments");*o={};fill_ids<eccdb::InstancePinId,eccdb_instance_pin_id_list_t,eccdb_instance_pin_id_t>(*(o),[&]{return d->database.instancePinIds(eccdb::InstanceId{id.value});});});}
eccdb_status_t eccdb_instance_pin_find(const eccdb_database_t*d,eccdb_instance_id_t id,const char*n,eccdb_instance_pin_id_t*o,uint8_t*f){return invoke([&]{if(!d||!n||!o||!f)throw std::invalid_argument("invalid instance pin find arguments");auto v=d->database.findInstancePinId(eccdb::InstanceId{id.value},n);*o={v.packed()};*f=v ? 1 : 0;});}
eccdb_status_t eccdb_instance_pin_connect(eccdb_database_t*d,eccdb_instance_pin_id_t p,eccdb_net_id_t n){return invoke([&]{if(!d)throw std::invalid_argument("database is null");d->database.connect(eccdb::InstancePinId{p.value},eccdb::NetId{n.value});});}
eccdb_status_t eccdb_instance_pin_disconnect(eccdb_database_t*d,eccdb_instance_pin_id_t p){return invoke([&]{if(!d)throw std::invalid_argument("database is null");d->database.disconnect(eccdb::InstancePinId{p.value});});}
eccdb_status_t eccdb_instance_pin_disconnect_from_net(eccdb_database_t*d,eccdb_instance_pin_id_t p,eccdb_net_id_t n){return invoke([&]{if(!d)throw std::invalid_argument("database is null");d->database.disconnect(eccdb::InstancePinId{p.value},eccdb::NetId{n.value});});}
void eccdb_instance_pin_id_list_free(eccdb_instance_pin_id_list_t*o){if(o){std::free(o->items);*o={};}}
eccdb_status_t eccdb_io_pin_create(eccdb_database_t*d,const eccdb_io_pin_data_t*v,eccdb_io_pin_id_t*o){return invoke([&]{if(!d||!v||!o)throw std::invalid_argument("invalid IO pin create arguments");*o={d->database.createIoPin(to_io_pin(*v)).packed()};});}
eccdb_status_t eccdb_io_pin_contains(const eccdb_database_t*d,eccdb_io_pin_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid IO pin contains arguments");*o=d->database.contains(eccdb::IoPinId{id.value});});}
eccdb_status_t eccdb_io_pin_find(const eccdb_database_t*d,const char*n,eccdb_io_pin_id_t*o,uint8_t*f){return invoke([&]{if(!d||!n||!o||!f)throw std::invalid_argument("invalid IO pin find arguments");auto id=d->database.findIoPinId(n);*o={id.packed()};*f=id ? 1 : 0;});}
eccdb_status_t eccdb_io_pin_get(const eccdb_database_t*d,eccdb_io_pin_id_t id,eccdb_io_pin_data_t*o,uint8_t*f){return invoke([&]{if(!d||!o||!f)throw std::invalid_argument("invalid IO pin get arguments");auto v=d->database.ioPinData(eccdb::IoPinId{id.value});*f=v.has_value();if(v)from_io_pin(*v,*o);});}
eccdb_status_t eccdb_io_pin_update(eccdb_database_t*d,eccdb_io_pin_id_t id,const eccdb_io_pin_data_t*v){return invoke([&]{if(!d||!v)throw std::invalid_argument("invalid IO pin update arguments");d->database.updateIoPin(eccdb::IoPinId{id.value},to_io_pin(*v));});}
eccdb_status_t eccdb_io_pin_destroy(eccdb_database_t*d,eccdb_io_pin_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid IO pin destroy arguments");*o=d->database.destroyIoPin(eccdb::IoPinId{id.value});});}
eccdb_status_t eccdb_io_pin_ids(const eccdb_database_t*d,eccdb_io_pin_id_list_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid IO pin list arguments");*o={};fill_ids<eccdb::IoPinId,eccdb_io_pin_id_list_t,eccdb_io_pin_id_t>(*(o),[&]{return d->database.ioPinIds();});});}
eccdb_status_t eccdb_io_pin_connect(eccdb_database_t*d,eccdb_io_pin_id_t p,eccdb_net_id_t n){return invoke([&]{if(!d)throw std::invalid_argument("database is null");d->database.connect(eccdb::IoPinId{p.value},eccdb::NetId{n.value});});}
eccdb_status_t eccdb_io_pin_disconnect(eccdb_database_t*d,eccdb_io_pin_id_t p){return invoke([&]{if(!d)throw std::invalid_argument("database is null");d->database.disconnect(eccdb::IoPinId{p.value});});}
eccdb_status_t eccdb_io_pin_disconnect_from_net(eccdb_database_t*d,eccdb_io_pin_id_t p,eccdb_net_id_t n){return invoke([&]{if(!d)throw std::invalid_argument("database is null");d->database.disconnect(eccdb::IoPinId{p.value},eccdb::NetId{n.value});});}
void eccdb_io_pin_data_free(eccdb_io_pin_data_t*o){if(o){std::free(o->name);*o={};}}
void eccdb_io_pin_data_init(eccdb_io_pin_data_t*o){if(o){*o={};o->net.value=UINT64_MAX;o->special_net.value=UINT64_MAX;}}
void eccdb_io_pin_id_list_free(eccdb_io_pin_id_list_t*o){if(o){std::free(o->items);*o={};}}
eccdb_status_t eccdb_wire_contains(const eccdb_database_t*d,eccdb_wire_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid wire contains arguments");*o=d->database.contains(eccdb::WireId{id.value});});}
eccdb_status_t eccdb_wire_ids(const eccdb_database_t*d,eccdb_wire_id_list_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid wire list arguments");*o={};fill_ids<eccdb::WireId,eccdb_wire_id_list_t,eccdb_wire_id_t>(*(o),[&]{return d->database.wireIds();});});}
eccdb_status_t eccdb_wire_ids_for_net(const eccdb_database_t*d,eccdb_net_id_t n,eccdb_wire_id_list_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid wire list arguments");*o={};fill_ids<eccdb::WireId,eccdb_wire_id_list_t,eccdb_wire_id_t>(*(o),[&]{return d->database.wireIds(eccdb::NetId{n.value});});});}
eccdb_status_t eccdb_wire_metadata_get(const eccdb_database_t*d,eccdb_wire_id_t id,eccdb_wire_metadata_t*o,uint8_t*f){return invoke([&]{if(!d||!o||!f)throw std::invalid_argument("invalid wire metadata arguments");auto v=d->database.wireMetadata(eccdb::WireId{id.value});*f=v.has_value();if(v)from_wire_metadata(*v,*o);});}
eccdb_status_t eccdb_wire_create(eccdb_database_t*d,const eccdb_wire_metadata_t*m,const eccdb_wire_routing_t*r,eccdb_wire_id_t*o){return invoke([&]{if(!d||!m||!r||!o)throw std::invalid_argument("invalid wire create arguments");*o={d->database.createWire(to_wire_metadata(*m),to_routing(*r)).packed()};});}
eccdb_status_t eccdb_wire_routing_get(const eccdb_database_t*d,eccdb_wire_id_t id,eccdb_wire_routing_t*o,uint8_t*f){return invoke([&]{if(!d||!o||!f)throw std::invalid_argument("invalid wire routing arguments");*f=0;auto v=d->database.wireRoutingData(eccdb::WireId{id.value});if(v){*o={};from_routing(*v,*o);*f=1;}});}
eccdb_status_t eccdb_wire_update(eccdb_database_t*d,eccdb_wire_id_t id,const eccdb_wire_metadata_t*m,const eccdb_wire_routing_t*r){return invoke([&]{if(!d||!m||!r)throw std::invalid_argument("invalid wire update arguments");d->database.updateWire(eccdb::WireId{id.value},to_wire_metadata(*m),to_routing(*r));});}
eccdb_status_t eccdb_wire_destroy(eccdb_database_t*d,eccdb_wire_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid wire destroy arguments");*o=d->database.destroyWire(eccdb::WireId{id.value});});}
void eccdb_wire_metadata_free(eccdb_wire_metadata_t*o){if(o){std::free(o->shield_net);*o={};}}
void eccdb_wire_metadata_init(eccdb_wire_metadata_t*o){if(o)*o={};}
void eccdb_wire_routing_free(eccdb_wire_routing_t*o){if(o){for(size_t i=0;o->paths && i<o->path_count;++i){auto&p=o->paths[i];std::free(p.taper_rule);std::free(p.shape);std::free(p.points);std::free(p.vias);std::free(p.rectangles);}std::free(o->paths);*o={};}}
void eccdb_wire_routing_init(eccdb_wire_routing_t*o){if(o)*o={};}
void eccdb_wire_id_list_free(eccdb_wire_id_list_t*o){if(o){std::free(o->items);*o={};}}
eccdb_status_t eccdb_diagnostics(const eccdb_database_t*d,eccdb_import_diagnostic_t**o,size_t*n){return invoke([&]{if(!d||!o||!n)throw std::invalid_argument("invalid diagnostics arguments");const auto&v=d->database.diagnostics();*o=nullptr;*n=v.size();if(*n)*o=static_cast<eccdb_import_diagnostic_t*>(checked_calloc(*n,sizeof(**o)));for(size_t i=0;i<*n;++i){(*o)[i].source=copy_string(v[i].source);(*o)[i].statement=copy_string(v[i].statement);(*o)[i].occurrence_count=v[i].occurrence_count;}});}
void eccdb_diagnostics_free(eccdb_import_diagnostic_t*o,size_t n){if(o){for(size_t i=0;i<n;++i){std::free(o[i].source);std::free(o[i].statement);}std::free(o);}}
eccdb_status_t eccdb_design_via_create(eccdb_database_t*d,const eccdb_design_via_data_t*v,eccdb_via_id_t*o){return invoke([&]{if(!d||!v||!o||!v->name)throw std::invalid_argument("invalid design via arguments");check_array(v->rectangles,v->rectangle_count);check_array(v->polygons,v->polygon_count);eccdb::DesignViaData x{.name=v->name};if(v->has_generated)x.generated=to_generated(v->generated);if(v->has_pattern_name){if(!v->pattern_name)throw std::invalid_argument("pattern name is null");x.pattern_name=v->pattern_name;}for(size_t i=0;i<v->rectangle_count;++i){auto&r=v->rectangles[i];x.rectangles.push_back({eccdb::LayerId{r.layer.value},{r.rectangle.ll_x,r.rectangle.ll_y,r.rectangle.ur_x,r.rectangle.ur_y},r.mask});}for(size_t i=0;i<v->polygon_count;++i){auto&p=v->polygons[i];check_array(p.points,p.point_count);eccdb::ViaPolygon q{.layer=eccdb::LayerId{p.layer.value},.mask=p.mask};for(size_t j=0;j<p.point_count;++j)q.points.push_back({p.points[j].x,p.points[j].y});x.polygons.push_back(std::move(q));}*o={d->database.createDesignVia(std::move(x)).packed()};});}
eccdb_status_t eccdb_design_via_contains(const eccdb_database_t*d,eccdb_via_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid design via arguments");*o=d->database.contains(eccdb::ViaId{id.value});});}
eccdb_status_t eccdb_design_via_find(const eccdb_database_t*d,const char*n,eccdb_via_id_t*o,uint8_t*f){return invoke([&]{if(!d||!n||!o||!f)throw std::invalid_argument("invalid design via arguments");auto id=d->database.findDesignViaId(n);*o={id.packed()};*f=id ? 1 : 0;});}
eccdb_status_t eccdb_design_via_get(const eccdb_database_t*d,eccdb_via_id_t id,eccdb_design_via_data_t*o,uint8_t*f){return invoke([&]{if(!d||!o||!f)throw std::invalid_argument("invalid design via arguments");auto v=d->database.designViaData(eccdb::ViaId{id.value});*f=v.has_value();if(!v)return;*o={};o->name=copy_string(v->name);if(v->generated){o->has_generated=1;from_generated(*v->generated,o->generated);}if(v->pattern_name){o->has_pattern_name=1;o->pattern_name=copy_string(*v->pattern_name);}o->rectangle_count=v->rectangles.size();if(o->rectangle_count)o->rectangles=static_cast<eccdb_via_rectangle_t*>(checked_calloc(o->rectangle_count,sizeof(*o->rectangles)));for(size_t i=0;i<o->rectangle_count;++i){auto&r=v->rectangles[i];o->rectangles[i].layer={r.layer.packed()};o->rectangles[i].rectangle={r.rectangle.ll_x,r.rectangle.ll_y,r.rectangle.ur_x,r.rectangle.ur_y};o->rectangles[i].mask=r.mask;}o->polygon_count=v->polygons.size();if(o->polygon_count)o->polygons=static_cast<eccdb_via_polygon_t*>(checked_calloc(o->polygon_count,sizeof(*o->polygons)));for(size_t i=0;i<o->polygon_count;++i){auto&p=v->polygons[i];o->polygons[i].layer={p.layer.packed()};o->polygons[i].mask=p.mask;o->polygons[i].point_count=p.points.size();if(o->polygons[i].point_count)o->polygons[i].points=static_cast<eccdb_point_t*>(checked_calloc(o->polygons[i].point_count,sizeof(*o->polygons[i].points)));for(size_t j=0;j<o->polygons[i].point_count;++j)o->polygons[i].points[j]={p.points[j].x,p.points[j].y};}});}
eccdb_status_t eccdb_design_via_update(eccdb_database_t*d,eccdb_via_id_t id,const eccdb_design_via_data_t*v){return invoke([&]{if(!d||!v||!v->name)throw std::invalid_argument("invalid design via arguments");check_array(v->rectangles,v->rectangle_count);check_array(v->polygons,v->polygon_count);eccdb::DesignViaData x{.name=v->name};if(v->has_generated)x.generated=to_generated(v->generated);if(v->has_pattern_name){if(!v->pattern_name)throw std::invalid_argument("pattern name is null");x.pattern_name=v->pattern_name;}for(size_t i=0;i<v->rectangle_count;++i){auto&r=v->rectangles[i];x.rectangles.push_back({eccdb::LayerId{r.layer.value},{r.rectangle.ll_x,r.rectangle.ll_y,r.rectangle.ur_x,r.rectangle.ur_y},r.mask});}for(size_t i=0;i<v->polygon_count;++i){check_array(v->polygons[i].points,v->polygons[i].point_count);eccdb::ViaPolygon p{.layer=eccdb::LayerId{v->polygons[i].layer.value},.mask=v->polygons[i].mask};for(size_t j=0;j<v->polygons[i].point_count;++j)p.points.push_back({v->polygons[i].points[j].x,v->polygons[i].points[j].y});x.polygons.push_back(std::move(p));}d->database.updateDesignVia(eccdb::ViaId{id.value},std::move(x));});}
eccdb_status_t eccdb_design_via_destroy(eccdb_database_t*d,eccdb_via_id_t id,uint8_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid design via arguments");*o=d->database.destroyDesignVia(eccdb::ViaId{id.value});});}
eccdb_status_t eccdb_design_via_ids(const eccdb_database_t*d,eccdb_via_id_list_t*o){return invoke([&]{if(!d||!o)throw std::invalid_argument("invalid design via list arguments");*o={};fill_ids<eccdb::ViaId,eccdb_via_id_list_t,eccdb_via_id_t>(*o,[&]{return d->database.designViaIds();});});}
void eccdb_design_via_data_free(eccdb_design_via_data_t*o){if(o){std::free(o->name);std::free(o->pattern_name);std::free(o->generated.cut_pattern);for(size_t i=0;o->polygons && i<o->polygon_count;++i)std::free(o->polygons[i].points);std::free(o->rectangles);std::free(o->polygons);*o={};}}
void eccdb_design_via_data_init(eccdb_design_via_data_t*o){if(o)*o={};}
void eccdb_via_id_list_free(eccdb_via_id_list_t*o){if(o){std::free(o->items);*o={};}}
}
