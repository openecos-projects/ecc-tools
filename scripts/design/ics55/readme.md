# ICS55 ECC 脚本说明和用户手册

本文档说明 `scripts/design/ics55` 目录下的 GCD 参考设计脚本、运行方式、输入输出目录和调试开关。该 flow 面向 ICS55 PDK，使用 `ecc_bin` 依次执行 floorplan、fanout 修复、place、CTS、legalization、route、DRC、filler、RCX 和 STA。

## 目录结构

```text
scripts/design/ics55/
├── config/                 # 默认配置模板
├── origin/                 # 默认原始输入模板，当前包含 gcd.v 和 gcd.sdc
├── ecc_bin                 # 可选：本目录默认使用的 ECC 可执行文件
├── run_ecc.sh              # 单步/逐步运行入口
├── readme.md               # 本文档
├── steps/                  # Tcl step 脚本
│   ├── create_workspace.tcl
│   ├── rtl2gds.tcl         # 完整 RTL2GDS 串行 flow 入口
│   ├── step_common.tcl     # workspace、load/save、restore 等公共函数
│   ├── pdk.tcl             # PDK/LEF/Lib/SDC/SPEF 路径定义
│   ├── Floorplan.tcl
│   ├── fixFanout.tcl
│   ├── place.tcl
│   ├── CTS.tcl
│   ├── legalization.tcl
│   ├── route.tcl
│   ├── rcx.tcl
│   ├── sta.tcl
│   ├── drc.tcl
│   └── filler.tcl
└── gcd/                    # 默认 workspace
    ├── home/               # workspace 元数据
    ├── origin/             # workspace 输入
    ├── config/             # workspace 配置
    ├── <step>_ecc/output/  # DEF/Verilog/GDS/idb 持久化数据
    ├── <step>_ecc/report/  # report_db、idb_validate、工具报告
    ├── <step>_ecc/feature/ # feature_summary、feature_tool 输出
    └── <step>_ecc/data/    # 工具工作目录
```

`<step>_ecc` 包括 `Floorplan_ecc`、`fixFanout_ecc`、`place_ecc`、`CTS_ecc`、`legalization_ecc`、`route_ecc`、`RCX_ecc`、`sta_ecc`、`drc_ecc` 和 `filler_ecc`。

## 前置条件

1. 在仓库根目录构建或准备 `ecc_bin`：

   ```bash
   cmake --build build --target ecc_bin
   ```

   `run_ecc.sh` 默认使用 `scripts/design/ics55/ecc_bin`。如果该文件不可执行，会尝试使用仓库下的 `bin/ecc_bin`。也可以通过 `ECC_BINARY=/path/to/ecc_bin` 指定。

2. 准备 workspace：

   默认 workspace home 为 `scripts/design/ics55/gcd/home`。命令行参数优先级高于环境变量：

   ```text
   ecc_bin -script steps/<step>.tcl <workspace>
   ```

   如果未传 `<workspace>`，脚本会读取 `WORKSPACE_HOME`；如果环境变量也未设置，则使用默认 `gcd`。Tcl 内部会通过 `workspace_load -path <workspace>` 解析真正的 workspace root。

3. 准备 PDK：

   `step_common.tcl` 通过 `workspace_get pdk.root` 读取 PDK 根目录；如果 workspace 未设置，则使用脚本中的默认路径：

   ```text
   scripts/design/ics55/../../../../icsprout55-pdk
   ```

   `steps/pdk.tcl` 会在该 PDK 下设置：

   - Tech LEF: `prtech/techLEF/N551P6M_ecos.lef`
   - STD LEF: `IP/STD_cell/.../lef/*.lef`
   - Liberty: `IP/STD_cell/.../liberty/*.lib`
   - SDC: `<workspace_root>/origin/gcd.sdc`
   - SPEF: `<workspace_root>/origin/gcd.spef`
   - RCX corners: `corners/*.map`、`corners/*.itf`、`corners/*.captab`

## 快速运行

进入脚本目录后运行：

