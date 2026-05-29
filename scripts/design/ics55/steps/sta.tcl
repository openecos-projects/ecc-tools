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

proc sta_json_parse {text} {
  set index 0
  set length [string length $text]
  set value [sta_json_parse_value $text index $length]
  sta_json_skip_space $text index $length
  if {$index < $length} {
    error "unexpected JSON data near index $index"
  }
  return $value
}

proc sta_json_skip_space {text index_var length} {
  upvar 1 $index_var index
  while {$index < $length} {
    set char [string index $text $index]
    if {$char ne " " && $char ne "\n" && $char ne "\r" && $char ne "\t"} {
      break
    }
    incr index
  }
}

proc sta_json_parse_value {text index_var length} {
  upvar 1 $index_var index
  sta_json_skip_space $text index $length
  if {$index >= $length} {
    error "unexpected end of JSON data"
  }

  set char [string index $text $index]
  switch -- $char {
    "\{" {
      return [sta_json_parse_object $text index $length]
    }
    "\[" {
      return [sta_json_parse_array $text index $length]
    }
    "\"" {
      return [sta_json_parse_string $text index $length]
    }
    default {
      return [sta_json_parse_scalar $text index $length]
    }
  }
}

proc sta_json_parse_object {text index_var length} {
  upvar 1 $index_var index
  incr index
  set result [dict create]
  sta_json_skip_space $text index $length
  if {[string index $text $index] eq "\}"} {
    incr index
    return $result
  }

  while {$index < $length} {
    sta_json_skip_space $text index $length
    if {[string index $text $index] ne "\""} {
      error "expected JSON object key near index $index"
    }
    set key [sta_json_parse_string $text index $length]
    sta_json_skip_space $text index $length
    if {[string index $text $index] ne ":"} {
      error "expected ':' after JSON object key '$key' near index $index"
    }
    incr index
    set value [sta_json_parse_value $text index $length]
    dict set result $key $value

    sta_json_skip_space $text index $length
    set char [string index $text $index]
    if {$char eq "\}"} {
      incr index
      return $result
    }
    if {$char ne ","} {
      error "expected comma or object close in JSON object near index $index"
    }
    incr index
  }

  error "unterminated JSON object"
}

proc sta_json_parse_array {text index_var length} {
  upvar 1 $index_var index
  incr index
  set result {}
  sta_json_skip_space $text index $length
  if {[string index $text $index] eq "\]"} {
    incr index
    return $result
  }

  while {$index < $length} {
    lappend result [sta_json_parse_value $text index $length]
    sta_json_skip_space $text index $length
    set char [string index $text $index]
    if {$char eq "\]"} {
      incr index
      return $result
    }
    if {$char ne ","} {
      error "expected ',' or ']' in JSON array near index $index"
    }
    incr index
  }

  error "unterminated JSON array"
}

proc sta_json_parse_string {text index_var length} {
  upvar 1 $index_var index
  incr index
  set result ""

  while {$index < $length} {
    set char [string index $text $index]
    if {$char eq "\""} {
      incr index
      return $result
    }

    if {$char eq "\\"} {
      incr index
      if {$index >= $length} {
        error "unterminated JSON string escape"
      }
      set escape [string index $text $index]
      switch -- $escape {
        "\"" {append result "\""}
        "\\" {append result "\\"}
        "/" {append result "/"}
        "b" {append result "\b"}
        "f" {append result "\f"}
        "n" {append result "\n"}
        "r" {append result "\r"}
        "t" {append result "\t"}
        "u" {
          set code [string range $text [expr {$index + 1}] [expr {$index + 4}]]
          if {![regexp {^[0-9A-Fa-f]{4}$} $code]} {
            error "invalid JSON unicode escape near index $index"
          }
          append result [format %c [scan $code %x]]
          incr index 4
        }
        default {
          error "invalid JSON escape '\\$escape' near index $index"
        }
      }
    } else {
      append result $char
    }
    incr index
  }

  error "unterminated JSON string"
}

