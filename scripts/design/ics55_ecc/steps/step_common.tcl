# Shared helpers for the per-step ECC debug scripts.

set ::ecc_workspace_root ""
set ::ecc_pdk_root ""

if {![info exists RTL2GDS]} {
  set RTL2GDS 0
}

proc set_workspace {path} {
  if {$path eq ""} {
    error "set_workspace requires a non-empty path"
  }
  set ::ecc_workspace_root [file normalize $path]
  return $::ecc_workspace_root
}

proc set_pdk {path} {
  if {$path eq ""} {
    error "set_pdk requires a non-empty path"
  }
  set ::ecc_pdk_root [file normalize $path]
  return $::ecc_pdk_root
}

proc ws_get {key} {
  return [workspace_get -key $key]
}

proc step_setup_workspace {default_workspace default_pdk} {
  global argv

  if {[llength $argv] > 0} {
    set workspace_home [file normalize [lindex $argv 0]]
  } elseif {[info exists ::env(WORKSPACE_HOME)]} {
    set workspace_home [file normalize $::env(WORKSPACE_HOME)]
  } else {
    set workspace_home $default_workspace
  }

  puts "Workspace: $workspace_home"
  workspace_load -path $workspace_home
  set workspace_root [ws_get workspace.dir]
  set_workspace $workspace_root

  set pdk_root [ws_get pdk.root]
  if {$pdk_root eq ""} {
    set pdk_root $default_pdk
  }
  set_pdk $pdk_root
  puts "PDK: $::ecc_pdk_root"

  set force_config 0
  if {[info exists ::env(WORKSPACE_FORCE_CONFIG)]} {
    set force_config $::env(WORKSPACE_FORCE_CONFIG)
  }
  workspace_prepare -force $force_config

  return [list $::ecc_workspace_root $::ecc_pdk_root]
}

proc step_safe_eval {script} {
  if {[catch {uplevel 1 $script} err opts]} {
    puts stderr "WARNING: $script failed: $err"
    return 0
  }
  return 1
}

proc step_ensure_parent_dir {path} {
  if {$path ne ""} {
    file mkdir [file dirname $path]
  }
}

proc step_expand_config_text {text workspace_root pdk_root} {
  set replacements {}
  foreach prefix {
    CTS_ecc Floorplan_ecc fixFanout_ecc place_ecc legalization_ecc
    route_ecc drc_ecc filler_ecc origin home
  } {
    lappend replacements "\"/$prefix\"" "\"$workspace_root/$prefix\""
    lappend replacements "\"/$prefix/" "\"$workspace_root/$prefix/"
    lappend replacements "\"$prefix\"" "\"$workspace_root/$prefix\""
    lappend replacements "\"$prefix/" "\"$workspace_root/$prefix/"
  }
  foreach prefix {IP prtech resource} {
    lappend replacements "\"/$prefix\"" "\"$pdk_root/$prefix\""
    lappend replacements "\"/$prefix/" "\"$pdk_root/$prefix/"
    lappend replacements "\"$prefix\"" "\"$pdk_root/$prefix\""
    lappend replacements "\"$prefix/" "\"$pdk_root/$prefix/"
  }
  return [string map $replacements $text]
}

proc step_expand_config {config_path workspace_root pdk_root} {
  if {$config_path eq "" || ![file exists $config_path]} {
    return $config_path
  }

  set fp [open $config_path r]
  set text [read $fp]
  close $fp

  set expanded_text [step_expand_config_text $text $workspace_root $pdk_root]
  if {$expanded_text ne $text} {
    set fp [open $config_path w]
    puts -nonewline $fp $expanded_text
    close $fp
  }

  return $config_path
}

proc step_prepare_configs {config_files workspace_root pdk_root} {
  foreach config_file $config_files {
    step_expand_config $config_file $workspace_root $pdk_root
  }
}

proc step_print_path {label value} {
  puts [format "  %-18s %s" "${label}:" $value]
}

proc step_print_list {label values} {
  puts [format "  %-18s" "${label}:"]
  foreach value $values {
    puts "    $value"
  }
}

proc step_load_design {flow_config db_config output_dir tech_lef lef_files input_def input_verilog top_module} {
  puts "==> load data"
  step_print_path "flow_config" $flow_config
  step_print_path "db_config" $db_config
  step_print_path "output_dir" $output_dir
  step_print_path "tech_lef" $tech_lef
  step_print_list "lef_files" $lef_files
  step_print_path "input_def" $input_def
  step_print_path "input_verilog" $input_verilog

  file mkdir $output_dir
  flow_init -config $flow_config
  db_init -config $db_config -output_dir_path $output_dir

  tech_lef_init -path $tech_lef
  lef_init -path $lef_files

  if {$input_def ne "" && [file exists $input_def]} {
    def_init -path $input_def
  } elseif {$input_verilog ne "" && [file exists $input_verilog]} {
    verilog_init -path $input_verilog -top $top_module
  } else {
    error "no valid input DEF or Verilog: DEF=$input_def Verilog=$input_verilog"
  }
}

proc step_save_design {step_name output_def output_verilog output_gds output_json output_db feature_db feature_step_path report_db_path sta_dir {feature_step_enable 1}} {
  puts "==> save data"
  step_print_path "output_def" $output_def
  step_print_path "output_verilog" $output_verilog
  step_print_path "output_gds" $output_gds
  step_print_path "output_json" $output_json
  step_print_path "output_db" $output_db
  step_print_path "feature_db" $feature_db
  step_print_path "feature_step" $feature_step_path
  step_print_path "report_db" $report_db_path
  step_print_path "sta_dir" $sta_dir

  step_ensure_parent_dir $output_def
  step_ensure_parent_dir $output_verilog
  step_ensure_parent_dir $output_gds
  step_ensure_parent_dir $output_json
  step_ensure_parent_dir $output_db
  step_ensure_parent_dir $feature_db
  step_ensure_parent_dir $feature_step_path
  step_ensure_parent_dir $report_db_path
  file mkdir $sta_dir

  def_save -path $output_def
  netlist_save -path $output_verilog -exclude_cell_names {}
  step_safe_eval [list gds_save -path $output_gds]
  # json_save -path $output_json
  # save_data -path $output_db
  set validate_path [file join [file dirname $report_db_path] "${step_name}.idb_validate.json"]
  if {[llength [info commands idb_validate]] > 0} {
    step_safe_eval [list idb_validate -path $validate_path]
  }
  feature_summary -path $feature_db
  if {$feature_step_enable} {
    feature_tool -path $feature_step_path -step $step_name
  }
  report_db -path $report_db_path

  # if {[step_safe_eval [list init_sta -output $sta_dir]]} {
  #   step_safe_eval {report_timing -json}
  #   step_safe_eval {release_sta}
  # }
}

proc step_maybe_flow_exit {} {
  if {[llength [info commands flow_exit]] > 0} {
    flow_exit
  }
}