```bash
cd /nfs/home/huangzengrong/ecos_cloud/ecc_tool_refactor/scripts/design/ics55

# 完整 RTL2GDS，一次进程内串行执行所有 step
./ecc_bin -script steps/rtl2gds.tcl gcd

# 或者逐 step 启动，每个 step 单独加载上一步输出
./run_ecc.sh all gcd
```

运行单个 step：

```bash
./run_ecc.sh CTS gcd
./run_ecc.sh route gcd
./run_ecc.sh RCX gcd
./run_ecc.sh sta gcd
```

使用其他 workspace：

```bash
./run_ecc.sh all /path/to/workspace
./ecc_bin -script steps/rtl2gds.tcl /path/to/workspace
```

使用其他二进制：

```bash
ECC_BINARY=/path/to/ecc_bin ./run_ecc.sh all gcd
```

## 完整 flow 和单步 flow 的区别

`steps/rtl2gds.tcl` 是完整 flow 入口，当前开关如下：

```tcl
set RTL2GDS 1
set RESTORE_DATA 1
set RTL2GDS_FLOW 1
```

含义：

- `RTL2GDS=1`：完整 flow 模式。`rtl2gds.tcl` 在同一个进程中依次 `source` 所有 step，Floorplan 初始化设计，后续 step 复用内存中的 idb，不会在每个 step 开头重新加载 DEF 或 idb。
- `RESTORE_DATA=1`：只在 `RTL2GDS=0` 时生效。完整 flow 中该值保留为打开状态，但不会触发单步 restore。
- `RTL2GDS_FLOW=1`：被 `rtl2gds.tcl` source 的 step 执行完后不调用 `flow_exit`，直到完整 flow 结束后统一退出。

单步脚本默认由 `step_common.tcl` 初始化：

```tcl
set RTL2GDS 0
set RESTORE_DATA 0
set RTL2GDS_FLOW 0
```

含义：

- `RTL2GDS=0`：单步调试模式。除 Floorplan 外，各 step 会先调用 `step_restore_or_load_design`。
- `RESTORE_DATA=0`：单步默认从上一步输出 DEF/Verilog 重新构建 idb。
- `RESTORE_DATA=1`：单步从上一步 `output/*_db` 持久化 idb 重新恢复数据。
- `RTL2GDS_FLOW=0`：单步结束后调用 `flow_exit`。

## Step 顺序、输入和输出

| 顺序 | Step | 脚本 | 主要输入 | 核心命令 | 主要输出 |
| --- | --- | --- | --- | --- | --- |
| 1 | Floorplan | `steps/Floorplan.tcl` | `origin/gcd.def`、`origin/gcd.def.gz` 或 `origin/gcd.v` | `init_floorplan`、`tapcell`、PDN 相关命令 | `Floorplan_ecc/output/gcd_Floorplan.*`、`gcd_Floorplan_db` |
| 2 | fixFanout | `steps/fixFanout.tcl` | `Floorplan_ecc/output/gcd_Floorplan.def.gz` 或 `gcd_Floorplan_db` | `run_no_fixfanout` | `fixFanout_ecc/output/gcd_fixFanout.*`、`gcd_fixFanout_db` |
| 3 | place | `steps/place.tcl` | `fixFanout_ecc/output/gcd_fixFanout.def.gz` 或 `gcd_fixFanout_db` | `run_placer`、`feature_eval_map` | `place_ecc/output/gcd_place.*`、`gcd_place_db` |
| 4 | CTS | `steps/CTS.tcl` | `place_ecc/output/gcd_place.def.gz` 或 `gcd_place_db` | `run_cts` | `CTS_ecc/output/gcd_CTS.*`、`gcd_CTS_db` |
| 5 | legalization | `steps/legalization.tcl` | `CTS_ecc/output/gcd_CTS.def.gz` 或 `gcd_CTS_db` | `run_incremental_flow` | `legalization_ecc/output/gcd_legalization.*`、`gcd_legalization_db` |
| 6 | route | `steps/route.tcl` | `legalization_ecc/output/gcd_legalization.def.gz` 或 `gcd_legalization_db` | `init_rt`、`run_rt` | `route_ecc/output/gcd_route.*`、`gcd_route_db` |
| 7 | drc | `steps/drc.tcl` | `route_ecc/output/gcd_route.def.gz` 或 `gcd_route_db` | `init_drc`、`run_drc` | `drc_ecc/output/gcd_drc.*`、`gcd_drc_db`、`report/drc.rpt` |
| 8 | filler | `steps/filler.tcl` | `drc_ecc/output/gcd_drc.def.gz` 或 `gcd_drc_db` | `run_filler` | `filler_ecc/output/gcd_filler.*`、`gcd_filler_db` |
| 9 | RCX | `steps/rcx.tcl` | `filler_ecc/output/gcd_filler.def.gz` 或 `gcd_filler_db`、`config/rcx.json` | `init_rcx`、`read_mapping`、`read_corner`、`run_rcx`、`report_rcx` | `RCX_ecc/output/gcd_RCX.*`、`gcd_RCX_db`、`gcd_<corner>.spef` |
| 10 | sta | `steps/sta.tcl` | `RCX_ecc/output/gcd_RCX.v`、`origin/gcd.sdc`、`config/rcx.json` 中的 SPEF | `read_netlist`、`read_liberty`、`read_sdc`、`read_spef`、`report_timing` | `sta_ecc/output/gcd_sta.*`、`output/sta/<corner>/` |

