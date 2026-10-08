# ***************************************************************************************
# SPDX-License-Identifier: MulanPSL-2.0
# Copyright (c) 2023-2025 Peng Cheng Laboratory
# Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
# Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
#
# iEDA is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
# http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.
# ***************************************************************************************

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
