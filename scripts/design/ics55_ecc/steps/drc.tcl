# Usage:
#   ecc -script steps/drc.tcl gcd

################################################################
# enable load data from db
# set RTL2GDS 0
# set RESTORE_DATA 1
# set RTL2GDS_FLOW 1
################################################################

set step_name "drc"
set script_dir [file normalize [file dirname [info script]]]
source [file normalize [file join $script_dir step_common.tcl]]

set flow_dir [file normalize [file join $script_dir ..]]
set default_workspace [file normalize [file join $flow_dir gcd home]]
set default_pdk [file normalize [file join $flow_dir .. .. .. .. icsprout55-pdk]]

lassign [step_setup_workspace $default_workspace $default_pdk] workspace_root pdk_root
set step_dir [file join $workspace_root drc_ecc]

set design_name "gcd"
set top_module "gcd"

source [file normalize [file join $script_dir pdk.tcl]]

set flow_config [file join $step_dir config flow_config.json]
set db_config [file join $step_dir config db_default_config.json]
set drc_config [file join $step_dir config drc_default_config.json]
set drc_work_dir [file join $step_dir data drc]
set output_dir [file join $step_dir output]

set input_def [file join $workspace_root route_ecc output gcd_route.def.gz]
set input_verilog [file join $workspace_root route_ecc output gcd_route.v]
set input_db [file join $workspace_root route_ecc output gcd_route_db]

set output_def [file join $output_dir gcd_drc.def.gz]
set output_verilog [file join $output_dir gcd_drc.v]
set output_gds [file join $output_dir gcd_drc.gds]
set output_json [file join $output_dir gcd_drc.json]
set output_db [file join $output_dir gcd_drc_db]
set feature_db [file join $step_dir feature drc.db.json]
set feature_step [file join $step_dir feature drc.step.json]
set report_db [file join $step_dir report drc.db.rpt]
set drc_report [file join $step_dir report drc.rpt]
set sta_dir [file join $step_dir data sta]

set drc_thread_number 128

step_prepare_configs [list $flow_config $db_config $drc_config] $workspace_root $pdk_root

puts "=============================="
puts "Running $step_name"
puts "Workspace: $workspace_root"
puts "PDK: $pdk_root"
step_print_list "lib_files" $lib_files
step_print_path "drc_config" $drc_config
step_print_path "drc_work_dir" $drc_work_dir
step_print_path "drc_report" $drc_report

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, loading data."
  step_restore_or_load_design $flow_config $db_config $output_dir $tech_lef $lef_files $input_def $input_verilog $top_module $input_db
}

file mkdir $drc_work_dir
init_drc -temp_directory_path $drc_work_dir -thread_number $drc_thread_number
run_drc -config $drc_config -path $drc_report
step_save_design $step_name $output_def $output_verilog $output_gds $output_json $output_db $feature_db $feature_step $report_db $sta_dir
save_drc -path $feature_step

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, exiting flow."
  step_maybe_flow_exit
}