每个 step 的 `output` 目录通常包含：

- `*.def.gz`：保存后的 DEF
- `*.v`：保存后的 Verilog netlist
- `*.gds`：保存后的 GDS，失败时脚本只打印 warning 并继续
- `*_db/`：`save_data` 保存的持久化 idb 数据
- `*.json`：脚本中预留的 `json_save` 路径，当前 `step_common.tcl` 中 `json_save` 被注释，默认不会生成

每个 step 的 `report` 目录通常包含：

- `<step>.db.rpt`：`report_db` 输出
- `<step>.idb_validate.json`：如果当前二进制支持 `idb_validate`，保存后会执行校验并输出该文件
- 工具专用报告，例如 `drc_ecc/report/drc.rpt`

## 从持久化 idb 恢复数据

`save_data -path <output_db>` 会在每个 step 结束时保存 idb。后续 step 在 `RTL2GDS=0` 且 `RESTORE_DATA=1` 时可以通过 `load_data -path <input_db>` 从该目录恢复。

例如希望单独调试 route，并从 legalization 保存的 idb 恢复：

```tcl
# 在 steps/route.tcl 顶部临时打开：
set RTL2GDS 0
set RESTORE_DATA 1
set RTL2GDS_FLOW 0
```

然后运行：

```bash
./ecc_bin -script steps/route.tcl gcd
```

恢复路径由 step 脚本固定指定。route 的输入 idb 为：

```text
gcd/legalization_ecc/output/gcd_legalization_db
```

注意事项：

- 上游 step 必须已经成功生成对应的 `*_db/` 目录。
- `step_restore_data` 会先执行 `flow_init`、`db_init`，再执行 `reset_data` 和 `load_data`。
- 当前 C++ 接口中 `load_data` 内部也会清理现有数据，避免旧 idb 数据叠加到恢复数据上。
- restore 成功后仍会走 `step_save_design`，因此会重新导出 DEF/Verilog/GDS/idb，并运行 `idb_validate`。

## 配置文件处理

每个 step 会读取 workspace 下的配置，例如：

```text
gcd/config/flow_config.json
gcd/config/db_default_config.json
gcd/config/<tool>_default_config.json
```

`step_common.tcl` 中的 `step_prepare_configs` 会把配置里的相对目录展开为绝对路径：

- `Floorplan_ecc`、`fixFanout_ecc`、`place_ecc`、`CTS_ecc`、`legalization_ecc`、`route_ecc`、`RCX_ecc`、`sta_ecc`、`drc_ecc`、`filler_ecc`、`config`、`origin`、`home` 展开到 workspace 根目录。
- `IP`、`prtech`、`corners` 展开到 PDK 根目录。

