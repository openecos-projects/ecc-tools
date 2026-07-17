if {![info exists pdk_root] || $pdk_root eq ""} {
  error "pdk.tcl requires pdk_root to be set before source"
}
if {![info exists workspace_root] || $workspace_root eq ""} {
  error "pdk.tcl requires workspace_root to be set before source"
}

set tech_lef [file join $pdk_root lef sg13g2_tech.lef]
set lef_files [list \
  [file join $pdk_root lef sg13g2_stdcell.lef] \
  [file join $pdk_root lef sg13g2_io.lef]]
set lib_files [list \
  [file join $pdk_root lib sg13g2_stdcell_typ_1p20V_25C.lib] \
  [file join $pdk_root lib sg13g2_io_typ_1p2V_3p3V_25C.lib]]
set sdc_file [file join $workspace_root origin gcd.sdc]
set spef_file [file join $pdk_root spef gcd.spef]

set TECH_LEF_PATH $tech_lef
set LEF_PATH $lef_files
set LIB_PATH $lib_files
set LIB_PATH_FIXFANOUT $lib_files
set LIB_PATH_DRV $lib_files
set LIB_PATH_HOLD $lib_files
set LIB_PATH_SETUP $lib_files
set SDC_PATH $sdc_file
set SPEF_PATH $spef_file
