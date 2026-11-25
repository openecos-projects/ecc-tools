# iSTA

本目录按数据所有权、计算阶段和输入输出边界组织。先读
[目录与计算流程](source/module/README.md)，再从
[STAInterface](interface/STAInterface.cpp) 和
[TimingAnalyzer](source/module/TimingAnalyzer.cpp) 跟踪一次运行。

| 目录 | 目的 | 内容边界 |
| --- | --- | --- |
| interface | 对其他工具的会话入口和独立值结果 | STAInterface、TimingData、数据库到公开值的转换；不执行局部延迟公式 |
| source/data_manager | 数据所有权与固有查询 | Network、Liberty、SDC、Parasitics、Graph、路径/检查结果及 Config；不调用外部解析器 |
| source/module | 设计读写、约束命令和实际计算 | reader/writer、建图、局部计算、传播、约束评价、选路、报告、模型抽取；顶层 TimingAnalyzer 组织计算步骤 |
| source/toolkit | 通用基础设施 | logger/monitor/utility/common；不依赖设计读写或分析模块 |
| test | 行为、所有权、接口及依赖边界验证 | 含正向和故意违反依赖的负向测试 |

SDC 命令位于 module/sdc；约束数据及 ExceptionMatcher 位于 data_manager/sdc；
SDC 序列化位于 module/writer/sdc。`write_sdc file_name [-nosplit]` 通过命令入口调用 writer。
写出的对象是当前有效约束，文件不会包含设计读取、分析、报告或再次写出文件的流程命令。
详见 [接口约定](interface/README.md)、[数据所有权](source/data_manager/README.md)、
[设计读取](source/module/reader/README.md) 和 [格式写出](source/module/writer/README.md)。

## IO-cell boundary connectivity

iSTA and iPW preserve signal nets between top-level ports and IO-cell PADs,
including both endpoints. A routing skip based on LEF `CLASS PAD` is not a
timing or power boundary: these objects carry SDC constraints and activity
annotations, and Liberty can define paths through the IO cell.

Do not delete or rename those objects to bypass INOUT direction checks, move
their constraints to core-facing pins, or silently ignore missing SDC targets.
The graph separates receiving and driving vertices for genuine INOUT pins,
while reports, constraints and activity annotations retain physical names.
Liberty functions, three-state conditions and conditional arcs determine
enabled paths. See the [bidirectional IO implementation](../../../docs/sta-pw-bidirectional-io.md)
for graph roles, driver-specific loads, regression coverage and power-estimation
limitations.

## Power/ground connectivity

iSTA excludes POWER/GROUND terms and nets in its reader adapter. Logic
control pins tied to an explicitly typed supply retain that constant, so an
IO enable connected to power or ground still selects the correct mode. Supply
wiring itself does not become signal timing arcs. This changes only iSTA's
analysis database, not IDB or iPW input. Retained nets reference only retained
pins, so DataManager cannot recreate filtered PG pins when building connectivity.

An ordinary Verilog wire can also represent a supply without carrying a supply
type. iSTA recognizes it when it has instance pins and all of them have the
same LEF POWER or GROUND type, without conflicting PG port types. Mixed nets
with logic pins, and explicitly typed clock/reset/scan/tie-off nets, are not
reclassified. Net names such as VDD/VSS alone are not evidence of supply use.

## Driver fitting and minimum slew

`init_sta -adjust_driver_resistance 1` allows the Arnoldi driver model to adjust
its resistance when the distributed source waveform meets the network-limited
voltage criterion. The adjusted model matches its lumped table crossing to the
distributed source crossing; cell and wire values share the resulting waveform.
`-adjust_driver_resistance 0` (the default) retains the unadjusted driver fit.

The adjustment remains experimental and is off by default. Validate its effect
on the intended designs, particularly networks with large resistance and capacitance,
before enabling it.

Adjustment requires symmetric slew thresholds around a 50% output delay
threshold; other conventions retain the ordinary driver model. Select the
same adjustment policy when comparing with a reference run.

`-min_slew_degradation 0` (the default) uses driver slew in minimum analysis
only when that driver model was successfully adjusted and the receiver has
compatible voltage and slew conventions. Other receivers retain their physical
waveform. `-min_slew_degradation 1` always keeps the calculated receiver slew.
Both policies retain the physical wire delay and its threshold correction.
The former pi-resistance ratio approximation has been removed.

Both integer options are accepted by Tcl, the C++ config map and the Python
dictionary/JSON bindings. Values other than `0` or `1` are rejected at the public
entry points. Empty Python values leave the option unspecified. Configure them
before starting a run; changing internal configuration requires clearing the
delay calculator's caches. Input ports with prescribed waveforms and the DMP
fallback use their existing response models.

These options describe the implemented driver and receiver rules; they do not
establish equivalence with every reference-tool model or version. In particular,
receiver replacement across different voltage or slew conventions still needs
separate validation.

## Effective capacitance and waveform calculation

The Arnoldi calculator fits effective capacitance and driver ramp together,
including when the driver resistance is limited by the library slew. A
residual-sign bracket handles nonlinear table cases where fixed-point iteration
oscillates. Fitting uses bounded iteration and may accept a response at its update
limit. Invalid fits return failure to the existing fallback chain.

The modal solve preserves positive RC time constants without an absolute
eigenvalue floor. Charge integration uses cancellation-resistant expressions,
including for nearly instantaneous modes and strongly shielded loads. Cell and
receiver delays use the same accepted waveform. The tests include independent
RC charge calculations.