因此运行脚本后，`gcd/config/*.json` 可能会被改写成绝对路径，这是预期行为。`steps/rtl2gds.tcl` 会先 source `create_workspace.tcl`，重新创建默认 `gcd` workspace 并复制 `config/`、`origin/` 模板；单步 `run_ecc.sh` 不会自动重建 workspace。

## run_ecc.sh 用法

`run_ecc.sh` 用于逐 step 运行，它会为目标 step 创建临时 Tcl runner，并执行：

```text
<ecc_bin> -script <temp_runner.tcl> <workspace>
```

命令格式：

```bash
./run_ecc.sh [WORKSPACE]
./run_ecc.sh [all|STEP] [WORKSPACE]
```

示例：

```bash
./run_ecc.sh
./run_ecc.sh gcd
./run_ecc.sh all gcd
./run_ecc.sh CTS gcd
./run_ecc.sh RCX gcd
./run_ecc.sh route /path/to/gcd
```

支持的 step：

```text
Floorplan fixFanout place CTS legalization route drc filler RCX sta
```

## 常见问题

### ecc binary is not executable

`run_ecc.sh` 找不到可执行的 `ecc_bin`。先构建：

```bash
cmake --build build --target ecc_bin
```

或者指定：

```bash
ECC_BINARY=/path/to/ecc_bin ./run_ecc.sh all gcd
```

### no valid input DEF or Verilog

单步默认从 DEF/Verilog 重建 idb。如果上游 `*.def.gz` 或 `*.v` 不存在，需要先运行上游 step，或切换为 `RESTORE_DATA=1` 并确认上游 `*_db/` 存在。

### no valid input idb data

当前 step 打开了 `RESTORE_DATA=1`，但上游 `*_db/` 目录不存在或路径不正确。先运行上游 step 生成 `save_data` 输出。

### RCX 或 STA 找不到 SPEF/ITF/captab

ICS55 的 `rcx` 和 `sta` 使用 `gcd/config/rcx.json`。检查其中 `mapping_file`、`itf_file`、`captab_file` 和 `spef_file` 是否已经由 `step_prepare_configs` 展开到正确的 PDK 或 workspace 路径，并确认上游 `route`、`RCX` 已成功运行。

### PDK 文件找不到

检查 workspace 中的 `pdk.root`，或确认默认 PDK 路径存在。`steps/pdk.tcl` 依赖 Tech LEF、STD LEF、Liberty、SDC、RCX corners 等路径，任一路径缺失都会导致初始化失败。

### 配置文件被改成绝对路径

这是 `step_prepare_configs` 的预期行为，目的是让工具读取明确的 workspace/PDK 路径。需要恢复默认配置时，重新运行 `./ecc_bin -script steps/rtl2gds.tcl gcd` 或 `tclsh steps/create_workspace.tcl` 以复制模板配置。

### idb_validate 报告异常

优先查看对应 step 的：

```text
<step>_ecc/report/<step>.idb_validate.json
<step>_ecc/report/<step>.db.rpt
```

如果是 restore 模式下出现对象引用、net/pin/via 关联异常，应对比同一 step 的 DEF 重建模式和 idb restore 模式输出，确认 `save_data`/`load_data` 是否恢复了对象引用和名称字段。

## 推荐调试流程

1. 先跑完整 flow，确认基础脚本和配置没有问题：

   ```bash
   ./ecc_bin -script steps/rtl2gds.tcl gcd
   ```

2. 如果需要定位某个 step，先跑到其上游 step，确保 DEF/Verilog/idb 都已生成。

3. 用单步 DEF 重建模式运行目标 step：

   ```bash
   ./run_ecc.sh route gcd
   ```

4. 再打开目标 step 顶部的 `RESTORE_DATA=1`，用 idb restore 模式重跑，比较：

   ```text
   <step>_ecc/output/*.def.gz
   <step>_ecc/report/*.db.rpt
   <step>_ecc/report/*.idb_validate.json
   ```

5. 若完整 flow 成功但单步 restore 失败，重点检查上游 `*_db/` 是否来自当前代码版本，必要时先重新运行上游 step 生成新的 `save_data` 数据。
