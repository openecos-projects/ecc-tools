# Usage:
#   tclsh steps/create_workspace.tcl
#   ecc -script steps/create_workspace.tcl

set script_dir [file normalize [file dirname [info script]]]
set flow_dir [file normalize [file join $script_dir ..]]
set workspace_root [file normalize [file join $flow_dir gcd]]
set source_config_dir [file normalize [file join $flow_dir config]]
set workspace_config_dir [file join $workspace_root config]
set source_origin_dir [file normalize [file join $flow_dir origin]]
set workspace_origin_dir [file join $workspace_root origin]

proc cw_write_file {path text} {
  file mkdir [file dirname $path]
  set fp [open $path w]
  puts -nonewline $fp $text
  close $fp
}

proc cw_copy_dir_files {label source_dir target_dir} {
  if {![file isdirectory $source_dir]} {
    error "source $label directory does not exist: $source_dir"
  }

  file mkdir $target_dir
  foreach source_path [glob -nocomplain -directory $source_dir *] {
    set target_path [file join $target_dir [file tail $source_path]]
    file copy -force $source_path $target_path
  }
}

proc cw_create_step_dirs {workspace_root step_name} {
  set step_dir [file join $workspace_root "${step_name}_ecc"]

  foreach dir [list \
    analysis \
    data data/cts data/drc data/fp data/no data/pl data/pl/density data/pl/gui data/pl/log data/pl/plot data/pl/report data/pnp data/rcx data/rt data/sta data/to \
    feature \
    log \
    output \
    report \
    script] {
    file mkdir [file join $step_dir $dir]
  }
}

proc cw_home_json {} {
  return {{
    "parameters": "/home/parameters.json",
    "flow": "/home/flow.json",
    "layout": "",
    "GDS merge": "",
    "checklist": "/home/checklist.json",
    "metrics": {},
    "monitor": {
        "step": [],
        "memory": [],
        "runtime": [],
        "instance": [],
        "frequency": []
    }
}
}
}

proc cw_parameters_json {} {
  return {{
    "PDK": "SKY130",
    "Design": "gcd",
    "Top module": "gcd",
    "Die": {
        "Size": [
            149.96,
            150.128
        ],
        "Area": 22513.19488
    },
    "Core": {
        "Size": [
            129.968,
            129.968
        ],
        "Area": 16891.681024,
        "Bounding box": "",
        "Utilitization": 0.8,
        "Margin": [
            9.996,
            10.08
        ],
        "Aspect ratio": 1
    },
    "Max fanout": 30,
    "Target density": 0.8,
    "Target overflow": 0.1,
    "Global right padding": 0,
    "Cell padding x": 0,
    "Routability opt flag": 1,
    "Clock": "clk",
    "Frequency max [MHz]": 100,
    "Bottom layer": "met1",
    "Top layer": "met4",
    "PDK Root": ""
}
}
}

proc cw_flow_json {} {
  set step_names {Floorplan fixFanout place CTS legalization route sta drc filler}
  set step_items {}
  foreach step_name $step_names {
    lappend step_items [format {        {
            "name": "%s",
            "tool": "ecc",
            "state": "Unstart",
            "runtime": "",
            "peak memory (mb)": 0,
            "info": {}
        }} $step_name]
  }

  return [format {{
    "steps": [
%s
    ]
}
} [join $step_items ",\n"]]
}

proc cw_checklist_json {} {
  return {{
    "path": "/home/checklist.json",
    "checklist": [
        {
            "step": "Floorplan",
            "type": "Area",
            "item": "check DIE area",
            "state": "Unstart"
        }
    ]
}
}
}

proc cw_subflow_json {step_name} {
  return [format {{
    "steps": [
        {
            "name": "%s",
            "tool": "ecc",
            "state": "Unstart",
            "runtime": "",
            "peak memory (mb)": 0,
            "info": {}
        }
    ]
}
} $step_name]
}

proc cw_create_workspace {workspace_root source_config_dir workspace_config_dir source_origin_dir workspace_origin_dir} {
  set step_names {Floorplan fixFanout place CTS legalization route sta drc filler}

  file mkdir $workspace_root
  foreach dir {home origin config log} {
    file mkdir [file join $workspace_root $dir]
  }

  foreach step_name $step_names {
    cw_create_step_dirs $workspace_root $step_name
    cw_write_file [file join $workspace_root "${step_name}_ecc" subflow.json] [cw_subflow_json $step_name]
    cw_write_file [file join $workspace_root "${step_name}_ecc" checklist.json] [cw_checklist_json]
  }

  cw_write_file [file join $workspace_root home home.json] [cw_home_json]
  cw_write_file [file join $workspace_root home parameters.json] [cw_parameters_json]
  cw_write_file [file join $workspace_root home flow.json] [cw_flow_json]
  cw_write_file [file join $workspace_root home checklist.json] [cw_checklist_json]

  cw_copy_dir_files config $source_config_dir $workspace_config_dir
  cw_copy_dir_files origin $source_origin_dir $workspace_origin_dir
}

cw_create_workspace $workspace_root $source_config_dir $workspace_config_dir $source_origin_dir $workspace_origin_dir

puts "Workspace created: $workspace_root"
puts "Config copied from: $source_config_dir"
puts "Config copied to: $workspace_config_dir"
puts "Origin copied from: $source_origin_dir"
puts "Origin copied to: $workspace_origin_dir"