proc sta_json_parse_scalar {text index_var length} {
  upvar 1 $index_var index
  set start $index
  while {$index < $length} {
    set char [string index $text $index]
    if {$char eq "," || $char eq "\]" || $char eq "\}" || $char eq " " || $char eq "\n" || $char eq "\r" || $char eq "\t"} {
      break
    }
    incr index
  }

  set value [string range $text $start [expr {$index - 1}]]
  if {$value eq ""} {
    error "expected JSON value near index $index"
  }
  if {$value in {true false null}} {
    return $value
  }
  if {[regexp {^-?[0-9]+([.][0-9]+)?([eE][+-]?[0-9]+)?$} $value]} {
    return $value
  }
  error "invalid JSON scalar '$value' near index $start"
}

proc sta_load_json {path} {
  step_require_file "JSON config" $path
  return [sta_json_parse [step_read_text_file $path]]
}

proc sta_resolve_path {path workspace_root pdk_root} {
  set expanded [step_expand_config_text "\"$path\"" $workspace_root $pdk_root]
  if {[string length $expanded] >= 2 && [string index $expanded 0] eq "\"" && [string index $expanded end] eq "\""} {
    return [string range $expanded 1 end-1]
  }
  return $expanded
}

proc sta_temp_dir_token {temperature} {
  set token [string map {- m} $temperature]
  return [step_safe_dir_name $token]
}

proc sta_find_liberty_corner {sta_data corner_name} {
  if {![dict exists $sta_data liberty]} {
    error "sta config missing liberty list"
  }
  foreach liberty [dict get $sta_data liberty] {
    if {[dict get $liberty corner] eq $corner_name} {
      return $liberty
    }
  }
  error "no liberty corner '$corner_name' found in sta config"
}

proc sta_find_rcx_corner {rcx_data rcx_corner_name} {
  if {![dict exists $rcx_data corners]} {
    error "rcx config missing corners list"
  }
  foreach corner [dict get $rcx_data corners] {
    if {[dict get $corner name] eq $rcx_corner_name} {
      return $corner
    }
  }
  error "no rcx corner '$rcx_corner_name' found in rcx config"
}

proc sta_find_spef_for_temp {rcx_corner temperature} {
  if {![dict exists $rcx_corner spef_file]} {
    error "rcx corner '[dict get $rcx_corner name]' missing spef_file list"
  }
  foreach spef_item [dict get $rcx_corner spef_file] {
    if {[dict exists $spef_item $temperature]} {
      return [dict get $spef_item $temperature]
    }
  }
  error "no SPEF for rcx corner '[dict get $rcx_corner name]' at temperature $temperature"
}

proc sta_normalize_spef_path {spef_file} {
  if {$spef_file eq "" || [file exists $spef_file]} {
    return $spef_file
  }

  set dirname [file dirname $spef_file]
  set tail [file tail $spef_file]
  if {[regexp {^(.*_)M([0-9]+C[.]spef)$} $tail -> prefix suffix]} {
    set normalized [file join $dirname "${prefix}m${suffix}"]
    if {[file exists $normalized]} {
      return $normalized
    }
  }

  return $spef_file
}

proc sta_normalize_liberty_path {liberty_file} {
  if {$liberty_file eq "" || [file exists $liberty_file]} {
    return $liberty_file
  }

  set liberty_dir [file dirname $liberty_file]
  set cell_dir [file tail [file dirname $liberty_dir]]
  set tail [file tail $liberty_file]
  if {[regexp {^(ics55_LLSC_H7C[[:alnum:]]+)(_.+)$} $tail -> old_prefix suffix]} {
    set normalized [file join $liberty_dir "${cell_dir}${suffix}"]
    if {[file exists $normalized]} {
      return $normalized
    }
  }

  return $liberty_file
}

