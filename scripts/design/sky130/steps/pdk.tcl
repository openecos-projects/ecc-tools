if {![info exists pdk_root] || $pdk_root eq ""} {
  error "pdk.tcl requires pdk_root to be set before source"
}
if {![info exists workspace_root] || $workspace_root eq ""} {
  error "pdk.tcl requires workspace_root to be set before source"
}

set tech_lef [file join $pdk_root lef sky130_fd_sc_hs.tlef]
set lef_files [list \
  [file join $pdk_root lef sky130_fd_sc_hs_merged.lef] \
  [file join $pdk_root lef sky130_ef_io__com_bus_slice_10um.lef] \
  [file join $pdk_root lef sky130_ef_io__com_bus_slice_1um.lef] \
  [file join $pdk_root lef sky130_ef_io__com_bus_slice_20um.lef] \
  [file join $pdk_root lef sky130_ef_io__com_bus_slice_5um.lef] \
  [file join $pdk_root lef sky130_ef_io__connect_vcchib_vccd_and_vswitch_vddio_slice_20um.lef] \
  [file join $pdk_root lef sky130_ef_io__corner_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__disconnect_vccd_slice_5um.lef] \
  [file join $pdk_root lef sky130_ef_io__disconnect_vdda_slice_5um.lef] \
  [file join $pdk_root lef sky130_ef_io__gpiov2_pad_wrapped.lef] \
  [file join $pdk_root lef sky130_ef_io__vccd_hvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vccd_lvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vdda_hvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vdda_lvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vddio_hvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vddio_lvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vssa_hvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vssa_lvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vssd_hvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vssd_lvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vssio_hvc_pad.lef] \
  [file join $pdk_root lef sky130_ef_io__vssio_lvc_pad.lef] \
  [file join $pdk_root lef sky130_fd_io__top_xres4v2.lef] \
  [file join $pdk_root lef sky130io_fill.lef] \
  [file join $pdk_root lef sky130_sram_1rw1r_128x256_8.lef] \
  [file join $pdk_root lef sky130_sram_1rw1r_44x64_8.lef] \
  [file join $pdk_root lef sky130_sram_1rw1r_64x256_8.lef] \
  [file join $pdk_root lef sky130_sram_1rw1r_80x64_8.lef]]
set lib_files [list \
  [file join $pdk_root lib sky130_fd_sc_hs__tt_025C_1v80.lib] \
  [file join $pdk_root lib sky130_dummy_io.lib] \
  [file join $pdk_root lib sky130_sram_1rw1r_128x256_8_TT_1p8V_25C.lib] \
  [file join $pdk_root lib sky130_sram_1rw1r_44x64_8_TT_1p8V_25C.lib] \
  [file join $pdk_root lib sky130_sram_1rw1r_64x256_8_TT_1p8V_25C.lib] \
  [file join $pdk_root lib sky130_sram_1rw1r_80x64_8_TT_1p8V_25C.lib]]
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
