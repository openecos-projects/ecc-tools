# Usage:
#   ecc -script steps/rcx.tcl gcd

################################################################
# enable load data from db
# set RTL2GDS 0
# set RESTORE_DATA 1
# set RTL2GDS_FLOW 1
################################################################

set step_name "rcx"
set script_dir [file normalize [file dirname [info script]]]
source [file normalize [file join $script_dir step_common.tcl]]

set flow_dir [file normalize [file join $script_dir ..]]
set default_workspace [file normalize [file join $flow_dir gcd home]]
set default_pdk [file normalize [file join $flow_dir .. .. .. .. icsprout55-pdk]]

lassign [step_setup_workspace $default_workspace $default_pdk] workspace_root pdk_root
set step_dir [file join $workspace_root RCX_ecc]
set config_dir [file join $workspace_root config]

set design_name "gcd"
set top_module "gcd"

source [file normalize [file join $script_dir pdk.tcl]]

set flow_config [file join $config_dir flow_config.json]
set db_config [file join $config_dir db_default_config.json]
set rcx_config [file join $config_dir rcx.json]
set output_dir [file join $step_dir output]

set filler_output_dir [file join $workspace_root filler_ecc output]
set input_def [file join $filler_output_dir gcd_filler.def.gz]
set input_verilog [file join $filler_output_dir gcd_filler.v]
set input_db [file join $filler_output_dir gcd_filler_db]

set output_def [file join $output_dir gcd_RCX.def.gz]
set output_verilog [file join $output_dir gcd_RCX.v]
set output_gds [file join $output_dir gcd_RCX.gds]
set output_json [file join $output_dir gcd_RCX.json]
set output_db [file join $output_dir gcd_RCX_db]
set feature_db [file join $step_dir feature RCX.db.json]
set feature_step [file join $step_dir feature RCX.step.json]
set report_db [file join $step_dir report RCX.db.rpt]
set sta_dir [file join $step_dir data sta]

step_update_flow_config $flow_config $config_dir
step_update_db_config $db_config $input_def $input_verilog $output_dir
step_prepare_configs [list $flow_config $db_config $rcx_config] $workspace_root $pdk_root
set rcx_data [step_parse_rcx_config $rcx_config]
set rcx_thread_num [dict get $rcx_data thread_num]
set rcx_output_dir [dict get $rcx_data output]
set rcx_mapping_file [dict get $rcx_data mapping_file]
set rcx_corners [dict get $rcx_data corners]

puts "=============================="
puts "Running $step_name"
puts "Workspace: $workspace_root"
puts "PDK: $pdk_root"
step_print_path "rcx_config" $rcx_config
step_print_path "rcx_output_dir" $rcx_output_dir
step_print_path "mapping_file" $rcx_mapping_file
step_print_list "lib_files" $lib_files

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, loading data."
  step_restore_or_load_design $flow_config $db_config $output_dir $tech_lef $lef_files $input_def $input_verilog $top_module $input_db
}

step_require_file "rcx mapping file" $rcx_mapping_file
if {[llength $rcx_corners] <= 0} {
  error "no RCX corners found in $rcx_config"
}

file mkdir $rcx_output_dir
init_rcx -thread $rcx_thread_num
read_mapping $rcx_mapping_file

foreach corner $rcx_corners {
  set corner_name [step_dict_get_default $corner name ""]
  set itf_file [step_dict_get_default $corner itf_file ""]
  set captab_file [step_dict_get_default $corner captab_file ""]
  if {$corner_name eq ""} {
    error "RCX corner has no name in $rcx_config"
  }
  step_require_file "RCX ITF for $corner_name" $itf_file
  step_require_file "RCX captab for $corner_name" $captab_file
  read_corner -name $corner_name -itf $itf_file -captab $captab_file
}

run_rcx
report_rcx $rcx_output_dir

step_save_design $step_name $output_def $output_verilog $output_gds $output_json $output_db $feature_db $feature_step $report_db $sta_dir 0

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, exiting flow."
  step_maybe_flow_exit
}
