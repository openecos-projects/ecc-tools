set data_dir [file dirname [file normalize [info script]]]
set output_dir [file join [pwd] "test_imj_antenna_command_[pid]"]

proc read_file {path} {
    set stream [open $path r]
    set text [read $stream]
    close $stream
    return $text
}

tech_lef_init -path [file join $data_dir tech.lef]
lef_init -path [file join $data_dir cell.lef]
def_init -path [file join $data_dir design.def.in]

init_mj -temp_directory_path $output_dir
def_save -path [file join $output_dir before.def]
check_antenna
set report [read_file [file join $output_dir antenna_checker antenna_check.rpt]]
if {![string match "*Total Violations: 10\n*" $report]} {
    error "Unexpected antenna violation count"
}
if {![string match "*Gate pin: early/A\n*" $report]} {
    error "Missing gate context"
}
destroy_mj

def_save -path [file join $output_dir after.def]
if {[read_file [file join $output_dir before.def]] ne [read_file [file join $output_dir after.def]]} {
    error "Standalone antenna check changed the design"
}
flow_exit
