# Usage:
#   ecc -script steps/legalization.tcl gcd

set step_name "legalization"
set script_dir [file normalize [file dirname [info script]]]
source [file normalize [file join $script_dir step_common.tcl]]

set flow_dir [file normalize [file join $script_dir ..]]
set default_workspace [file normalize [file join $flow_dir gcd home]]
set default_pdk [file normalize [file join $flow_dir .. .. foundry sky130]]

lassign [step_setup_workspace $default_workspace $default_pdk] workspace_root pdk_root
set step_dir [file join $workspace_root legalization_ecc]
set config_dir [file join $workspace_root config]

set design_name "gcd"
set top_module "gcd"

source [file normalize [file join $script_dir pdk.tcl]]

set flow_config [file join $config_dir flow_config.json]
set db_config [file join $config_dir db_default_config.json]
set place_config [file join $config_dir pl_default_config.json]
set output_dir [file join $step_dir output]

set input_def [file join $workspace_root CTS_ecc output gcd_CTS.def.gz]
set input_verilog [file join $workspace_root CTS_ecc output gcd_CTS.v]
set input_db [file join $workspace_root CTS_ecc output gcd_CTS_db]

set output_def [file join $output_dir gcd_legalization.def.gz]
set output_verilog [file join $output_dir gcd_legalization.v]
set output_gds [file join $output_dir gcd_legalization.gds]
set output_json [file join $output_dir gcd_legalization.json]
set output_db [file join $output_dir gcd_legalization_db]
set feature_db [file join $step_dir feature legalization.db.json]
set feature_step [file join $step_dir feature legalization.step.json]
set report_db [file join $step_dir report legalization.db.rpt]
set sta_dir [file join $step_dir data sta]

step_update_flow_config $flow_config $config_dir
step_update_db_config $db_config $input_def $input_verilog $output_dir
step_prepare_configs [list $flow_config $db_config $place_config] $workspace_root $pdk_root

puts "=============================="
puts "Running $step_name"
puts "Workspace: $workspace_root"
puts "PDK: $pdk_root"
step_print_list "lib_files" $lib_files
step_print_path "place_config" $place_config

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, loading data."
  step_restore_or_load_design $flow_config $db_config $output_dir $tech_lef $lef_files $input_def $input_verilog $top_module $input_db
}

step_safe_eval {destroy_pl}
run_incremental_flow -config $place_config
step_save_design $step_name $output_def $output_verilog $output_gds $output_json $output_db $feature_db $feature_step $report_db $sta_dir
step_safe_eval {destroy_pl}

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, exiting flow."
  step_maybe_flow_exit
}
