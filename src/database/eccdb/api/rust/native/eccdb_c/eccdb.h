#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental ABI. IDs are database-local; UINT64_MAX is invalid, not zero.
 * Initialize snapshots before use. Output buffers must be empty before queries.
 * Free returned allocations only with matching eccdb_*_free functions, including
 * after errors (partially populated outputs remain releasable).
 * Input pointers are borrowed for the call. Never free caller-owned input arrays.
 * Serialize access to each database. Error text is thread-local and valid until
 * the next status-returning call on that thread. */
#define ECCDB_INVALID_ID UINT64_MAX
/* These constants describe the uint8_t fields below; the struct fields have
 * fixed width regardless of a consumer's enum compiler options. */
enum { ECCDB_INPUT_LEF_DEF=0, ECCDB_INPUT_BINARY=1 };
enum { ECCDB_POLYGON_NATIVE=0, ECCDB_POLYGON_RECTANGULARIZED=1 };
enum { ECCDB_ORIENT_N=0, ECCDB_ORIENT_S, ECCDB_ORIENT_E, ECCDB_ORIENT_W,
       ECCDB_ORIENT_FN, ECCDB_ORIENT_FS, ECCDB_ORIENT_FE, ECCDB_ORIENT_FW };
enum { ECCDB_UNPLACED=0, ECCDB_PLACED, ECCDB_FIXED, ECCDB_COVER };
enum { ECCDB_USE_NONE=0, ECCDB_USE_SIGNAL, ECCDB_USE_ANALOG, ECCDB_USE_POWER,
       ECCDB_USE_GROUND, ECCDB_USE_CLOCK, ECCDB_USE_TIEOFF, ECCDB_USE_SCAN, ECCDB_USE_RESET };
enum { ECCDB_SOURCE_NONE=0, ECCDB_SOURCE_NETLIST, ECCDB_SOURCE_DIST,
       ECCDB_SOURCE_USER, ECCDB_SOURCE_TIMING, ECCDB_SOURCE_TEST };
enum { ECCDB_DIRECTION_NONE=0, ECCDB_DIRECTION_INPUT, ECCDB_DIRECTION_OUTPUT,
       ECCDB_DIRECTION_INOUT, ECCDB_DIRECTION_FEEDTHRU };
enum { ECCDB_PATTERN_NONE=0, ECCDB_PATTERN_BALANCED, ECCDB_PATTERN_STEINER,
       ECCDB_PATTERN_TRUNK, ECCDB_PATTERN_WIREDLOGIC };
enum { ECCDB_WIRE_ROUTED=0, ECCDB_WIRE_FIXED, ECCDB_WIRE_COVER,
       ECCDB_WIRE_SHIELD, ECCDB_WIRE_NOSHIELD };
