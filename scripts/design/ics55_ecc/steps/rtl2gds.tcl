# Usage:
#   ecc -script steps/rtl2gds.tcl gcd

################################################################
# enable load data from db
set RTL2GDS 1
set RESTORE_DATA 1
set RTL2GDS_FLOW 1
################################################################

set script_dir [file normalize [file dirname [info script]]]

source $script_dir/Floorplan.tcl
source $script_dir/fixFanout.tcl
source $script_dir/place.tcl
source $script_dir/CTS.tcl
source $script_dir/legalization.tcl
source $script_dir/route.tcl
source $script_dir/drc.tcl
source $script_dir/filler.tcl

set RTL2GDS_FLOW 0
step_maybe_flow_exit
