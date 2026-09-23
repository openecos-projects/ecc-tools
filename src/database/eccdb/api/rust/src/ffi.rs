//! Private ABI declarations; layout is checked against the packaged C header.
#![allow(non_camel_case_types, dead_code)]
#[repr(C)]
pub struct eccdb_database_t {
    _opaque: [u8; 0],
}
pub type eccdb_status_t = std::ffi::c_int;
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_binary_files_t {
    pub technology: *const std::ffi::c_char,
    pub library: *const std::ffi::c_char,
    pub design: *const std::ffi::c_char,
}
impl Default for eccdb_binary_files_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_config_t {
    pub input_kind: u8,
    pub lef_files: *const *const std::ffi::c_char,
    pub lef_count: usize,
    pub def_file: *const std::ffi::c_char,
    pub binary: eccdb_binary_files_t,
    pub polygon_mode: u8,
    pub has_binary_cache: u8,
    pub binary_cache: eccdb_binary_files_t,
}
impl Default for eccdb_config_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_net_id_t {
    pub value: u64,
}
impl Default for eccdb_net_id_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_instance_id_t {
    pub value: u64,
}
impl Default for eccdb_instance_id_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_instance_pin_id_t {
    pub value: u64,
}
impl Default for eccdb_instance_pin_id_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_io_pin_id_t {
    pub value: u64,
}
impl Default for eccdb_io_pin_id_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_wire_id_t {
    pub value: u64,
}
impl Default for eccdb_wire_id_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_via_id_t {
    pub value: u64,
}
impl Default for eccdb_via_id_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_layer_id_t {
    pub value: u64,
}
impl Default for eccdb_layer_id_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_routing_layer_id_t {
    pub value: u64,
}
impl Default for eccdb_routing_layer_id_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_net_data_t {
    pub name: *mut std::ffi::c_char,
    pub signal_use: u8,
    pub source: u8,
    pub weight: i32,
    pub has_weight: u8,
    pub tech_non_default_rule: u64,
    pub design_non_default_rule: u64,
}
impl Default for eccdb_net_data_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_instance_data_t {
    pub name: *mut std::ffi::c_char,
    pub master: u64,
    pub origin_x: i32,
    pub origin_y: i32,
    pub orientation: u8,
    pub placement_status: u8,
    pub source: u8,
}
impl Default for eccdb_instance_data_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_instance_pin_data_t {
    pub instance: eccdb_instance_id_t,
    pub master_term: u64,
    pub net: eccdb_net_id_t,
    pub special_net: eccdb_net_id_t,
}
impl Default for eccdb_instance_pin_data_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_io_pin_data_t {
    pub name: *mut std::ffi::c_char,
    pub direction: u8,
    pub r#use: u8,
    pub net: eccdb_net_id_t,
    pub special_net: eccdb_net_id_t,
}
impl Default for eccdb_io_pin_data_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_net_spacing_rule_t {
    pub layer: eccdb_routing_layer_id_t,
    pub spacing: i32,
    pub has_range: u8,
    pub range_min: i32,
    pub range_max: i32,
}
impl Default for eccdb_net_spacing_rule_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_net_options_t {
    pub original: *mut std::ffi::c_char,
    pub has_original: u8,
    pub pattern: u8,
    pub has_pattern: u8,
    pub estimated_capacitance: f64,
    pub has_estimated_capacitance: u8,
    pub frequency: f64,
    pub has_frequency: u8,
    pub xtalk: i32,
    pub has_xtalk: u8,
    pub style: i32,
    pub has_style: u8,
    pub voltage: i32,
    pub has_voltage: u8,
    pub spacing_rules: *mut eccdb_net_spacing_rule_t,
    pub spacing_rule_count: usize,
}
impl Default for eccdb_net_options_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_point_t {
    pub x: i32,
    pub y: i32,
}
impl Default for eccdb_point_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_rect_t {
    pub ll_x: i32,
    pub ll_y: i32,
    pub ur_x: i32,
    pub ur_y: i32,
}
impl Default for eccdb_rect_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_wire_point_t {
    pub position: eccdb_point_t,
    pub extension: i32,
    pub has_extension: u8,
    pub virtual_point: u8,
}
impl Default for eccdb_wire_point_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_wire_point_list_t {
    pub points: *mut eccdb_wire_point_t,
    pub point_count: usize,
}
impl Default for eccdb_wire_point_list_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_wire_mask_t {
    pub top: u32,
    pub cut: u32,
    pub bottom: u32,
}
impl Default for eccdb_wire_mask_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_via_placement_t {
    pub point_index: u32,
    pub definition_kind: u8,
    pub definition: u64,
    pub orientation: u8,
    pub has_mask: u8,
    pub mask: eccdb_wire_mask_t,
    pub has_rows_columns: u8,
    pub rows: u32,
    pub columns: u32,
    pub step_x: i32,
    pub step_y: i32,
}
impl Default for eccdb_via_placement_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_wire_rectangle_t {
    pub point_index: u32,
    pub delta: eccdb_rect_t,
}
impl Default for eccdb_wire_rectangle_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_wire_metadata_t {
    pub net: eccdb_net_id_t,
    pub status: u8,
    pub shield_net: *mut std::ffi::c_char,
}
impl Default for eccdb_wire_metadata_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_wire_path_t {
    pub layer: eccdb_routing_layer_id_t,
    pub has_width: u8,
    pub width: i32,
    pub has_mask: u8,
    pub mask: u32,
    pub taper: u8,
    pub taper_rule: *mut std::ffi::c_char,
    pub has_taper_rule: u8,
    pub shape: *mut std::ffi::c_char,
    pub has_shape: u8,
    pub style: i32,
    pub has_style: u8,
    pub points: *mut eccdb_wire_point_t,
    pub point_count: usize,
    pub vias: *mut eccdb_via_placement_t,
    pub via_count: usize,
    pub rectangles: *mut eccdb_wire_rectangle_t,
    pub rectangle_count: usize,
}
impl Default for eccdb_wire_path_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_wire_routing_t {
    pub paths: *mut eccdb_wire_path_t,
    pub path_count: usize,
}
impl Default for eccdb_wire_routing_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_import_diagnostic_t {
    pub source: *mut std::ffi::c_char,
    pub statement: *mut std::ffi::c_char,
    pub occurrence_count: usize,
}
impl Default for eccdb_import_diagnostic_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_via_rectangle_t {
    pub layer: eccdb_layer_id_t,
    pub rectangle: eccdb_rect_t,
    pub mask: u32,
}
impl Default for eccdb_via_rectangle_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_via_polygon_t {
    pub layer: eccdb_layer_id_t,
    pub points: *mut eccdb_point_t,
    pub point_count: usize,
    pub mask: u32,
}
impl Default for eccdb_via_polygon_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_generated_via_data_t {
    pub via_rule: u64,
    pub bottom_layer: u64,
    pub cut_layer: u64,
    pub top_layer: u64,
    pub cut_size_x: i32,
    pub cut_size_y: i32,
    pub cut_spacing_x: i32,
    pub cut_spacing_y: i32,
    pub bottom_enclosure_x: i32,
    pub bottom_enclosure_y: i32,
    pub top_enclosure_x: i32,
    pub top_enclosure_y: i32,
    pub has_row_column: u8,
    pub rows: u32,
    pub columns: u32,
    pub has_origin: u8,
    pub origin: eccdb_point_t,
    pub has_offsets: u8,
    pub bottom_offset: eccdb_point_t,
    pub top_offset: eccdb_point_t,
    pub has_cut_pattern: u8,
    pub cut_pattern: *mut std::ffi::c_char,
}
impl Default for eccdb_generated_via_data_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_design_via_data_t {
    pub name: *mut std::ffi::c_char,
    pub pattern_name: *mut std::ffi::c_char,
    pub has_pattern_name: u8,
    pub rectangles: *mut eccdb_via_rectangle_t,
    pub rectangle_count: usize,
    pub polygons: *mut eccdb_via_polygon_t,
    pub polygon_count: usize,
    pub has_generated: u8,
    pub generated: eccdb_generated_via_data_t,
}
impl Default for eccdb_design_via_data_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_net_id_list_t {
    pub items: *mut eccdb_net_id_t,
    pub count: usize,
}
impl Default for eccdb_net_id_list_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_instance_id_list_t {
    pub items: *mut eccdb_instance_id_t,
    pub count: usize,
}
impl Default for eccdb_instance_id_list_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_instance_pin_id_list_t {
    pub items: *mut eccdb_instance_pin_id_t,
    pub count: usize,
}
impl Default for eccdb_instance_pin_id_list_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_io_pin_id_list_t {
    pub items: *mut eccdb_io_pin_id_t,
    pub count: usize,
}
impl Default for eccdb_io_pin_id_list_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_wire_id_list_t {
    pub items: *mut eccdb_wire_id_t,
    pub count: usize,
}
impl Default for eccdb_wire_id_list_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct eccdb_via_id_list_t {
    pub items: *mut eccdb_via_id_t,
    pub count: usize,
}
impl Default for eccdb_via_id_list_t {
    fn default() -> Self {
        // All raw fields permit zero bits; semantic ID defaults are set by the safe layer.
        unsafe { std::mem::zeroed() }
    }
}
unsafe extern "C" {
    pub fn eccdb_config_init(arg0: *mut eccdb_config_t) -> ();
    pub fn eccdb_open(
        arg0: *const eccdb_config_t,
        arg1: *mut *mut eccdb_database_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_supports_lef_def(arg0: *mut u8) -> std::ffi::c_int;
    pub fn eccdb_open_binary(
        arg0: *const std::ffi::c_char,
        arg1: *const std::ffi::c_char,
        arg2: *const std::ffi::c_char,
        arg3: *mut *mut eccdb_database_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_open_lef_def(
        arg0: *const std::ffi::c_char,
        arg1: *const std::ffi::c_char,
        arg2: *mut *mut eccdb_database_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_database_free(arg0: *mut eccdb_database_t) -> ();
    pub fn eccdb_last_error_message() -> *const std::ffi::c_char;
    pub fn eccdb_write_binary(
        arg0: *const eccdb_database_t,
        arg1: *const std::ffi::c_char,
        arg2: *const std::ffi::c_char,
        arg3: *const std::ffi::c_char,
    ) -> std::ffi::c_int;
    pub fn eccdb_write_def(
        arg0: *const eccdb_database_t,
        arg1: *const std::ffi::c_char,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_find(
        arg0: *const eccdb_database_t,
        arg1: *const std::ffi::c_char,
        arg2: *mut eccdb_net_id_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_find_regular(
        arg0: *const eccdb_database_t,
        arg1: *const std::ffi::c_char,
        arg2: *mut eccdb_net_id_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_find_special(
        arg0: *const eccdb_database_t,
        arg1: *const std::ffi::c_char,
        arg2: *mut eccdb_net_id_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_contains(
        arg0: *const eccdb_database_t,
        arg1: eccdb_net_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_create(
        arg0: *mut eccdb_database_t,
        arg1: *const eccdb_net_data_t,
        arg2: *mut eccdb_net_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_create_special(
        arg0: *mut eccdb_database_t,
        arg1: *const eccdb_net_data_t,
        arg2: *mut eccdb_net_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_is_special(
        arg0: *const eccdb_database_t,
        arg1: eccdb_net_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_options_get(
        arg0: *const eccdb_database_t,
        arg1: eccdb_net_id_t,
        arg2: *mut eccdb_net_options_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_options_set(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_net_id_t,
        arg2: *const eccdb_net_options_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_get(
        arg0: *const eccdb_database_t,
        arg1: eccdb_net_id_t,
        arg2: *mut eccdb_net_data_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_update(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_net_id_t,
        arg2: *const eccdb_net_data_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_destroy(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_net_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_ids(
        arg0: *const eccdb_database_t,
        arg1: *mut eccdb_net_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_regular_ids(
        arg0: *const eccdb_database_t,
        arg1: *mut eccdb_net_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_special_ids(
        arg0: *const eccdb_database_t,
        arg1: *mut eccdb_net_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_id_list_free(arg0: *mut eccdb_net_id_list_t) -> ();
    pub fn eccdb_net_data_free(arg0: *mut eccdb_net_data_t) -> ();
    pub fn eccdb_net_data_init(arg0: *mut eccdb_net_data_t) -> ();
    pub fn eccdb_net_options_free(arg0: *mut eccdb_net_options_t) -> ();
    pub fn eccdb_net_options_init(arg0: *mut eccdb_net_options_t) -> ();
    pub fn eccdb_net_pin_ids(
        arg0: *const eccdb_database_t,
        arg1: eccdb_net_id_t,
        arg2: *mut eccdb_instance_pin_id_list_t,
        arg3: *mut eccdb_io_pin_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_net_wire_ids(
        arg0: *const eccdb_database_t,
        arg1: eccdb_net_id_t,
        arg2: *mut eccdb_wire_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_find(
        arg0: *const eccdb_database_t,
        arg1: *const std::ffi::c_char,
        arg2: *mut eccdb_instance_id_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_contains(
        arg0: *const eccdb_database_t,
        arg1: eccdb_instance_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_create(
        arg0: *mut eccdb_database_t,
        arg1: *const eccdb_instance_data_t,
        arg2: *mut eccdb_instance_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_get(
        arg0: *const eccdb_database_t,
        arg1: eccdb_instance_id_t,
        arg2: *mut eccdb_instance_data_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_update(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_instance_id_t,
        arg2: *const eccdb_instance_data_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_destroy(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_instance_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_ids(
        arg0: *const eccdb_database_t,
        arg1: *mut eccdb_instance_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_id_list_free(arg0: *mut eccdb_instance_id_list_t) -> ();
    pub fn eccdb_instance_data_free(arg0: *mut eccdb_instance_data_t) -> ();
    pub fn eccdb_instance_data_init(arg0: *mut eccdb_instance_data_t) -> ();
    pub fn eccdb_instance_pin_contains(
        arg0: *const eccdb_database_t,
        arg1: eccdb_instance_pin_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_pin_get(
        arg0: *const eccdb_database_t,
        arg1: eccdb_instance_pin_id_t,
        arg2: *mut eccdb_instance_pin_data_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_pin_ids(
        arg0: *const eccdb_database_t,
        arg1: eccdb_instance_id_t,
        arg2: *mut eccdb_instance_pin_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_pin_find(
        arg0: *const eccdb_database_t,
        arg1: eccdb_instance_id_t,
        arg2: *const std::ffi::c_char,
        arg3: *mut eccdb_instance_pin_id_t,
        arg4: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_pin_connect(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_instance_pin_id_t,
        arg2: eccdb_net_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_pin_disconnect(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_instance_pin_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_pin_disconnect_from_net(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_instance_pin_id_t,
        arg2: eccdb_net_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_instance_pin_id_list_free(arg0: *mut eccdb_instance_pin_id_list_t) -> ();
    pub fn eccdb_io_pin_create(
        arg0: *mut eccdb_database_t,
        arg1: *const eccdb_io_pin_data_t,
        arg2: *mut eccdb_io_pin_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_io_pin_contains(
        arg0: *const eccdb_database_t,
        arg1: eccdb_io_pin_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_io_pin_find(
        arg0: *const eccdb_database_t,
        arg1: *const std::ffi::c_char,
        arg2: *mut eccdb_io_pin_id_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_io_pin_get(
        arg0: *const eccdb_database_t,
        arg1: eccdb_io_pin_id_t,
        arg2: *mut eccdb_io_pin_data_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_io_pin_update(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_io_pin_id_t,
        arg2: *const eccdb_io_pin_data_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_io_pin_destroy(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_io_pin_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_io_pin_ids(
        arg0: *const eccdb_database_t,
        arg1: *mut eccdb_io_pin_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_io_pin_connect(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_io_pin_id_t,
        arg2: eccdb_net_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_io_pin_disconnect(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_io_pin_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_io_pin_disconnect_from_net(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_io_pin_id_t,
        arg2: eccdb_net_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_io_pin_data_free(arg0: *mut eccdb_io_pin_data_t) -> ();
    pub fn eccdb_io_pin_data_init(arg0: *mut eccdb_io_pin_data_t) -> ();
    pub fn eccdb_io_pin_id_list_free(arg0: *mut eccdb_io_pin_id_list_t) -> ();
    pub fn eccdb_wire_contains(
        arg0: *const eccdb_database_t,
        arg1: eccdb_wire_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_wire_ids(
        arg0: *const eccdb_database_t,
        arg1: *mut eccdb_wire_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_wire_ids_for_net(
        arg0: *const eccdb_database_t,
        arg1: eccdb_net_id_t,
        arg2: *mut eccdb_wire_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_wire_metadata_get(
        arg0: *const eccdb_database_t,
        arg1: eccdb_wire_id_t,
        arg2: *mut eccdb_wire_metadata_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_wire_create(
        arg0: *mut eccdb_database_t,
        arg1: *const eccdb_wire_metadata_t,
        arg2: *const eccdb_wire_routing_t,
        arg3: *mut eccdb_wire_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_wire_routing_get(
        arg0: *const eccdb_database_t,
        arg1: eccdb_wire_id_t,
        arg2: *mut eccdb_wire_routing_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_wire_update(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_wire_id_t,
        arg2: *const eccdb_wire_metadata_t,
        arg3: *const eccdb_wire_routing_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_wire_destroy(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_wire_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_wire_metadata_free(arg0: *mut eccdb_wire_metadata_t) -> ();
    pub fn eccdb_wire_metadata_init(arg0: *mut eccdb_wire_metadata_t) -> ();
    pub fn eccdb_wire_routing_free(arg0: *mut eccdb_wire_routing_t) -> ();
    pub fn eccdb_wire_routing_init(arg0: *mut eccdb_wire_routing_t) -> ();
    pub fn eccdb_wire_id_list_free(arg0: *mut eccdb_wire_id_list_t) -> ();
    pub fn eccdb_design_via_create(
        arg0: *mut eccdb_database_t,
        arg1: *const eccdb_design_via_data_t,
        arg2: *mut eccdb_via_id_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_design_via_contains(
        arg0: *const eccdb_database_t,
        arg1: eccdb_via_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_design_via_find(
        arg0: *const eccdb_database_t,
        arg1: *const std::ffi::c_char,
        arg2: *mut eccdb_via_id_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_design_via_get(
        arg0: *const eccdb_database_t,
        arg1: eccdb_via_id_t,
        arg2: *mut eccdb_design_via_data_t,
        arg3: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_design_via_update(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_via_id_t,
        arg2: *const eccdb_design_via_data_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_design_via_destroy(
        arg0: *mut eccdb_database_t,
        arg1: eccdb_via_id_t,
        arg2: *mut u8,
    ) -> std::ffi::c_int;
    pub fn eccdb_design_via_ids(
        arg0: *const eccdb_database_t,
        arg1: *mut eccdb_via_id_list_t,
    ) -> std::ffi::c_int;
    pub fn eccdb_design_via_data_free(arg0: *mut eccdb_design_via_data_t) -> ();
    pub fn eccdb_design_via_data_init(arg0: *mut eccdb_design_via_data_t) -> ();
    pub fn eccdb_via_id_list_free(arg0: *mut eccdb_via_id_list_t) -> ();
    pub fn eccdb_diagnostics(
        arg0: *const eccdb_database_t,
        arg1: *mut *mut eccdb_import_diagnostic_t,
        arg2: *mut usize,
    ) -> std::ffi::c_int;
    pub fn eccdb_diagnostics_free(arg0: *mut eccdb_import_diagnostic_t, arg1: usize) -> ();
}
#[cfg(test)]
mod abi_layout {
    use super::*;
    unsafe extern "C" {
        fn eccdb_rust_layout(index: usize) -> usize;
    }
    #[test]
    fn matches_c_header() {
        assert_eq!(
            std::mem::size_of::<eccdb_binary_files_t>(),
            unsafe { eccdb_rust_layout(0) },
            "sizeof(eccdb_binary_files_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_binary_files_t>(),
            unsafe { eccdb_rust_layout(1) },
            "_Alignof(eccdb_binary_files_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_binary_files_t, technology),
            unsafe { eccdb_rust_layout(2) },
            "offsetof(eccdb_binary_files_t,technology)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_binary_files_t, library),
            unsafe { eccdb_rust_layout(3) },
            "offsetof(eccdb_binary_files_t,library)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_binary_files_t, design),
            unsafe { eccdb_rust_layout(4) },
            "offsetof(eccdb_binary_files_t,design)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_config_t>(),
            unsafe { eccdb_rust_layout(5) },
            "sizeof(eccdb_config_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_config_t>(),
            unsafe { eccdb_rust_layout(6) },
            "_Alignof(eccdb_config_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_config_t, input_kind),
            unsafe { eccdb_rust_layout(7) },
            "offsetof(eccdb_config_t,input_kind)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_config_t, lef_files),
            unsafe { eccdb_rust_layout(8) },
            "offsetof(eccdb_config_t,lef_files)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_config_t, lef_count),
            unsafe { eccdb_rust_layout(9) },
            "offsetof(eccdb_config_t,lef_count)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_config_t, def_file),
            unsafe { eccdb_rust_layout(10) },
            "offsetof(eccdb_config_t,def_file)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_config_t, binary),
            unsafe { eccdb_rust_layout(11) },
            "offsetof(eccdb_config_t,binary)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_config_t, polygon_mode),
            unsafe { eccdb_rust_layout(12) },
            "offsetof(eccdb_config_t,polygon_mode)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_config_t, has_binary_cache),
            unsafe { eccdb_rust_layout(13) },
            "offsetof(eccdb_config_t,has_binary_cache)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_config_t, binary_cache),
            unsafe { eccdb_rust_layout(14) },
            "offsetof(eccdb_config_t,binary_cache)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_net_id_t>(),
            unsafe { eccdb_rust_layout(15) },
            "sizeof(eccdb_net_id_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_net_id_t>(),
            unsafe { eccdb_rust_layout(16) },
            "_Alignof(eccdb_net_id_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_id_t, value),
            unsafe { eccdb_rust_layout(17) },
            "offsetof(eccdb_net_id_t,value)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_instance_id_t>(),
            unsafe { eccdb_rust_layout(18) },
            "sizeof(eccdb_instance_id_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_instance_id_t>(),
            unsafe { eccdb_rust_layout(19) },
            "_Alignof(eccdb_instance_id_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_id_t, value),
            unsafe { eccdb_rust_layout(20) },
            "offsetof(eccdb_instance_id_t,value)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_instance_pin_id_t>(),
            unsafe { eccdb_rust_layout(21) },
            "sizeof(eccdb_instance_pin_id_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_instance_pin_id_t>(),
            unsafe { eccdb_rust_layout(22) },
            "_Alignof(eccdb_instance_pin_id_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_pin_id_t, value),
            unsafe { eccdb_rust_layout(23) },
            "offsetof(eccdb_instance_pin_id_t,value)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_io_pin_id_t>(),
            unsafe { eccdb_rust_layout(24) },
            "sizeof(eccdb_io_pin_id_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_io_pin_id_t>(),
            unsafe { eccdb_rust_layout(25) },
            "_Alignof(eccdb_io_pin_id_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_io_pin_id_t, value),
            unsafe { eccdb_rust_layout(26) },
            "offsetof(eccdb_io_pin_id_t,value)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_wire_id_t>(),
            unsafe { eccdb_rust_layout(27) },
            "sizeof(eccdb_wire_id_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_wire_id_t>(),
            unsafe { eccdb_rust_layout(28) },
            "_Alignof(eccdb_wire_id_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_id_t, value),
            unsafe { eccdb_rust_layout(29) },
            "offsetof(eccdb_wire_id_t,value)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_via_id_t>(),
            unsafe { eccdb_rust_layout(30) },
            "sizeof(eccdb_via_id_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_via_id_t>(),
            unsafe { eccdb_rust_layout(31) },
            "_Alignof(eccdb_via_id_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_id_t, value),
            unsafe { eccdb_rust_layout(32) },
            "offsetof(eccdb_via_id_t,value)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_layer_id_t>(),
            unsafe { eccdb_rust_layout(33) },
            "sizeof(eccdb_layer_id_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_layer_id_t>(),
            unsafe { eccdb_rust_layout(34) },
            "_Alignof(eccdb_layer_id_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_layer_id_t, value),
            unsafe { eccdb_rust_layout(35) },
            "offsetof(eccdb_layer_id_t,value)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_routing_layer_id_t>(),
            unsafe { eccdb_rust_layout(36) },
            "sizeof(eccdb_routing_layer_id_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_routing_layer_id_t>(),
            unsafe { eccdb_rust_layout(37) },
            "_Alignof(eccdb_routing_layer_id_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_routing_layer_id_t, value),
            unsafe { eccdb_rust_layout(38) },
            "offsetof(eccdb_routing_layer_id_t,value)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_net_data_t>(),
            unsafe { eccdb_rust_layout(39) },
            "sizeof(eccdb_net_data_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_net_data_t>(),
            unsafe { eccdb_rust_layout(40) },
            "_Alignof(eccdb_net_data_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_data_t, name),
            unsafe { eccdb_rust_layout(41) },
            "offsetof(eccdb_net_data_t,name)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_data_t, signal_use),
            unsafe { eccdb_rust_layout(42) },
            "offsetof(eccdb_net_data_t,signal_use)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_data_t, source),
            unsafe { eccdb_rust_layout(43) },
            "offsetof(eccdb_net_data_t,source)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_data_t, weight),
            unsafe { eccdb_rust_layout(44) },
            "offsetof(eccdb_net_data_t,weight)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_data_t, has_weight),
            unsafe { eccdb_rust_layout(45) },
            "offsetof(eccdb_net_data_t,has_weight)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_data_t, tech_non_default_rule),
            unsafe { eccdb_rust_layout(46) },
            "offsetof(eccdb_net_data_t,tech_non_default_rule)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_data_t, design_non_default_rule),
            unsafe { eccdb_rust_layout(47) },
            "offsetof(eccdb_net_data_t,design_non_default_rule)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_instance_data_t>(),
            unsafe { eccdb_rust_layout(48) },
            "sizeof(eccdb_instance_data_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_instance_data_t>(),
            unsafe { eccdb_rust_layout(49) },
            "_Alignof(eccdb_instance_data_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_data_t, name),
            unsafe { eccdb_rust_layout(50) },
            "offsetof(eccdb_instance_data_t,name)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_data_t, master),
            unsafe { eccdb_rust_layout(51) },
            "offsetof(eccdb_instance_data_t,master)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_data_t, origin_x),
            unsafe { eccdb_rust_layout(52) },
            "offsetof(eccdb_instance_data_t,origin_x)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_data_t, origin_y),
            unsafe { eccdb_rust_layout(53) },
            "offsetof(eccdb_instance_data_t,origin_y)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_data_t, orientation),
            unsafe { eccdb_rust_layout(54) },
            "offsetof(eccdb_instance_data_t,orientation)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_data_t, placement_status),
            unsafe { eccdb_rust_layout(55) },
            "offsetof(eccdb_instance_data_t,placement_status)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_data_t, source),
            unsafe { eccdb_rust_layout(56) },
            "offsetof(eccdb_instance_data_t,source)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_instance_pin_data_t>(),
            unsafe { eccdb_rust_layout(57) },
            "sizeof(eccdb_instance_pin_data_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_instance_pin_data_t>(),
            unsafe { eccdb_rust_layout(58) },
            "_Alignof(eccdb_instance_pin_data_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_pin_data_t, instance),
            unsafe { eccdb_rust_layout(59) },
            "offsetof(eccdb_instance_pin_data_t,instance)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_pin_data_t, master_term),
            unsafe { eccdb_rust_layout(60) },
            "offsetof(eccdb_instance_pin_data_t,master_term)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_pin_data_t, net),
            unsafe { eccdb_rust_layout(61) },
            "offsetof(eccdb_instance_pin_data_t,net)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_pin_data_t, special_net),
            unsafe { eccdb_rust_layout(62) },
            "offsetof(eccdb_instance_pin_data_t,special_net)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_io_pin_data_t>(),
            unsafe { eccdb_rust_layout(63) },
            "sizeof(eccdb_io_pin_data_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_io_pin_data_t>(),
            unsafe { eccdb_rust_layout(64) },
            "_Alignof(eccdb_io_pin_data_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_io_pin_data_t, name),
            unsafe { eccdb_rust_layout(65) },
            "offsetof(eccdb_io_pin_data_t,name)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_io_pin_data_t, direction),
            unsafe { eccdb_rust_layout(66) },
            "offsetof(eccdb_io_pin_data_t,direction)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_io_pin_data_t, r#use),
            unsafe { eccdb_rust_layout(67) },
            "offsetof(eccdb_io_pin_data_t,use)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_io_pin_data_t, net),
            unsafe { eccdb_rust_layout(68) },
            "offsetof(eccdb_io_pin_data_t,net)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_io_pin_data_t, special_net),
            unsafe { eccdb_rust_layout(69) },
            "offsetof(eccdb_io_pin_data_t,special_net)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_net_spacing_rule_t>(),
            unsafe { eccdb_rust_layout(70) },
            "sizeof(eccdb_net_spacing_rule_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_net_spacing_rule_t>(),
            unsafe { eccdb_rust_layout(71) },
            "_Alignof(eccdb_net_spacing_rule_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_spacing_rule_t, layer),
            unsafe { eccdb_rust_layout(72) },
            "offsetof(eccdb_net_spacing_rule_t,layer)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_spacing_rule_t, spacing),
            unsafe { eccdb_rust_layout(73) },
            "offsetof(eccdb_net_spacing_rule_t,spacing)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_spacing_rule_t, has_range),
            unsafe { eccdb_rust_layout(74) },
            "offsetof(eccdb_net_spacing_rule_t,has_range)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_spacing_rule_t, range_min),
            unsafe { eccdb_rust_layout(75) },
            "offsetof(eccdb_net_spacing_rule_t,range_min)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_spacing_rule_t, range_max),
            unsafe { eccdb_rust_layout(76) },
            "offsetof(eccdb_net_spacing_rule_t,range_max)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_net_options_t>(),
            unsafe { eccdb_rust_layout(77) },
            "sizeof(eccdb_net_options_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_net_options_t>(),
            unsafe { eccdb_rust_layout(78) },
            "_Alignof(eccdb_net_options_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, original),
            unsafe { eccdb_rust_layout(79) },
            "offsetof(eccdb_net_options_t,original)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, has_original),
            unsafe { eccdb_rust_layout(80) },
            "offsetof(eccdb_net_options_t,has_original)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, pattern),
            unsafe { eccdb_rust_layout(81) },
            "offsetof(eccdb_net_options_t,pattern)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, has_pattern),
            unsafe { eccdb_rust_layout(82) },
            "offsetof(eccdb_net_options_t,has_pattern)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, estimated_capacitance),
            unsafe { eccdb_rust_layout(83) },
            "offsetof(eccdb_net_options_t,estimated_capacitance)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, has_estimated_capacitance),
            unsafe { eccdb_rust_layout(84) },
            "offsetof(eccdb_net_options_t,has_estimated_capacitance)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, frequency),
            unsafe { eccdb_rust_layout(85) },
            "offsetof(eccdb_net_options_t,frequency)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, has_frequency),
            unsafe { eccdb_rust_layout(86) },
            "offsetof(eccdb_net_options_t,has_frequency)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, xtalk),
            unsafe { eccdb_rust_layout(87) },
            "offsetof(eccdb_net_options_t,xtalk)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, has_xtalk),
            unsafe { eccdb_rust_layout(88) },
            "offsetof(eccdb_net_options_t,has_xtalk)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, style),
            unsafe { eccdb_rust_layout(89) },
            "offsetof(eccdb_net_options_t,style)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, has_style),
            unsafe { eccdb_rust_layout(90) },
            "offsetof(eccdb_net_options_t,has_style)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, voltage),
            unsafe { eccdb_rust_layout(91) },
            "offsetof(eccdb_net_options_t,voltage)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, has_voltage),
            unsafe { eccdb_rust_layout(92) },
            "offsetof(eccdb_net_options_t,has_voltage)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, spacing_rules),
            unsafe { eccdb_rust_layout(93) },
            "offsetof(eccdb_net_options_t,spacing_rules)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_options_t, spacing_rule_count),
            unsafe { eccdb_rust_layout(94) },
            "offsetof(eccdb_net_options_t,spacing_rule_count)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_point_t>(),
            unsafe { eccdb_rust_layout(95) },
            "sizeof(eccdb_point_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_point_t>(),
            unsafe { eccdb_rust_layout(96) },
            "_Alignof(eccdb_point_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_point_t, x),
            unsafe { eccdb_rust_layout(97) },
            "offsetof(eccdb_point_t,x)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_point_t, y),
            unsafe { eccdb_rust_layout(98) },
            "offsetof(eccdb_point_t,y)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_rect_t>(),
            unsafe { eccdb_rust_layout(99) },
            "sizeof(eccdb_rect_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_rect_t>(),
            unsafe { eccdb_rust_layout(100) },
            "_Alignof(eccdb_rect_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_rect_t, ll_x),
            unsafe { eccdb_rust_layout(101) },
            "offsetof(eccdb_rect_t,ll_x)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_rect_t, ll_y),
            unsafe { eccdb_rust_layout(102) },
            "offsetof(eccdb_rect_t,ll_y)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_rect_t, ur_x),
            unsafe { eccdb_rust_layout(103) },
            "offsetof(eccdb_rect_t,ur_x)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_rect_t, ur_y),
            unsafe { eccdb_rust_layout(104) },
            "offsetof(eccdb_rect_t,ur_y)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_wire_point_t>(),
            unsafe { eccdb_rust_layout(105) },
            "sizeof(eccdb_wire_point_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_wire_point_t>(),
            unsafe { eccdb_rust_layout(106) },
            "_Alignof(eccdb_wire_point_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_point_t, position),
            unsafe { eccdb_rust_layout(107) },
            "offsetof(eccdb_wire_point_t,position)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_point_t, extension),
            unsafe { eccdb_rust_layout(108) },
            "offsetof(eccdb_wire_point_t,extension)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_point_t, has_extension),
            unsafe { eccdb_rust_layout(109) },
            "offsetof(eccdb_wire_point_t,has_extension)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_point_t, virtual_point),
            unsafe { eccdb_rust_layout(110) },
            "offsetof(eccdb_wire_point_t,virtual_point)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_wire_point_list_t>(),
            unsafe { eccdb_rust_layout(111) },
            "sizeof(eccdb_wire_point_list_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_wire_point_list_t>(),
            unsafe { eccdb_rust_layout(112) },
            "_Alignof(eccdb_wire_point_list_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_point_list_t, points),
            unsafe { eccdb_rust_layout(113) },
            "offsetof(eccdb_wire_point_list_t,points)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_point_list_t, point_count),
            unsafe { eccdb_rust_layout(114) },
            "offsetof(eccdb_wire_point_list_t,point_count)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_wire_mask_t>(),
            unsafe { eccdb_rust_layout(115) },
            "sizeof(eccdb_wire_mask_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_wire_mask_t>(),
            unsafe { eccdb_rust_layout(116) },
            "_Alignof(eccdb_wire_mask_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_mask_t, top),
            unsafe { eccdb_rust_layout(117) },
            "offsetof(eccdb_wire_mask_t,top)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_mask_t, cut),
            unsafe { eccdb_rust_layout(118) },
            "offsetof(eccdb_wire_mask_t,cut)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_mask_t, bottom),
            unsafe { eccdb_rust_layout(119) },
            "offsetof(eccdb_wire_mask_t,bottom)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_via_placement_t>(),
            unsafe { eccdb_rust_layout(120) },
            "sizeof(eccdb_via_placement_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_via_placement_t>(),
            unsafe { eccdb_rust_layout(121) },
            "_Alignof(eccdb_via_placement_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, point_index),
            unsafe { eccdb_rust_layout(122) },
            "offsetof(eccdb_via_placement_t,point_index)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, definition_kind),
            unsafe { eccdb_rust_layout(123) },
            "offsetof(eccdb_via_placement_t,definition_kind)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, definition),
            unsafe { eccdb_rust_layout(124) },
            "offsetof(eccdb_via_placement_t,definition)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, orientation),
            unsafe { eccdb_rust_layout(125) },
            "offsetof(eccdb_via_placement_t,orientation)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, has_mask),
            unsafe { eccdb_rust_layout(126) },
            "offsetof(eccdb_via_placement_t,has_mask)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, mask),
            unsafe { eccdb_rust_layout(127) },
            "offsetof(eccdb_via_placement_t,mask)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, has_rows_columns),
            unsafe { eccdb_rust_layout(128) },
            "offsetof(eccdb_via_placement_t,has_rows_columns)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, rows),
            unsafe { eccdb_rust_layout(129) },
            "offsetof(eccdb_via_placement_t,rows)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, columns),
            unsafe { eccdb_rust_layout(130) },
            "offsetof(eccdb_via_placement_t,columns)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, step_x),
            unsafe { eccdb_rust_layout(131) },
            "offsetof(eccdb_via_placement_t,step_x)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_placement_t, step_y),
            unsafe { eccdb_rust_layout(132) },
            "offsetof(eccdb_via_placement_t,step_y)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_wire_rectangle_t>(),
            unsafe { eccdb_rust_layout(133) },
            "sizeof(eccdb_wire_rectangle_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_wire_rectangle_t>(),
            unsafe { eccdb_rust_layout(134) },
            "_Alignof(eccdb_wire_rectangle_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_rectangle_t, point_index),
            unsafe { eccdb_rust_layout(135) },
            "offsetof(eccdb_wire_rectangle_t,point_index)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_rectangle_t, delta),
            unsafe { eccdb_rust_layout(136) },
            "offsetof(eccdb_wire_rectangle_t,delta)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_wire_metadata_t>(),
            unsafe { eccdb_rust_layout(137) },
            "sizeof(eccdb_wire_metadata_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_wire_metadata_t>(),
            unsafe { eccdb_rust_layout(138) },
            "_Alignof(eccdb_wire_metadata_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_metadata_t, net),
            unsafe { eccdb_rust_layout(139) },
            "offsetof(eccdb_wire_metadata_t,net)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_metadata_t, status),
            unsafe { eccdb_rust_layout(140) },
            "offsetof(eccdb_wire_metadata_t,status)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_metadata_t, shield_net),
            unsafe { eccdb_rust_layout(141) },
            "offsetof(eccdb_wire_metadata_t,shield_net)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_wire_path_t>(),
            unsafe { eccdb_rust_layout(142) },
            "sizeof(eccdb_wire_path_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_wire_path_t>(),
            unsafe { eccdb_rust_layout(143) },
            "_Alignof(eccdb_wire_path_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, layer),
            unsafe { eccdb_rust_layout(144) },
            "offsetof(eccdb_wire_path_t,layer)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, has_width),
            unsafe { eccdb_rust_layout(145) },
            "offsetof(eccdb_wire_path_t,has_width)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, width),
            unsafe { eccdb_rust_layout(146) },
            "offsetof(eccdb_wire_path_t,width)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, has_mask),
            unsafe { eccdb_rust_layout(147) },
            "offsetof(eccdb_wire_path_t,has_mask)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, mask),
            unsafe { eccdb_rust_layout(148) },
            "offsetof(eccdb_wire_path_t,mask)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, taper),
            unsafe { eccdb_rust_layout(149) },
            "offsetof(eccdb_wire_path_t,taper)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, taper_rule),
            unsafe { eccdb_rust_layout(150) },
            "offsetof(eccdb_wire_path_t,taper_rule)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, has_taper_rule),
            unsafe { eccdb_rust_layout(151) },
            "offsetof(eccdb_wire_path_t,has_taper_rule)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, shape),
            unsafe { eccdb_rust_layout(152) },
            "offsetof(eccdb_wire_path_t,shape)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, has_shape),
            unsafe { eccdb_rust_layout(153) },
            "offsetof(eccdb_wire_path_t,has_shape)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, style),
            unsafe { eccdb_rust_layout(154) },
            "offsetof(eccdb_wire_path_t,style)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, has_style),
            unsafe { eccdb_rust_layout(155) },
            "offsetof(eccdb_wire_path_t,has_style)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, points),
            unsafe { eccdb_rust_layout(156) },
            "offsetof(eccdb_wire_path_t,points)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, point_count),
            unsafe { eccdb_rust_layout(157) },
            "offsetof(eccdb_wire_path_t,point_count)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, vias),
            unsafe { eccdb_rust_layout(158) },
            "offsetof(eccdb_wire_path_t,vias)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, via_count),
            unsafe { eccdb_rust_layout(159) },
            "offsetof(eccdb_wire_path_t,via_count)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, rectangles),
            unsafe { eccdb_rust_layout(160) },
            "offsetof(eccdb_wire_path_t,rectangles)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_path_t, rectangle_count),
            unsafe { eccdb_rust_layout(161) },
            "offsetof(eccdb_wire_path_t,rectangle_count)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_wire_routing_t>(),
            unsafe { eccdb_rust_layout(162) },
            "sizeof(eccdb_wire_routing_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_wire_routing_t>(),
            unsafe { eccdb_rust_layout(163) },
            "_Alignof(eccdb_wire_routing_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_routing_t, paths),
            unsafe { eccdb_rust_layout(164) },
            "offsetof(eccdb_wire_routing_t,paths)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_routing_t, path_count),
            unsafe { eccdb_rust_layout(165) },
            "offsetof(eccdb_wire_routing_t,path_count)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_import_diagnostic_t>(),
            unsafe { eccdb_rust_layout(166) },
            "sizeof(eccdb_import_diagnostic_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_import_diagnostic_t>(),
            unsafe { eccdb_rust_layout(167) },
            "_Alignof(eccdb_import_diagnostic_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_import_diagnostic_t, source),
            unsafe { eccdb_rust_layout(168) },
            "offsetof(eccdb_import_diagnostic_t,source)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_import_diagnostic_t, statement),
            unsafe { eccdb_rust_layout(169) },
            "offsetof(eccdb_import_diagnostic_t,statement)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_import_diagnostic_t, occurrence_count),
            unsafe { eccdb_rust_layout(170) },
            "offsetof(eccdb_import_diagnostic_t,occurrence_count)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_via_rectangle_t>(),
            unsafe { eccdb_rust_layout(171) },
            "sizeof(eccdb_via_rectangle_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_via_rectangle_t>(),
            unsafe { eccdb_rust_layout(172) },
            "_Alignof(eccdb_via_rectangle_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_rectangle_t, layer),
            unsafe { eccdb_rust_layout(173) },
            "offsetof(eccdb_via_rectangle_t,layer)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_rectangle_t, rectangle),
            unsafe { eccdb_rust_layout(174) },
            "offsetof(eccdb_via_rectangle_t,rectangle)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_rectangle_t, mask),
            unsafe { eccdb_rust_layout(175) },
            "offsetof(eccdb_via_rectangle_t,mask)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_via_polygon_t>(),
            unsafe { eccdb_rust_layout(176) },
            "sizeof(eccdb_via_polygon_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_via_polygon_t>(),
            unsafe { eccdb_rust_layout(177) },
            "_Alignof(eccdb_via_polygon_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_polygon_t, layer),
            unsafe { eccdb_rust_layout(178) },
            "offsetof(eccdb_via_polygon_t,layer)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_polygon_t, points),
            unsafe { eccdb_rust_layout(179) },
            "offsetof(eccdb_via_polygon_t,points)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_polygon_t, point_count),
            unsafe { eccdb_rust_layout(180) },
            "offsetof(eccdb_via_polygon_t,point_count)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_polygon_t, mask),
            unsafe { eccdb_rust_layout(181) },
            "offsetof(eccdb_via_polygon_t,mask)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_generated_via_data_t>(),
            unsafe { eccdb_rust_layout(182) },
            "sizeof(eccdb_generated_via_data_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_generated_via_data_t>(),
            unsafe { eccdb_rust_layout(183) },
            "_Alignof(eccdb_generated_via_data_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, via_rule),
            unsafe { eccdb_rust_layout(184) },
            "offsetof(eccdb_generated_via_data_t,via_rule)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, bottom_layer),
            unsafe { eccdb_rust_layout(185) },
            "offsetof(eccdb_generated_via_data_t,bottom_layer)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, cut_layer),
            unsafe { eccdb_rust_layout(186) },
            "offsetof(eccdb_generated_via_data_t,cut_layer)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, top_layer),
            unsafe { eccdb_rust_layout(187) },
            "offsetof(eccdb_generated_via_data_t,top_layer)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, cut_size_x),
            unsafe { eccdb_rust_layout(188) },
            "offsetof(eccdb_generated_via_data_t,cut_size_x)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, cut_size_y),
            unsafe { eccdb_rust_layout(189) },
            "offsetof(eccdb_generated_via_data_t,cut_size_y)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, cut_spacing_x),
            unsafe { eccdb_rust_layout(190) },
            "offsetof(eccdb_generated_via_data_t,cut_spacing_x)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, cut_spacing_y),
            unsafe { eccdb_rust_layout(191) },
            "offsetof(eccdb_generated_via_data_t,cut_spacing_y)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, bottom_enclosure_x),
            unsafe { eccdb_rust_layout(192) },
            "offsetof(eccdb_generated_via_data_t,bottom_enclosure_x)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, bottom_enclosure_y),
            unsafe { eccdb_rust_layout(193) },
            "offsetof(eccdb_generated_via_data_t,bottom_enclosure_y)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, top_enclosure_x),
            unsafe { eccdb_rust_layout(194) },
            "offsetof(eccdb_generated_via_data_t,top_enclosure_x)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, top_enclosure_y),
            unsafe { eccdb_rust_layout(195) },
            "offsetof(eccdb_generated_via_data_t,top_enclosure_y)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, has_row_column),
            unsafe { eccdb_rust_layout(196) },
            "offsetof(eccdb_generated_via_data_t,has_row_column)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, rows),
            unsafe { eccdb_rust_layout(197) },
            "offsetof(eccdb_generated_via_data_t,rows)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, columns),
            unsafe { eccdb_rust_layout(198) },
            "offsetof(eccdb_generated_via_data_t,columns)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, has_origin),
            unsafe { eccdb_rust_layout(199) },
            "offsetof(eccdb_generated_via_data_t,has_origin)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, origin),
            unsafe { eccdb_rust_layout(200) },
            "offsetof(eccdb_generated_via_data_t,origin)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, has_offsets),
            unsafe { eccdb_rust_layout(201) },
            "offsetof(eccdb_generated_via_data_t,has_offsets)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, bottom_offset),
            unsafe { eccdb_rust_layout(202) },
            "offsetof(eccdb_generated_via_data_t,bottom_offset)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, top_offset),
            unsafe { eccdb_rust_layout(203) },
            "offsetof(eccdb_generated_via_data_t,top_offset)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, has_cut_pattern),
            unsafe { eccdb_rust_layout(204) },
            "offsetof(eccdb_generated_via_data_t,has_cut_pattern)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_generated_via_data_t, cut_pattern),
            unsafe { eccdb_rust_layout(205) },
            "offsetof(eccdb_generated_via_data_t,cut_pattern)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_design_via_data_t>(),
            unsafe { eccdb_rust_layout(206) },
            "sizeof(eccdb_design_via_data_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_design_via_data_t>(),
            unsafe { eccdb_rust_layout(207) },
            "_Alignof(eccdb_design_via_data_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_design_via_data_t, name),
            unsafe { eccdb_rust_layout(208) },
            "offsetof(eccdb_design_via_data_t,name)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_design_via_data_t, pattern_name),
            unsafe { eccdb_rust_layout(209) },
            "offsetof(eccdb_design_via_data_t,pattern_name)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_design_via_data_t, has_pattern_name),
            unsafe { eccdb_rust_layout(210) },
            "offsetof(eccdb_design_via_data_t,has_pattern_name)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_design_via_data_t, rectangles),
            unsafe { eccdb_rust_layout(211) },
            "offsetof(eccdb_design_via_data_t,rectangles)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_design_via_data_t, rectangle_count),
            unsafe { eccdb_rust_layout(212) },
            "offsetof(eccdb_design_via_data_t,rectangle_count)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_design_via_data_t, polygons),
            unsafe { eccdb_rust_layout(213) },
            "offsetof(eccdb_design_via_data_t,polygons)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_design_via_data_t, polygon_count),
            unsafe { eccdb_rust_layout(214) },
            "offsetof(eccdb_design_via_data_t,polygon_count)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_design_via_data_t, has_generated),
            unsafe { eccdb_rust_layout(215) },
            "offsetof(eccdb_design_via_data_t,has_generated)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_design_via_data_t, generated),
            unsafe { eccdb_rust_layout(216) },
            "offsetof(eccdb_design_via_data_t,generated)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_net_id_list_t>(),
            unsafe { eccdb_rust_layout(217) },
            "sizeof(eccdb_net_id_list_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_net_id_list_t>(),
            unsafe { eccdb_rust_layout(218) },
            "_Alignof(eccdb_net_id_list_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_id_list_t, items),
            unsafe { eccdb_rust_layout(219) },
            "offsetof(eccdb_net_id_list_t,items)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_net_id_list_t, count),
            unsafe { eccdb_rust_layout(220) },
            "offsetof(eccdb_net_id_list_t,count)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_instance_id_list_t>(),
            unsafe { eccdb_rust_layout(221) },
            "sizeof(eccdb_instance_id_list_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_instance_id_list_t>(),
            unsafe { eccdb_rust_layout(222) },
            "_Alignof(eccdb_instance_id_list_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_id_list_t, items),
            unsafe { eccdb_rust_layout(223) },
            "offsetof(eccdb_instance_id_list_t,items)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_id_list_t, count),
            unsafe { eccdb_rust_layout(224) },
            "offsetof(eccdb_instance_id_list_t,count)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_instance_pin_id_list_t>(),
            unsafe { eccdb_rust_layout(225) },
            "sizeof(eccdb_instance_pin_id_list_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_instance_pin_id_list_t>(),
            unsafe { eccdb_rust_layout(226) },
            "_Alignof(eccdb_instance_pin_id_list_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_pin_id_list_t, items),
            unsafe { eccdb_rust_layout(227) },
            "offsetof(eccdb_instance_pin_id_list_t,items)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_instance_pin_id_list_t, count),
            unsafe { eccdb_rust_layout(228) },
            "offsetof(eccdb_instance_pin_id_list_t,count)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_io_pin_id_list_t>(),
            unsafe { eccdb_rust_layout(229) },
            "sizeof(eccdb_io_pin_id_list_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_io_pin_id_list_t>(),
            unsafe { eccdb_rust_layout(230) },
            "_Alignof(eccdb_io_pin_id_list_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_io_pin_id_list_t, items),
            unsafe { eccdb_rust_layout(231) },
            "offsetof(eccdb_io_pin_id_list_t,items)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_io_pin_id_list_t, count),
            unsafe { eccdb_rust_layout(232) },
            "offsetof(eccdb_io_pin_id_list_t,count)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_wire_id_list_t>(),
            unsafe { eccdb_rust_layout(233) },
            "sizeof(eccdb_wire_id_list_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_wire_id_list_t>(),
            unsafe { eccdb_rust_layout(234) },
            "_Alignof(eccdb_wire_id_list_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_id_list_t, items),
            unsafe { eccdb_rust_layout(235) },
            "offsetof(eccdb_wire_id_list_t,items)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_wire_id_list_t, count),
            unsafe { eccdb_rust_layout(236) },
            "offsetof(eccdb_wire_id_list_t,count)"
        );
        assert_eq!(
            std::mem::size_of::<eccdb_via_id_list_t>(),
            unsafe { eccdb_rust_layout(237) },
            "sizeof(eccdb_via_id_list_t)"
        );
        assert_eq!(
            std::mem::align_of::<eccdb_via_id_list_t>(),
            unsafe { eccdb_rust_layout(238) },
            "_Alignof(eccdb_via_id_list_t)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_id_list_t, items),
            unsafe { eccdb_rust_layout(239) },
            "offsetof(eccdb_via_id_list_t,items)"
        );
        assert_eq!(
            std::mem::offset_of!(eccdb_via_id_list_t, count),
            unsafe { eccdb_rust_layout(240) },
            "offsetof(eccdb_via_id_list_t,count)"
        );
    }
}
