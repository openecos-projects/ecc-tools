# Usage:
#   ecc -script steps/harden.tcl gcd

################################################################
# enable load data from db
# set RTL2GDS 0
# set RESTORE_DATA 1
# set RTL2GDS_FLOW 1
################################################################

set step_name "harden"
set script_dir [file normalize [file dirname [info script]]]
source [file normalize [file join $script_dir step_common.tcl]]

set flow_dir [file normalize [file join $script_dir ..]]
set default_workspace [file normalize [file join $flow_dir gcd home]]
set default_pdk [file normalize [file join $flow_dir .. .. .. .. icsprout55-pdk]]

lassign [step_setup_workspace $default_workspace $default_pdk] workspace_root pdk_root
set step_dir [file join $workspace_root harden_ecc]
set config_dir [file join $workspace_root config]

set design_name "gcd"
set top_module "gcd"

source [file normalize [file join $script_dir pdk.tcl]]

set flow_config [file join $config_dir flow_config.json]
set db_config [file join $config_dir db_default_config.json]
set output_dir [file join $step_dir output]

set filler_output_dir [file join $workspace_root filler_ecc output]
set input_def [file join $filler_output_dir gcd_filler.def.gz]
set input_verilog [file join $filler_output_dir gcd_filler.v]
set input_db [file join $filler_output_dir gcd_filler_db]

set output_def [file join $output_dir gcd_harden.def.gz]
set output_verilog [file join $output_dir gcd_harden.v]
set output_gds [file join $output_dir gcd_harden.gds]
set output_json [file join $output_dir gcd_harden.json]
set output_lef [file join $output_dir gcd_harden.lef]
set output_lib [file join $output_dir gcd_harden.lib]
set output_db [file join $output_dir gcd_harden_db]
set feature_db [file join $step_dir feature harden.db.json]
set feature_step [file join $step_dir feature harden.step.json]
set report_db [file join $step_dir report harden.db.rpt]
set sta_dir [file join $step_dir data sta]

# Instance names to include in the SoC harden core list.
# It can also be overridden by setting HARDEN_CORES="inst_a inst_b" in the environment.
if {![info exists harden_cores]} {
  set harden_cores {}
}
if {[info exists ::env(HARDEN_CORES)] && $::env(HARDEN_CORES) ne ""} {
  set harden_cores $::env(HARDEN_CORES)
}

step_update_flow_config $flow_config $config_dir
step_update_db_config $db_config $input_def $input_verilog $output_dir
step_prepare_configs [list $flow_config $db_config] $workspace_root $pdk_root

puts "=============================="
puts "Running $step_name"
puts "Workspace: $workspace_root"
puts "PDK: $pdk_root"
step_print_path "input_def" $input_def
step_print_path "input_verilog" $input_verilog
step_print_path "input_db" $input_db
step_print_list "harden_cores" $harden_cores

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, loading data."
  step_restore_or_load_design $flow_config $db_config $output_dir $tech_lef $lef_files $input_def $input_verilog $top_module $input_db
}

puts "==> save harden data"
step_print_path "output_def" $output_def
step_print_path "output_verilog" $output_verilog
step_print_path "output_gds" $output_gds
step_print_path "output_json" $output_json
step_print_path "output_lef" $output_lef
step_print_path "output_lib" $output_lib
step_print_path "output_db" $output_db
step_print_path "feature_db" $feature_db
step_print_path "feature_step" $feature_step
step_print_path "report_db" $report_db
step_print_path "sta_dir" $sta_dir

step_ensure_parent_dir $output_def
step_ensure_parent_dir $output_verilog
step_ensure_parent_dir $output_gds
step_ensure_parent_dir $output_json
step_ensure_parent_dir $output_lef
step_ensure_parent_dir $output_lib
step_ensure_parent_dir $output_db
step_ensure_parent_dir $feature_db
step_ensure_parent_dir $feature_step
step_ensure_parent_dir $report_db
file mkdir $sta_dir

def_save -path $output_def
netlist_save -path $output_verilog -exclude_cell_names {}
gds_save -path $output_gds -harden
write_abstract_lef -path $output_lef

set timing_status [catch {
  release_sta
  init_sta $sta_dir
  update_timing
  write_timing_model -output_lib_path $output_lib -analysis_mode max
} timing_err timing_opts]
step_safe_eval {release_sta}
if {$timing_status != 0} {
  error $timing_err
}

save_data -path $output_db

set validate_path [file join [file dirname $report_db] "${step_name}.idb_validate.json"]
if {[llength [info commands idb_validate]] > 0} {
  step_safe_eval [list idb_validate -path $validate_path]
}
feature_summary -path $feature_db
# feature_tool does not currently have a harden step parser.
report_db -path $report_db

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, exiting flow."
  step_maybe_flow_exit
}