enum { ECCDB_VIA_TECH=0, ECCDB_VIA_DESIGN=1 };
typedef struct { const char* technology; const char* library; const char* design; } eccdb_binary_files_t;
typedef struct {
  uint8_t input_kind; /* 0: LEF/DEF; 1: binary */
  const char* const* lef_files;
  size_t lef_count;
  const char* def_file;
  eccdb_binary_files_t binary;
  uint8_t polygon_mode; /* 0: native; 1: rectangularized */
  uint8_t has_binary_cache;
  eccdb_binary_files_t binary_cache;
} eccdb_config_t;
typedef struct eccdb_database eccdb_database_t;
typedef struct { uint64_t value; } eccdb_net_id_t;
typedef struct { uint64_t value; } eccdb_instance_id_t;
typedef struct { uint64_t value; } eccdb_instance_pin_id_t;
typedef struct { uint64_t value; } eccdb_io_pin_id_t;
typedef struct { uint64_t value; } eccdb_wire_id_t;
typedef struct { uint64_t value; } eccdb_via_id_t;
typedef struct { uint64_t value; } eccdb_layer_id_t;
typedef struct { uint64_t value; } eccdb_routing_layer_id_t;
typedef enum { ECCDB_STATUS_OK=0, ECCDB_STATUS_NOT_FOUND=1, ECCDB_STATUS_INVALID_ARGUMENT=2, ECCDB_STATUS_INVALID_STATE=3, ECCDB_STATUS_IO_ERROR=4, ECCDB_STATUS_INTERNAL_ERROR=5 } eccdb_status_t;
typedef struct { char* name; uint8_t signal_use; uint8_t source; int32_t weight; uint8_t has_weight; uint64_t tech_non_default_rule; uint64_t design_non_default_rule; } eccdb_net_data_t;
typedef struct { char* name; uint64_t master; int32_t origin_x; int32_t origin_y; uint8_t orientation; uint8_t placement_status; uint8_t source; } eccdb_instance_data_t;
typedef struct { eccdb_instance_id_t instance; uint64_t master_term; eccdb_net_id_t net; eccdb_net_id_t special_net; } eccdb_instance_pin_data_t;
typedef struct { char* name; uint8_t direction; uint8_t use; eccdb_net_id_t net; eccdb_net_id_t special_net; } eccdb_io_pin_data_t;
typedef struct { eccdb_routing_layer_id_t layer; int32_t spacing; uint8_t has_range; int32_t range_min; int32_t range_max; } eccdb_net_spacing_rule_t;
typedef struct { char* original; uint8_t has_original; uint8_t pattern; uint8_t has_pattern; double estimated_capacitance; uint8_t has_estimated_capacitance; double frequency; uint8_t has_frequency; int32_t xtalk; uint8_t has_xtalk; int32_t style; uint8_t has_style; int32_t voltage; uint8_t has_voltage; eccdb_net_spacing_rule_t* spacing_rules; size_t spacing_rule_count; } eccdb_net_options_t;
typedef struct { int32_t x; int32_t y; } eccdb_point_t;
typedef struct { int32_t ll_x; int32_t ll_y; int32_t ur_x; int32_t ur_y; } eccdb_rect_t;
typedef struct { eccdb_point_t position; int32_t extension; uint8_t has_extension; uint8_t virtual_point; } eccdb_wire_point_t;
typedef struct { eccdb_wire_point_t* points; size_t point_count; } eccdb_wire_point_list_t;
typedef struct { uint32_t top; uint32_t cut; uint32_t bottom; } eccdb_wire_mask_t;
typedef struct { uint32_t point_index; uint8_t definition_kind; uint64_t definition; uint8_t orientation; uint8_t has_mask; eccdb_wire_mask_t mask; uint8_t has_rows_columns; uint32_t rows; uint32_t columns; int32_t step_x; int32_t step_y; } eccdb_via_placement_t;
typedef struct { uint32_t point_index; eccdb_rect_t delta; } eccdb_wire_rectangle_t;
typedef struct { eccdb_net_id_t net; uint8_t status; char* shield_net; } eccdb_wire_metadata_t;
typedef struct { eccdb_routing_layer_id_t layer; uint8_t has_width; int32_t width; uint8_t has_mask; uint32_t mask; uint8_t taper; char* taper_rule; uint8_t has_taper_rule; char* shape; uint8_t has_shape; int32_t style; uint8_t has_style; eccdb_wire_point_t* points; size_t point_count; eccdb_via_placement_t* vias; size_t via_count; eccdb_wire_rectangle_t* rectangles; size_t rectangle_count; } eccdb_wire_path_t;
typedef struct { eccdb_wire_path_t* paths; size_t path_count; } eccdb_wire_routing_t;
typedef struct { char* source; char* statement; size_t occurrence_count; } eccdb_import_diagnostic_t;
typedef struct { eccdb_layer_id_t layer; eccdb_rect_t rectangle; uint32_t mask; } eccdb_via_rectangle_t;
typedef struct { eccdb_layer_id_t layer; eccdb_point_t* points; size_t point_count; uint32_t mask; } eccdb_via_polygon_t;
typedef struct { uint64_t via_rule; uint64_t bottom_layer; uint64_t cut_layer; uint64_t top_layer; int32_t cut_size_x; int32_t cut_size_y; int32_t cut_spacing_x; int32_t cut_spacing_y; int32_t bottom_enclosure_x; int32_t bottom_enclosure_y; int32_t top_enclosure_x; int32_t top_enclosure_y; uint8_t has_row_column; uint32_t rows; uint32_t columns; uint8_t has_origin; eccdb_point_t origin; uint8_t has_offsets; eccdb_point_t bottom_offset; eccdb_point_t top_offset; uint8_t has_cut_pattern; char* cut_pattern; } eccdb_generated_via_data_t;
typedef struct { char* name; char* pattern_name; uint8_t has_pattern_name; eccdb_via_rectangle_t* rectangles; size_t rectangle_count; eccdb_via_polygon_t* polygons; size_t polygon_count; uint8_t has_generated; eccdb_generated_via_data_t generated; } eccdb_design_via_data_t;
typedef struct { eccdb_net_id_t* items; size_t count; } eccdb_net_id_list_t;
typedef struct { eccdb_instance_id_t* items; size_t count; } eccdb_instance_id_list_t;
typedef struct { eccdb_instance_pin_id_t* items; size_t count; } eccdb_instance_pin_id_list_t;
typedef struct { eccdb_io_pin_id_t* items; size_t count; } eccdb_io_pin_id_list_t;
typedef struct { eccdb_wire_id_t* items; size_t count; } eccdb_wire_id_list_t;
typedef struct { eccdb_via_id_t* items; size_t count; } eccdb_via_id_list_t;
void eccdb_config_init(eccdb_config_t*);
eccdb_status_t eccdb_open(const eccdb_config_t*, eccdb_database_t**);
eccdb_status_t eccdb_supports_lef_def(uint8_t* out_supported);
eccdb_status_t eccdb_open_binary(const char*, const char*, const char*, eccdb_database_t**);
eccdb_status_t eccdb_open_lef_def(const char* lef, const char* def, eccdb_database_t**);
void eccdb_database_free(eccdb_database_t*);
const char* eccdb_last_error_message(void);
eccdb_status_t eccdb_write_binary(const eccdb_database_t*, const char*, const char*, const char*);
eccdb_status_t eccdb_write_def(const eccdb_database_t*, const char*);
eccdb_status_t eccdb_net_find(const eccdb_database_t*, const char*, eccdb_net_id_t*, uint8_t*);
eccdb_status_t eccdb_net_find_regular(const eccdb_database_t*, const char*, eccdb_net_id_t*, uint8_t*);
eccdb_status_t eccdb_net_find_special(const eccdb_database_t*, const char*, eccdb_net_id_t*, uint8_t*);
eccdb_status_t eccdb_net_contains(const eccdb_database_t*, eccdb_net_id_t, uint8_t*);
eccdb_status_t eccdb_net_create(eccdb_database_t*, const eccdb_net_data_t*, eccdb_net_id_t*);
eccdb_status_t eccdb_net_create_special(eccdb_database_t*, const eccdb_net_data_t*, eccdb_net_id_t*);
eccdb_status_t eccdb_net_is_special(const eccdb_database_t*, eccdb_net_id_t, uint8_t*);
eccdb_status_t eccdb_net_options_get(const eccdb_database_t*, eccdb_net_id_t, eccdb_net_options_t*, uint8_t*);
eccdb_status_t eccdb_net_options_set(eccdb_database_t*, eccdb_net_id_t, const eccdb_net_options_t*);
eccdb_status_t eccdb_net_get(const eccdb_database_t*, eccdb_net_id_t, eccdb_net_data_t*, uint8_t*);
eccdb_status_t eccdb_net_update(eccdb_database_t*, eccdb_net_id_t, const eccdb_net_data_t*);
eccdb_status_t eccdb_net_destroy(eccdb_database_t*, eccdb_net_id_t, uint8_t*);
eccdb_status_t eccdb_net_ids(const eccdb_database_t*, eccdb_net_id_list_t*);
eccdb_status_t eccdb_net_regular_ids(const eccdb_database_t*, eccdb_net_id_list_t*);
eccdb_status_t eccdb_net_special_ids(const eccdb_database_t*, eccdb_net_id_list_t*);
void eccdb_net_id_list_free(eccdb_net_id_list_t*);
void eccdb_net_data_free(eccdb_net_data_t*);
void eccdb_net_data_init(eccdb_net_data_t*);
void eccdb_net_options_free(eccdb_net_options_t*);
void eccdb_net_options_init(eccdb_net_options_t*);
eccdb_status_t eccdb_net_pin_ids(const eccdb_database_t*, eccdb_net_id_t, eccdb_instance_pin_id_list_t*, eccdb_io_pin_id_list_t*);
eccdb_status_t eccdb_net_wire_ids(const eccdb_database_t*, eccdb_net_id_t, eccdb_wire_id_list_t*);
eccdb_status_t eccdb_instance_find(const eccdb_database_t*, const char*, eccdb_instance_id_t*, uint8_t*);
eccdb_status_t eccdb_instance_contains(const eccdb_database_t*, eccdb_instance_id_t, uint8_t*);
eccdb_status_t eccdb_instance_create(eccdb_database_t*, const eccdb_instance_data_t*, eccdb_instance_id_t*);
eccdb_status_t eccdb_instance_get(const eccdb_database_t*, eccdb_instance_id_t, eccdb_instance_data_t*, uint8_t*);
eccdb_status_t eccdb_instance_update(eccdb_database_t*, eccdb_instance_id_t, const eccdb_instance_data_t*);
eccdb_status_t eccdb_instance_destroy(eccdb_database_t*, eccdb_instance_id_t, uint8_t*);
eccdb_status_t eccdb_instance_ids(const eccdb_database_t*, eccdb_instance_id_list_t*);
void eccdb_instance_id_list_free(eccdb_instance_id_list_t*);
void eccdb_instance_data_free(eccdb_instance_data_t*);
void eccdb_instance_data_init(eccdb_instance_data_t*);

