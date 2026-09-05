// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace eccdb::lef_test {

struct LefPdkDomain
{
  std::string name;
  std::filesystem::path technology;
  std::vector<std::filesystem::path> cells;
  std::string known_master;
};

inline std::vector<LefPdkDomain> fullSky130Corpus(const std::filesystem::path& source_root)
{
  const auto lef_root = source_root / "scripts/foundry/sky130/lef";

  // HD and HS have overlapping layer names but different technology rules and
  // SITE definitions, so they remain separate database domains.
  return {
      {.name = "Sky130 HD, IO, SRAM, and filler",
       .technology = lef_root / "sky130_fd_sc_hd.tlef",
       .cells = {lef_root / "sky130_fd_sc_hd_merged.lef",
                 lef_root / "sky130_ef_io__com_bus_slice_10um.lef",
                 lef_root / "sky130_ef_io__com_bus_slice_1um.lef",
                 lef_root / "sky130_ef_io__com_bus_slice_20um.lef",
                 lef_root / "sky130_ef_io__com_bus_slice_5um.lef",
                 lef_root / "sky130_ef_io__connect_vcchib_vccd_and_vswitch_vddio_slice_20um.lef",
                 lef_root / "sky130_ef_io__corner_pad.lef",
                 lef_root / "sky130_ef_io__disconnect_vccd_slice_5um.lef",
                 lef_root / "sky130_ef_io__disconnect_vdda_slice_5um.lef",
                 lef_root / "sky130_ef_io__gpiov2_pad_wrapped.lef",
                 lef_root / "sky130_ef_io__vccd_hvc_pad.lef",
                 lef_root / "sky130_ef_io__vccd_lvc_pad.lef",
                 lef_root / "sky130_ef_io__vdda_hvc_pad.lef",
                 lef_root / "sky130_ef_io__vdda_lvc_pad.lef",
                 lef_root / "sky130_ef_io__vddio_hvc_pad.lef",
                 lef_root / "sky130_ef_io__vddio_lvc_pad.lef",
                 lef_root / "sky130_ef_io__vssa_hvc_pad.lef",
                 lef_root / "sky130_ef_io__vssa_lvc_pad.lef",
                 lef_root / "sky130_ef_io__vssd_hvc_pad.lef",
                 lef_root / "sky130_ef_io__vssd_lvc_pad.lef",
                 lef_root / "sky130_ef_io__vssio_hvc_pad.lef",
                 lef_root / "sky130_ef_io__vssio_lvc_pad.lef",
                 lef_root / "sky130_fd_io__top_xres4v2.lef",
                 lef_root / "sky130_sram_1rw1r_128x256_8.lef",
                 lef_root / "sky130_sram_1rw1r_44x64_8.lef",
                 lef_root / "sky130_sram_1rw1r_64x256_8.lef",
                 lef_root / "sky130_sram_1rw1r_80x64_8.lef",
                 lef_root / "sky130io_fill.lef"},
       .known_master = "sky130_sram_1rw1r_128x256_8"},
      {.name = "Sky130 HS",
       .technology = lef_root / "sky130_fd_sc_hs.tlef",
       .cells = {lef_root / "sky130_fd_sc_hs_merged.lef"},
       .known_master = "sky130_fd_sc_hs__a2111o_1"},
  };
}

inline std::vector<LefPdkDomain> fullIhp130Corpus(const std::filesystem::path& source_root)
{
  const auto libraries = source_root / "scripts/foundry/ihp130/ihp-sg13g2/libs.ref";
  const auto stdcell = libraries / "sg13g2_stdcell/lef";
  const auto io = libraries / "sg13g2_io/lef";
  const auto sram = libraries / "sg13g2_sram/lef";

  // sg13g2_io_notracks.lef contains the same Macro names and is an alternative
  // view, not an additional library.
  return {{.name = "IHP130 standard-cell, IO, and SRAM",
           .technology = stdcell / "sg13g2_tech.lef",
           .cells = {stdcell / "sg13g2_stdcell.lef",
                     io / "sg13g2_io.lef",
                     sram / "RM_IHPSG13_1P_1024x16_c2_bm_bist.lef",
                     sram / "RM_IHPSG13_1P_1024x64_c2_bm_bist.lef",
                     sram / "RM_IHPSG13_1P_1024x8_c2_bm_bist.lef",
                     sram / "RM_IHPSG13_1P_2048x64_c2_bm_bist.lef",
                     sram / "RM_IHPSG13_1P_256x48_c2_bm_bist.lef",
                     sram / "RM_IHPSG13_1P_256x64_c2_bm_bist.lef",
                     sram / "RM_IHPSG13_1P_4096x16_c3_bm_bist.lef",
                     sram / "RM_IHPSG13_1P_4096x8_c3_bm_bist.lef",
                     sram / "RM_IHPSG13_1P_512x64_c2_bm_bist.lef",
                     sram / "RM_IHPSG13_1P_64x64_c2_bm_bist.lef"},
           .known_master = "RM_IHPSG13_1P_4096x16_c3_bm_bist"}};
}

}  // namespace eccdb::lef_test
