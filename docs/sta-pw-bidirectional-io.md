# iSTA / iPW 双向 IO 分析

## 输入边界

顶层 port 与 IO cell 的 PAD 连接必须保留。iRT 可以跳过这段连接的物理布线，但不能把同一规则直接用于时序或功耗输入：删除它会破坏 `get_ports`、外部时钟、输入输出 delay、输出 load 和 VCD 注解。

IO cell 可以在 LEF 中是物理黑盒，在 Liberty 中仍有完整的接收、输出和三态模型。不能按 `CLASS PAD`、pin 名称或一对一连接删除它，也不能要求用户把整芯片 SDC 改到 core pin 上。Liberty 明确的双向方向优先于旧 LEF 的单向描述。

iSTA 和 iPW 独立持有内部数据，不互相调用。公开 Tcl/Python API 不变；IDB 设计不被改写。

## 图节点与物理对象

`Pin`、`Net`、`Instance` 的名称和数量保持物理含义。SDC、VCD、SPEF、RPT、SDF 仍引用真实名称，不为双向节点拼接虚构 pin 后缀。

跨阶段使用的 `GraphVertex` 放在各工具的 `source/data_manager/advance`：

- 普通单向 pin 使用一个图节点。
- INOUT pin 分别有 load 和 driver 节点，邻接关系、slew、arrival、required、clock state 和前驱不混用。
- cell arc 从接收侧连接输出驱动侧；net arc 从 driver 连接其他 pin 的 load，排除同一物理 pin 自连。
- 图节点引用 Database 持有的 TimingPoint；图存活期间不能单独清空或替换这些状态容器。

```text
输入：top port(driver) -> PAD(load) -> IO 接收输出 -> core
输出：core -> IO 数据输入 -> PAD(driver) -> top port(load)
```

这种物理 pin / 图角色分离方式参考 OpenSTA 的 `graph/Graph.cc` 中 `makePinVertices()`、`makeWireEdge()`。计算图的遍历、环检查和拓扑排序使用节点索引；报告和接口仍映射回物理 pin。

## 模式与常量

Liberty parser 保存 `three_state` 字符串，包括 bus 属性继承和对象移动。Interface 将 `function`、`three_state`、timing arc 的 `when` 转为工具自己的 `LogicExpression`，后续算法不读取外部 Liberty 对象。

`three_state` 为高阻条件：真表示关闭输出。不能按 OE、IE 等名称猜极性。GraphBuilder 在角色和连线构建后、环检查和拓扑排序前传播常量并选择有效弧：

- 接收值与内部驱动值分别保存，不能把内部常量输出覆盖到外部输入角色。
- 已知高阻的 driver 不传播；未知使能保留可能的路径，不随机选一个 driver，也不将未知当成关闭。
- 所有候选驱动均提供一致常量时才向负载传播该常量；不能只取第一个候选的值。
- 过滤已知为假的 `when`，延迟查表也使用同一条件，避免被关闭的表格参与最大/最小值选择。
- CK->Q 的输出 `function` 通常引用寄存器内部状态，不能用组合逻辑对 CK 的敏感性判断来关闭它。其显式 `when` 和 case 条件仍生效。
- 普通多个无条件输出短接仍沿用各工具原来的多驱动诊断，不把真实错误伪装成双向端口。

POWER/GROUND 网络不参与信号时序。连接到显式电源地网络的逻辑控制 pin 仍保留为常量输入；PG term 不进入信号图。不按 VDD/VSS 等名称推断常量。

## 传播与负载

iSTA 的 ClockPropagator、TimingPropagator、TimingAnalyzer 使用相应图角色；时钟前驱和数据前驱可区分同名物理 pin 的两个状态。外部时钟经 IO 接收路径到达寄存器，输出约束仍在原顶层端口检查。

两个 DelayCalculator 都显式接收当前 driver。负载不包含 driver 自身的接收 pin 电容，物理寄生电容仍保留。RC 缓存检查当前根节点；切换 driver 后重新构建相关负载、延迟与响应，不能因为缓存包含该节点就认为根未变化。

