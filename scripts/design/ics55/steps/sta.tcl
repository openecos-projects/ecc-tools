# Usage:
#   ecc -script steps/sta.tcl gcd

################################################################
# enable load data from db
# set RTL2GDS 0
# set RESTORE_DATA 1
# set RTL2GDS_FLOW 1
################################################################

set step_name "sta"
set script_dir [file normalize [file dirname [info script]]]
source [file normalize [file join $script_dir step_common.tcl]]

set flow_dir [file normalize [file join $script_dir ..]]
set default_workspace [file normalize [file join $flow_dir gcd home]]
set default_pdk [file normalize [file join $flow_dir .. .. .. .. icsprout55-pdk]]

lassign [step_setup_workspace $default_workspace $default_pdk] workspace_root pdk_root
set step_dir [file join $workspace_root sta_ecc]
set config_dir [file join $workspace_root config]

set design_name "gcd"
set top_module "gcd"

source [file normalize [file join $script_dir pdk.tcl]]

set flow_config [file join $config_dir flow_config.json]
set db_config [file join $config_dir db_default_config.json]
set rcx_config [file join $config_dir rcx.json]
set output_dir [file join $step_dir output]

set rcx_output_dir [file join $workspace_root RCX_ecc output]
set input_def [file join $rcx_output_dir gcd_RCX.def.gz]
set input_verilog [file join $rcx_output_dir gcd_RCX.v]
set input_db [file join $rcx_output_dir gcd_RCX_db]

set output_def [file join $output_dir gcd_sta.def.gz]
set output_verilog [file join $output_dir gcd_sta.v]
set output_gds [file join $output_dir gcd_sta.gds]
set output_json [file join $output_dir gcd_sta.json]
set output_db [file join $output_dir gcd_sta_db]
set feature_db [file join $step_dir feature sta.db.json]
set feature_step [file join $step_dir feature sta.step.json]
set report_db [file join $step_dir report sta.db.rpt]
set sta_dir [file join $step_dir data sta]
set sta_report_root [file join $output_dir sta]

step_update_flow_config $flow_config $config_dir
step_update_db_config $db_config $input_def $input_verilog $output_dir
step_prepare_configs [list $flow_config $db_config $rcx_config] $workspace_root $pdk_root
set spef_items [step_collect_spef_items $rcx_config $design_name {}]

puts "=============================="
puts "Running $step_name"
puts "Workspace: $workspace_root"
puts "PDK: $pdk_root"
step_print_path "rcx_config" $rcx_config
step_print_path "input_verilog" $input_verilog
step_print_path "sdc_file" $sdc_file
step_print_list "lib_files" $lib_files

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, loading data."
  step_restore_or_load_design $flow_config $db_config $output_dir $tech_lef $lef_files $input_def $input_verilog $top_module $input_db
}

if {[llength $spef_items] <= 0} {
  error "no SPEF files found for STA in $rcx_config"
}
step_require_file "STA netlist" $input_verilog
step_require_files "STA liberty" $lib_files
step_require_file "STA SDC" $sdc_file

file mkdir $sta_report_root
foreach spef_item $spef_items {
  lassign $spef_item spef_type spef_file
  step_require_file "STA SPEF for $spef_type" $spef_file

  set report_dir [file join $sta_report_root $spef_type]
  file mkdir $report_dir

  puts "==> run STA for $spef_type"
  step_print_path "spef_file" $spef_file
  step_print_path "report_dir" $report_dir

  set sta_status [catch {
    set_design_workspace $report_dir
    read_netlist $input_verilog
    read_liberty $lib_files
    link_design $top_module
    read_sdc $sdc_file
    read_spef $spef_file
    report_timing
  } sta_err sta_opts]
  step_safe_eval {release_sta}
  if {$sta_status != 0} {
    error $sta_err
  }
}

step_save_design $step_name $output_def $output_verilog $output_gds $output_json $output_db $feature_db $feature_step $report_db $sta_dir 0

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, exiting flow."
  step_maybe_flow_exit
}
