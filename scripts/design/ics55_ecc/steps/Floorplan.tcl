# Usage:
#   ecc -script steps/Floorplan.tcl gcd

set step_name "Floorplan"
set script_dir [file normalize [file dirname [info script]]]
source [file normalize [file join $script_dir step_common.tcl]]

set flow_dir [file normalize [file join $script_dir ..]]
set default_workspace [file normalize [file join $flow_dir gcd home]]
set default_pdk [file normalize [file join $flow_dir .. .. .. .. icsprout55-pdk]]

lassign [step_setup_workspace $default_workspace $default_pdk] workspace_root pdk_root
set step_dir [file join $workspace_root Floorplan_ecc]
set config_dir [file join $workspace_root config]

set design_name "gcd"
set top_module "gcd"
set clock_name "clk"

source [file normalize [file join $script_dir pdk.tcl]]

set flow_config [file join $config_dir flow_config.json]
set db_config [file join $config_dir db_default_config.json]
set floorplan_config [file join $config_dir fp_default_config.json]
set output_dir [file join $step_dir output]

set input_def [file join $workspace_root origin gcd.def]
set input_def_gz [file join $workspace_root origin gcd.def.gz]
set input_verilog [file join $workspace_root origin gcd.v]
set load_def $input_def
if {![file exists $load_def] && [file exists $input_def_gz]} {
  set load_def $input_def_gz
}

set output_def [file join $output_dir gcd_Floorplan.def.gz]
set output_verilog [file join $output_dir gcd_Floorplan.v]
set output_gds [file join $output_dir gcd_Floorplan.gds]
set output_json [file join $output_dir gcd_Floorplan.json]
set output_db [file join $output_dir gcd_Floorplan_db]
set feature_db [file join $step_dir feature Floorplan.db.json]
set feature_step [file join $step_dir feature Floorplan.step.json]
set report_db [file join $step_dir report Floorplan.db.rpt]
set sta_dir [file join $step_dir data sta]

set core_utilization 0.4
set core_margin_x 2
set core_margin_y 2
set core_aspect_ratio 1
set core_site "core7"
set io_site "core7"
set corner_site "core7"
set tap_cell "FILLTAPH7R"
set end_cap "FILLTAPH7R"

step_update_flow_config $flow_config $config_dir
step_update_db_config $db_config $load_def $input_verilog $output_dir
step_prepare_configs [list $flow_config $db_config $floorplan_config] $workspace_root $pdk_root

puts "=============================="
puts "Running $step_name"
puts "Workspace: $workspace_root"
puts "PDK: $pdk_root"
step_print_list "lib_files" $lib_files
step_print_path "sdc_file" $sdc_file
step_print_path "spef_file" $spef_file
step_print_path "input_def_gz" $input_def_gz
step_print_path "floorplan_config" $floorplan_config

step_load_design $flow_config $db_config $output_dir $tech_lef $lef_files $load_def $input_verilog $top_module

init_floorplan \
  -core_util $core_utilization \
  -x_margin $core_margin_x \
  -y_margin $core_margin_y \
  -xy_ratio $core_aspect_ratio \
  -core_site $core_site \
  -io_site $io_site \
  -corner_site $corner_site

foreach track {
  {MET1 0 200 0 200}
  {MET2 0 200 0 200}
  {MET3 0 200 0 200}
  {MET4 0 200 0 200}
  {MET5 0 200 0 200}
  {T4M2 0 800 0 800}
  {RDL 0 5000 0 5000}
} {
  lassign $track layer x_start x_step y_start y_step
  gern_track -layer $layer -x_start $x_start -x_step $x_step -y_start $y_start -y_step $y_step
}

foreach io {
  {VDD INOUT 1}
  {VDDIO INOUT 1}
  {VSS INOUT 0}
  {VSSIO INOUT 0}
} {
  lassign $io net direction is_power
  add_pdn_io -net_name $net -direction $direction -is_power $is_power
}

foreach connect {
  {VDD VDD 1}
  {VDD VDD1 1}
  {VDD VNW 1}
  {VDDIO VDDIO 1}
  {VSS VSS 0}
  {VSS VSS1 0}
  {VSS VPW 0}
  {VSSIO VSSIO 0}
} {
  lassign $connect net pin is_power
  global_net_connect -net_name $net -instance_pin_name $pin -is_power $is_power
}

auto_place_pins -layer MET3 -width 300 -height 600
tapcell -tapcell $tap_cell -distance 58 -endcap $end_cap

create_grid -layer_name MET1 -net_name_power VDD -net_name_ground VSS -width 0.16 -offset 0
create_stripe -layer_name MET4 -net_name_power VDD -net_name_ground VSS -width 1 -pitch 16 -offset 0.5
create_stripe -layer_name MET5 -net_name_power VDD -net_name_ground VSS -width 1 -pitch 16 -offset 0.5
connect_two_layer -layers {MET1 MET4}
connect_two_layer -layers {MET4 MET5}

if {$clock_name ne ""} {
  set_net -net_name $clock_name -type CLOCK
}

step_save_design $step_name $output_def $output_verilog $output_gds $output_json $output_db $feature_db $feature_step $report_db $sta_dir 0

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, exiting flow."
  step_maybe_flow_exit
}