eccdb_status_t eccdb_instance_pin_contains(const eccdb_database_t*, eccdb_instance_pin_id_t, uint8_t*);
eccdb_status_t eccdb_instance_pin_get(const eccdb_database_t*, eccdb_instance_pin_id_t, eccdb_instance_pin_data_t*, uint8_t*);
eccdb_status_t eccdb_instance_pin_ids(const eccdb_database_t*, eccdb_instance_id_t, eccdb_instance_pin_id_list_t*);
eccdb_status_t eccdb_instance_pin_find(const eccdb_database_t*, eccdb_instance_id_t, const char*, eccdb_instance_pin_id_t*, uint8_t*);
eccdb_status_t eccdb_instance_pin_connect(eccdb_database_t*, eccdb_instance_pin_id_t, eccdb_net_id_t);
eccdb_status_t eccdb_instance_pin_disconnect(eccdb_database_t*, eccdb_instance_pin_id_t);
eccdb_status_t eccdb_instance_pin_disconnect_from_net(eccdb_database_t*, eccdb_instance_pin_id_t, eccdb_net_id_t);
void eccdb_instance_pin_id_list_free(eccdb_instance_pin_id_list_t*);

eccdb_status_t eccdb_io_pin_create(eccdb_database_t*, const eccdb_io_pin_data_t*, eccdb_io_pin_id_t*);
eccdb_status_t eccdb_io_pin_contains(const eccdb_database_t*, eccdb_io_pin_id_t, uint8_t*);
eccdb_status_t eccdb_io_pin_find(const eccdb_database_t*, const char*, eccdb_io_pin_id_t*, uint8_t*);
eccdb_status_t eccdb_io_pin_get(const eccdb_database_t*, eccdb_io_pin_id_t, eccdb_io_pin_data_t*, uint8_t*);
eccdb_status_t eccdb_io_pin_update(eccdb_database_t*, eccdb_io_pin_id_t, const eccdb_io_pin_data_t*);
eccdb_status_t eccdb_io_pin_destroy(eccdb_database_t*, eccdb_io_pin_id_t, uint8_t*);
eccdb_status_t eccdb_io_pin_ids(const eccdb_database_t*, eccdb_io_pin_id_list_t*);
eccdb_status_t eccdb_io_pin_connect(eccdb_database_t*, eccdb_io_pin_id_t, eccdb_net_id_t);
eccdb_status_t eccdb_io_pin_disconnect(eccdb_database_t*, eccdb_io_pin_id_t);
eccdb_status_t eccdb_io_pin_disconnect_from_net(eccdb_database_t*, eccdb_io_pin_id_t, eccdb_net_id_t);
void eccdb_io_pin_data_free(eccdb_io_pin_data_t*);
void eccdb_io_pin_data_init(eccdb_io_pin_data_t*);
void eccdb_io_pin_id_list_free(eccdb_io_pin_id_list_t*);

