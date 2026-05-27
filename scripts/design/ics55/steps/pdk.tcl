if {![info exists pdk_root] || $pdk_root eq ""} {
  error "pdk.tcl requires pdk_root to be set before source"
}
if {![info exists workspace_root] || $workspace_root eq ""} {
  error "pdk.tcl requires workspace_root to be set before source"
}

set tech_lef [file join $pdk_root prtech techLEF N551P6M_ecos.lef]
set lef_files [list \
  [file join $pdk_root IP STD_cell ics55_LLSC_H7C_V1p10C100 ics55_LLSC_H7CR lef ics55_LLSC_H7CR_ecos.lef] \
  [file join $pdk_root IP STD_cell ics55_LLSC_H7C_V1p10C100 ics55_LLSC_H7CL lef ics55_LLSC_H7CL_ecos.lef]]
set lib_files [list \
  [file join $pdk_root IP STD_cell ics55_LLSC_H7C_V1p10C100 ics55_LLSC_H7CR liberty ics55_LLSC_H7CR_ss_rcworst_1p08_125_nldm.lib] \
  [file join $pdk_root IP STD_cell ics55_LLSC_H7C_V1p10C100 ics55_LLSC_H7CL liberty ics55_LLSC_H7CL_ss_rcworst_1p08_125_nldm.lib]]
set sdc_file [file join $workspace_root origin gcd.sdc]
set spef_file [file join $workspace_root origin gcd.spef]

set TECH_LEF_PATH $tech_lef
set LEF_PATH $lef_files
set LIB_PATH $lib_files
set LIB_PATH_FIXFANOUT $lib_files
set SDC_PATH $sdc_file
set SPEF_PATH $spef_file