proc sta_collect_signoff_items {sta_config rcx_config workspace_root pdk_root} {
  set sta_data [sta_load_json $sta_config]
  set rcx_data [sta_load_json $rcx_config]

  if {![dict exists $sta_data signoff]} {
    error "sta config missing signoff list"
  }

  set items {}
  foreach signoff_group [dict get $sta_data signoff] {
    foreach {corner_name rcx_corner_names} $signoff_group {
      set liberty [sta_find_liberty_corner $sta_data $corner_name]
      set temperature [dict get $liberty temperature]
      set liberty_files {}
      foreach liberty_file [dict get $liberty path] {
        set liberty_file [sta_resolve_path $liberty_file $workspace_root $pdk_root]
        lappend liberty_files [sta_normalize_liberty_path $liberty_file]
      }

      foreach rcx_corner_name $rcx_corner_names {
        set rcx_corner [sta_find_rcx_corner $rcx_data $rcx_corner_name]
        set spef_file [sta_find_spef_for_temp $rcx_corner $temperature]
        set spef_file [sta_resolve_path $spef_file $workspace_root $pdk_root]
        set spef_file [sta_normalize_spef_path $spef_file]
        lappend items [dict create \
          corner $corner_name \
          temperature $temperature \
          rcx_corner $rcx_corner_name \
          liberty_files $liberty_files \
          spef_file $spef_file]
      }
    }
  }

  return $items
}

proc sta_apply_data_config {db_config output_dir liberty_files sdc_file spef_file} {
  db_init \
    -config $db_config \
    -output_dir_path $output_dir \
    -lib_path $liberty_files \
    -sdc_path $sdc_file \
    -spef_path $spef_file
}

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
set sta_config [file join $config_dir sta.json]
set output_dir [file join $step_dir output]

set filler_output_dir [file join $workspace_root filler_ecc output]
set input_def [file join $filler_output_dir gcd_filler.def.gz]
set input_verilog [file join $filler_output_dir gcd_filler.v]
set input_db [file join $filler_output_dir gcd_filler_db]

set output_def [file join $output_dir gcd_sta.def.gz]
set output_verilog [file join $output_dir gcd_sta.v]
set output_gds [file join $output_dir gcd_sta.gds]
set output_json [file join $output_dir gcd_sta.json]
set output_db [file join $output_dir gcd_sta_db]
set feature_db [file join $step_dir feature sta.db.json]
set feature_step [file join $step_dir feature sta.step.json]
set report_db [file join $step_dir report sta.db.rpt]
set sta_dir [file join $step_dir data sta]
set sta_report_root $output_dir

step_update_flow_config $flow_config $config_dir
step_update_db_config $db_config $input_def $input_verilog $output_dir
step_prepare_configs [list $flow_config $db_config $rcx_config $sta_config] $workspace_root $pdk_root
set signoff_items [sta_collect_signoff_items $sta_config $rcx_config $workspace_root $pdk_root]

puts "=============================="
puts "Running $step_name"
puts "Workspace: $workspace_root"
puts "PDK: $pdk_root"
step_print_path "sta_config" $sta_config
step_print_path "rcx_config" $rcx_config
step_print_path "input_verilog" $input_verilog
step_print_path "sdc_file" $sdc_file

if {$RTL2GDS == 0} {
  puts "RTL2GDS is disabled, loading data."
  step_restore_or_load_design $flow_config $db_config $output_dir $tech_lef $lef_files $input_def $input_verilog $top_module $input_db
}

if {[llength $signoff_items] <= 0} {
  error "no signoff STA items found in $sta_config"
}
step_require_file "STA SDC" $sdc_file

file mkdir $sta_report_root
foreach signoff_item $signoff_items {
  set corner_name [dict get $signoff_item corner]
  set temperature [dict get $signoff_item temperature]
  set rcx_corner_name [dict get $signoff_item rcx_corner]
  set signoff_lib_files [dict get $signoff_item liberty_files]
  set signoff_spef_file [dict get $signoff_item spef_file]
  set report_corner_dir "${corner_name}_[sta_temp_dir_token $temperature]"
  set report_dir [file join $sta_report_root $report_corner_dir [step_safe_dir_name $rcx_corner_name]]

  step_require_files "STA liberty for $corner_name" $signoff_lib_files
  step_require_file "STA SPEF for $corner_name/$rcx_corner_name at $temperature" $signoff_spef_file
  file mkdir $report_dir

  puts "==> run STA for $corner_name/$rcx_corner_name at ${temperature}C"
  step_print_list "lib_files" $signoff_lib_files
  step_print_path "spef_file" $signoff_spef_file
  step_print_path "report_dir" $report_dir

  set sta_status [catch {
    sta_apply_data_config $db_config $output_dir $signoff_lib_files $sdc_file $signoff_spef_file
    release_sta
    init_sta $report_dir
    read_spef $signoff_spef_file
    report_timing
    release_sta
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