eccdb_status_t eccdb_wire_contains(const eccdb_database_t*, eccdb_wire_id_t, uint8_t*);
eccdb_status_t eccdb_wire_ids(const eccdb_database_t*, eccdb_wire_id_list_t*);
eccdb_status_t eccdb_wire_ids_for_net(const eccdb_database_t*, eccdb_net_id_t, eccdb_wire_id_list_t*);
eccdb_status_t eccdb_wire_metadata_get(const eccdb_database_t*, eccdb_wire_id_t, eccdb_wire_metadata_t*, uint8_t*);
eccdb_status_t eccdb_wire_create(eccdb_database_t*, const eccdb_wire_metadata_t*, const eccdb_wire_routing_t*, eccdb_wire_id_t*);
eccdb_status_t eccdb_wire_routing_get(const eccdb_database_t*, eccdb_wire_id_t, eccdb_wire_routing_t*, uint8_t*);
eccdb_status_t eccdb_wire_update(eccdb_database_t*, eccdb_wire_id_t, const eccdb_wire_metadata_t*, const eccdb_wire_routing_t*);
eccdb_status_t eccdb_wire_destroy(eccdb_database_t*, eccdb_wire_id_t, uint8_t*);
void eccdb_wire_metadata_free(eccdb_wire_metadata_t*);
void eccdb_wire_metadata_init(eccdb_wire_metadata_t*);
void eccdb_wire_routing_free(eccdb_wire_routing_t*);
void eccdb_wire_routing_init(eccdb_wire_routing_t*);
void eccdb_wire_id_list_free(eccdb_wire_id_list_t*);

