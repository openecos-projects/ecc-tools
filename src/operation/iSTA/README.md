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

## Driver and receiver waveforms

The distributed calculator uses a linear voltage ramp behind a source resistance.
It obtains the physical transition span from the library slew and derate. For a
trial effective load C, it matches the local load derivative of the interpolated
delay table together with that span. The derivative comes from the active table
segment, including its extrapolation segments and axis/unit conventions. The
delay at C sets the time origin, so translating a delay table
does not change resistance, ramp width, effective load, or receiver slew.

Asymmetric thresholds can admit multiple parameter pairs. A 64-interval search
brackets the first detected solution in increasing resistance, then bisection
solves it. This finite search does not establish uniqueness or guarantee finding
an arbitrarily narrow solution interval. The output crossing derivative follows
directly from the linear-ramp RC response; no finite load perturbation is used.

For an identifiable driver, effective capacitance equates the distributed source
threshold time with its lumped RC counterpart. At convergence the source delay
agrees with the library delay at that load. Every changed resistance rebuilds
the modal response.

Discontinuous table sensitivities or inconsistent delay/slew observations can
prevent that model from converging. In that case threshold matching restarts at
total load with one fixed source resistance and its corresponding modal response.
The local delay
slope estimates resistance through the slow-ramp first-moment limit, delay = R*C;
the measured RC slew bounds it. Nonpositive slope uses the slew boundary as an
explicit approximation. This does not identify a unique driver from flat delay
data. The ramp then follows the library transition; an infeasible transition
uses the minimum RC span. Its finite ramp is the longest one whose upper
remaining-voltage crossing is at the ramp end. This restart preserves the library
delay at its converged effective load even when the local delay derivative cannot
identify a source. If threshold matching has no feasible root, a separate
charge-equivalence solve restarts: effective capacitance is delivered charge
divided by source voltage change over the ramp. In particular, a slow remote
pole can make a late threshold crossing infeasible within the total-load bound;
that bound is not accepted as a threshold-matching solution.

All residual solves keep capacitance in (0, Ctotal] and require relative error
below sqrt(double epsilon). No partially converged joint model or jump at a
table knot is accepted. Analytic inverse bounds are checked after rounding.
The stopping criteria use floating-point precision and relative load residuals.

Rise and fall calculations use the library's actual voltage thresholds. Each
receiver retains its own physical waveform in both minimum and maximum
analysis. Charge integration avoids cancellation for small time constants and
strong shielding. Synthetic tests cover analytical RC crossings, library-delay
consistency, first moments, arrival translation, asymmetric thresholds, and unit
scaling.

The RC reduction uses a capacitance-weighted Lanczos basis with at most five
vectors. Two reorthogonalization passes suppress repeated modes caused by
roundoff. Breakdown is relative to the operator response norm and dot-product
roundoff, so scaling every RC time does not change the reduced dimension. Tests
compare a complete four-node nodal solution with the reduced response over twelve
orders of magnitude in resistance.

Receiver thresholds observe the calculated voltage without changing the
passive circuit. A receiver whose threshold cannot be reached is marked failed;
reachable siblings retain their delays and transitions. Input-port crossings
grow their root bracket from the ramp or fastest observable time constant, so
an almost unobservable slow mode does not set the root's time resolution.

`adjust_driver_resistance` and `min_slew_degradation` remain accepted as legacy
0/1 configuration fields in Tcl, C++ and Python. They have no effect on the
physical model. The configurable `small_net_limit_ps` approximation defaults to
2.0 ps; zero disables it. This is a budget on total R times total C, not a delay
error bound. Eligible networks use the cell tables at total capacitance and a
first-moment one-pole response at each receiver. See the
[small-network model](../../../docs/ista-small-network.md) for details.

SPEF import preserves finite nonnegative resistances and small positive
capacitances. Negative or nonfinite resistance is rejected. Zero-resistance
branches identify the same voltage: the reduced network contracts these nodes,
sums their capacitances, and retains each terminal and parallel conductive branch.