iPW 的物理 pin 索引和 VCD 匹配保持不变，另存双向 driver 的活动。外部输入活动与 IO 内部输出活动不互相覆盖。输出关闭时不把外部 PAD 活动归入该实例的输出切换功耗；未知模式按使能活动估计，缺少使能活动时告警并不归属该项切换功耗。实例功耗和电容不因两节点表示而重复累计。

当前无 VCD 的布尔向量模拟器把整个 net 合为一个状态，不适用于双向节点。遇到 INOUT 使用现有解析活动传播，并记录该选择。它是活动估计，不是对任意总线争用和模式相关性的精确模拟；本修复不宣称功耗数值与商业工具对齐。

解析活动的顺序逻辑迭代有次数上限，未收敛时保留告警，生成的功耗不能视为已收敛的精确结果。

## SDC 查询

iPW 内部补齐 `get_fanin` / `get_fanout` 的 `-flat`、边界和 `-only_cells` 查询，并支持 `get_name`。查询发生在建图前，使用内部 net/pin/Liberty arc；不提前执行主流程，不忽略未知命令。对已解析对象取名字直接查索引，避免每次重扫全设计。

两工具的查询均识别 Liberty 的 INOUT 方向。新增查询没有把 setup/hold 分析模块复制到 iPW。

`get_ports` / `get_nets` 返回的 Tcl 元素保留对象类型，显示名称不变。`set_load` 对已解析对象按类型和完整名称赋值，不再把同一端口名重新匹配成其他层次的同名 net，也不同时施加 port load 和 net load。变量、`foreach`、`lindex`、`concat` 保留该身份；普通字符串参数按端口优先解析。集合类型只属于 SDC 模块，不持有 Database 对象指针。

## 回归与验证边界

可独立构建并运行：

```sh
cmake --build build --target ecc_bin test_ista_io_boundary test_ipw_io_boundary test_ista_power_net test_ista_sdc_compatibility test_ista_bidirectional_graph test_ipw_bidirectional_graph
ctest --test-dir build -R '^(ista|ipw)_(io_boundary|power_net|sdc_compatibility|bidirectional_graph)$' --output-on-failure
```

回归覆盖原端口约束、VCD、物理索引、PG 常量控制、三态输入/输出/未知模式、条件弧、外部及 generated clock、CK->Q 保留、真实名称、无伪自环，以及更换驱动根后的寄生负载和延迟。测试使用独立的 cell/pin 名称，不依赖某个 PDK 的命名。

完整复现使用 `STA_inout_pin_direction_report_debug` 提供的原始网表/DEF、SDC、IO Liberty 和新版 INOUT IO LEF。README 中“PAD 不在 Liberty timing port 中”的判断与实际库不符，PAD 在库中确实有双向模型。复现结果需同时检查流程退出状态与实际输入/输出路径，不能只验证 init 或没有报错。

已验证的结果：

- `ecc_bin`、`ecc_py` 和六项 CTest 回归构建通过，六项回归全部通过。
- 原始网表与原始 DEF 分别使用未修改的 SDC 完成 STA、RPT、SDF 输出及销毁，退出码均为 0。
- 当前报告配置输出的八份 `timing_*.rpt` 在网表与 DEF 两种输入间逐字节一致。max/min 的 in2reg、reg2out、reg2reg 均有路径，in2out 均为空；这是本复现的检查结果，不代表全量时序精度对齐。
- 原始网表完成 iPW，退出码为 0。`instance_power.tsv` 有 253547 个不同实例，所有数值有限，各实例功耗分项与总和一致；顺序活动估计未收敛的告警仍保留。
- Python 小型真实 IO 库用例在同一进程依次执行两个工具的 init/run/destroy，检查实际输入、输出路径及功耗文件。

本次范围不包括重新运行 ECC/DreamPlace 的完整 placement flow，也不等同于全量时序精度收敛；相关调用方仍通过原接口使用物理对象名称。