eccdb_status_t eccdb_design_via_create(eccdb_database_t*, const eccdb_design_via_data_t*, eccdb_via_id_t*);
eccdb_status_t eccdb_design_via_contains(const eccdb_database_t*, eccdb_via_id_t, uint8_t*);
eccdb_status_t eccdb_design_via_find(const eccdb_database_t*, const char*, eccdb_via_id_t*, uint8_t*);
eccdb_status_t eccdb_design_via_get(const eccdb_database_t*, eccdb_via_id_t, eccdb_design_via_data_t*, uint8_t*);
eccdb_status_t eccdb_design_via_update(eccdb_database_t*, eccdb_via_id_t, const eccdb_design_via_data_t*);
eccdb_status_t eccdb_design_via_destroy(eccdb_database_t*, eccdb_via_id_t, uint8_t*);
eccdb_status_t eccdb_design_via_ids(const eccdb_database_t*, eccdb_via_id_list_t*);
void eccdb_design_via_data_free(eccdb_design_via_data_t*);
void eccdb_design_via_data_init(eccdb_design_via_data_t*);
void eccdb_via_id_list_free(eccdb_via_id_list_t*);

eccdb_status_t eccdb_diagnostics(const eccdb_database_t*, eccdb_import_diagnostic_t**, size_t*);
void eccdb_diagnostics_free(eccdb_import_diagnostic_t*, size_t);
#ifdef __cplusplus
}
#endif
