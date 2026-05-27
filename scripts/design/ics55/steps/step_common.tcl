# Shared helpers for the per-step ECC debug scripts.

set ::ecc_workspace_root ""
set ::ecc_pdk_root ""

if {![info exists RTL2GDS]} {
  set RTL2GDS 0
}
if {![info exists RESTORE_DATA]} {
  set RESTORE_DATA 0
}
if {![info exists RTL2GDS_FLOW]} {
  set RTL2GDS_FLOW 0
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

proc step_require_file {label path} {
  if {$path eq "" || ![file exists $path]} {
    error "$label does not exist: $path"
  }
  return $path
}

proc step_require_files {label paths} {
  if {[llength $paths] <= 0} {
    error "no $label files specified"
  }
  foreach path $paths {
    step_require_file $label $path
  }
}

proc step_expand_config_text {text workspace_root pdk_root} {
  set replacements {}
  foreach prefix {
    CTS_ecc Floorplan_ecc fixFanout_ecc place_ecc legalization_ecc
    route_ecc drc_ecc filler_ecc RCX_ecc rcx_ecc sta_ecc config origin home
  } {
    lappend replacements "\"/$prefix\"" "\"$workspace_root/$prefix\""
    lappend replacements "\"/$prefix/" "\"$workspace_root/$prefix/"
  }
  foreach prefix {IP prtech corners} {
    lappend replacements "\"/$prefix\"" "\"$pdk_root/$prefix\""
    lappend replacements "\"/$prefix/" "\"$pdk_root/$prefix/"
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

proc step_read_text_file {path} {
  set fp [open $path r]
  set text [read $fp]
  close $fp
  return $text
}

proc step_write_text_file {path text} {
  step_ensure_parent_dir $path
  set fp [open $path w]
  puts -nonewline $fp $text
  close $fp
}

proc step_replace_json_string_key {text key value} {
  set result {}
  set pattern [format {^([[:space:]]*"%s"[[:space:]]*:[[:space:]]*")[^"]*(".*)$} $key]
  foreach line [split $text "\n"] {
    if {[regexp $pattern $line -> prefix suffix]} {
      set line "${prefix}${value}${suffix}"
    }
    lappend result $line
  }
  return [join $result "\n"]
}

proc step_update_flow_config {flow_config config_dir} {
  set text [step_read_text_file $flow_config]
  foreach {key file_name} {
    idb_path db_default_config.json
    ifp_path fp_default_config.json
    ipl_path pl_default_config.json
    irt_path rt_default_config.json
    idrc_path drc_default_config.json
    icts_path cts_default_config.json
    ito_path to_default_config_drv.json
    ipnp_path pnp_default_config.json
  } {
    set text [step_replace_json_string_key $text $key [file join $config_dir $file_name]]
  }
  step_write_text_file $flow_config $text
}

proc step_update_db_config {db_config input_def input_verilog output_dir} {
  set text [step_read_text_file $db_config]
  set text [step_replace_json_string_key $text def_path $input_def]
  set text [step_replace_json_string_key $text verilog_path $input_verilog]
  set text [step_replace_json_string_key $text output_dir_path $output_dir]
  step_write_text_file $db_config $text
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

proc step_dict_get_default {values key default} {
  if {[dict exists $values $key]} {
    return [dict get $values $key]
  }
  return $default
}

proc step_safe_dir_name {name} {
  set result ""
  foreach char [split [string trim $name] ""] {
    if {[regexp {^[[:alnum:]_.-]$} $char]} {
      append result $char
    } else {
      append result "_"
    }
  }
  if {$result eq ""} {
    return "spef"
  }
  return $result
}

proc step_spef_type_from_path {design_name spef_file} {
  set stem [file rootname [file tail $spef_file]]
  set design_prefix "${design_name}_"
  if {[string first $design_prefix $stem] == 0} {
    set stem [string range $stem [string length $design_prefix] end]
  }
  return $stem
}

proc step_parse_rcx_config {config_path} {
  step_require_file "rcx config" $config_path

  set fp [open $config_path r]
  set text [read $fp]
  close $fp

  set thread_num 64
  set output_dir ""
  set mapping_file ""
  set corners {}
  set in_corners 0
  set current_corner {}

  foreach line [split $text "\n"] {
    set line [string trim $line]
    if {$line eq ""} {
      continue
    }

    if {[regexp {^"corners"[[:space:]]*:[[:space:]]*\[} $line]} {
      set in_corners 1
      continue
    }

    if {!$in_corners} {
      if {[regexp {^"thread_num"[[:space:]]*:[[:space:]]*([0-9]+)} $line -> value]} {
        set thread_num $value
      } elseif {[regexp {^"output"[[:space:]]*:[[:space:]]*"([^"]*)"} $line -> value]} {
        set output_dir $value
      } elseif {[regexp {^"mapping_file"[[:space:]]*:[[:space:]]*"([^"]*)"} $line -> value]} {
        set mapping_file $value
      }
      continue
    }

    if {[regexp {^\]} $line]} {
      set in_corners 0
      continue
    }

    if {[regexp {^\{} $line]} {
      set current_corner [dict create]
      continue
    }

    if {[regexp {^\}} $line]} {
      if {[dict size $current_corner] > 0} {
        lappend corners $current_corner
      }
      set current_corner {}
      continue
    }

    if {[regexp {^"([^"]+)"[[:space:]]*:[[:space:]]*"([^"]*)"} $line -> key value]} {
      dict set current_corner $key $value
    }
  }

  return [dict create \
    thread_num $thread_num \
    output $output_dir \
    mapping_file $mapping_file \
    corners $corners]
}

proc step_collect_rcx_corners {rcx_config design_name} {
  set config [step_parse_rcx_config $rcx_config]
  return [dict get $config corners]
}

proc step_collect_spef_items {rcx_config design_name explicit_spef_files} {
  set spef_items {}

  if {[llength $explicit_spef_files] > 0} {
    foreach spef_file $explicit_spef_files {
      set spef_type [step_safe_dir_name [step_spef_type_from_path $design_name $spef_file]]
      lappend spef_items [list $spef_type $spef_file]
    }
    return $spef_items
  }

  set config [step_parse_rcx_config $rcx_config]
  foreach corner [dict get $config corners] {
    set spef_file [step_dict_get_default $corner spef_file ""]
    if {$spef_file eq ""} {
      continue
    }
    set spef_type [step_dict_get_default $corner name ""]
    if {$spef_type eq ""} {
      set spef_type [step_spef_type_from_path $design_name $spef_file]
    }
    lappend spef_items [list [step_safe_dir_name $spef_type] $spef_file]
  }

  return $spef_items
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

proc step_should_restore_data {} {
  return [expr {$::RTL2GDS == 0 && $::RESTORE_DATA == 1}]
}

proc step_restore_data {flow_config db_config output_dir input_db} {
  puts "==> restore data"
  step_print_path "flow_config" $flow_config
  step_print_path "db_config" $db_config
  step_print_path "output_dir" $output_dir
  step_print_path "input_db" $input_db

  if {$input_db eq "" || ![file exists $input_db]} {
    error "no valid input idb data: $input_db"
  }

  file mkdir $output_dir
  flow_init -config $flow_config
  db_init -config $db_config -output_dir_path $output_dir
  reset_data
  load_data -path $input_db
}

proc step_restore_or_load_design {flow_config db_config output_dir tech_lef lef_files input_def input_verilog top_module input_db} {
  if {[step_should_restore_data]} {
    step_restore_data $flow_config $db_config $output_dir $input_db
  } else {
    step_load_design $flow_config $db_config $output_dir $tech_lef $lef_files $input_def $input_verilog $top_module
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
  save_data -path $output_db
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
  if {$::RTL2GDS_FLOW == 1} {
    return
  }
  if {[llength [info commands flow_exit]] > 0} {
    flow_exit
  }
}
