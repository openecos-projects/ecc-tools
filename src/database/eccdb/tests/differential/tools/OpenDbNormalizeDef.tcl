# SPDX-License-Identifier: Apache-2.0

if {$argc != 3} {
  puts stderr "usage: OpenDbNormalizeDef.tcl input.lef input.def output.def"
  exit 2
}

set lef_file [lindex $argv 0]
set def_file [lindex $argv 1]
set output_file [lindex $argv 2]

if {[catch {
  set db [odb::dbDatabase_create]
  odb::read_lef $db $lef_file
  odb::read_def $db $def_file
  set chip [$db getChip]
  if {$chip == "NULL"} {
    error "OpenDB did not create a chip"
  }
  set block [$chip getBlock]
  if {$block == "NULL"} {
    error "OpenDB did not create a block"
  }
  if {![odb::write_def $block $output_file]} {
    error "OpenDB failed to write DEF"
  }
} message options]} {
  puts stderr $message
  if {[dict exists $options -errorinfo]} {
    puts stderr [dict get $options -errorinfo]
  }
  exit 1
}

exit 0
