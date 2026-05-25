# Usage:
#   ecc -script steps/fixFanout.tcl gcd

################################################################
# enable load data from db
# set RTL2GDS 0
# set RESTORE_DATA 1
# set RTL2GDS_FLOW 1
################################################################

set step_name "fixFanout"
set script_dir [file normalize [file dirname [info script]]]
source [file normalize [file join $script_dir step_common.tcl]]

set flow_dir [file normalize [file join $script_dir ..]]
set default_workspace [file normalize [file join $flow_dir gcd home]]
set default_pdk [file normalize [file join $flow_dir .. .. .. .. icsprout55-pdk]]

lassign [step_setup_workspace $default_workspace $default_pdk] workspace_root pdk_root
set step_dir [file join $workspace_root fixFanout_ecc]

set design_name "gcd"
set top_module "gcd"

source [file normalize [file join $script_dir pdk.tcl]]

set flow_config [file join $step_dir config flow_config.json]
set db_config [file join $step_dir config db_default_config.json]
set fixfanout_config [file join $step_dir config no_default_config_fixfanout.json]
set output_dir [file join $step_dir output]

set input_def [file join $workspace_root Floorplan_ecc output gcd_Floorplan.def.gz]
set input_verilog [file join $workspace_root Floorplan_ecc output gcd_Floorplan.v]
set input_db [file join $workspace_root Floorplan_ecc output gcd_Floorplan_db]

set output_def [file join $output_dir gcd_fixFanout.def.gz]
set output_verilog [file join $output_dir gcd_fixFanout.v]
set output_gds [file join $output_dir gcd_fixFanout.gds]
set output_json [file join $output_dir gcd_fixFanout.json]
set output_db [file join $output_dir gcd_fixFanout_db]
set feature_db [file join $step_dir feature fixFanout.db.json]
set feature_step [file join $step_dir feature fixFanout.step.json]
set report_db [file join $step_dir report fixFanout.db.rpt]
set sta_dir [file join $step_dir data sta]

step_prepare_configs [list $flow_config $db_config $fixfanout_config] $workspace_root $pdk_root

puts "=============================="
puts "Running $step_name"
puts "Workspace: $workspace_root"
puts "PDK: $pdk_root"
step_print_list "lib_files" $lib_files
step_print_path "sdc_file" $sdc_file
step_print_path "fixfanout_config" $fixfanout_config

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, loading data."
  step_restore_or_load_design $flow_config $db_config $output_dir $tech_lef $lef_files $input_def $input_verilog $top_module $input_db
}

run_no_fixfanout -config $fixfanout_config
step_save_design $step_name $output_def $output_verilog $output_gds $output_json $output_db $feature_db $feature_step $report_db $sta_dir

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, exiting flow."
  step_maybe_flow_exit
}
