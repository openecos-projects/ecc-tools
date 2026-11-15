# SDC 内核

`Database` 拥有 `Sdc`，生命周期继续由现有 DataManager 单例管理。Tcl 命令负责解析参数和更新约束；计算模块读取同一个 Sdc。

| 类 | 职责 | 依赖 |
| --- | --- | --- |
| `Network`（相邻 network 目录） | 拥有设计连接和层次映射，查询层次边界和 launch transition | 不读取 Sdc 或传播结果；库端口查询显式接收 const LibertyLibrary |
| `Sdc` | 保存现有约束；解析时钟 source、层次时钟替换边界和有效 fanout 限值 | 规则查询接收 const Network，fanout 查询还接收 const LibertyLibrary |
| `ExceptionMatcher`（本目录） | 有序 through 状态、端点匹配、clock groups 和例外优先级 | 借用 const Sdc + const Network；路径状态由调用者持有 |
| `LogicSimulator`（module/graph_builder） | case 值传播和有效弧更新 | 只读 Network/LibertyLibrary/Sdc，写 Graph::simValues 和弧状态；图构建显式调用 |
| `FanoutChecker`（module/timing_checker） | 汇总 fanout load、应用限值、排序检查结果 | 借用 const Sdc + const Network + const LibertyLibrary + const SimValues，返回结果 vector |

`ista_sdc → ista_network → ista_model`，`ista_fanout_checker → ista_sdc`。这些计算库不链接 DataManager、Tcl、Search 或外部设计读取器；通用 toolkit 不再包含路径异常匹配。

```cpp
const auto& network = database.network();
ExceptionMatcher exceptions(database.sdc(), network);
ExceptionThroughIndex through(database.sdc(), network);
auto state = exceptions.initState(start, clock, transition, analysis, launch_transition);
state = exceptions.advanceState(through, state, pin, transition);
auto checks = FanoutChecker(database).check();
database.fanoutChecks() = std::move(checks);
```

Network 本身拥有网表；ExceptionMatcher、FanoutChecker 按 const 引用借用数据，不复制网表。被借用的 Sdc/Network/LibertyLibrary/SimValues 必须比查询对象活得更久；查询期间不要修改或移动它们。ExceptionMatcher 返回的例外指针指向 Sdc 内部条目，修改例外容器后应重新查询。FanoutChecker 不写数据库；TimingAnalyzer 在 PathGroupBuilder 发布之后写入 fanout 结果，与原有成功分析的顺序一致。

这一批保留原有时序和 fanout 算法，包括缺失库端口的 fallback、外部端口的既有 fanout 处理和最终排序；不增加 `set_fanout_load`、slew/capacitance 检查或并行执行。Sdc 中两个派生 case map 已删除，SimValues 保存计算结果；LogicSimulator::propagate 在每次计算前清理所有逻辑结果，随后按原次序求值和更新有效弧。

`Sdc::clearConstraints()` 重置全部约束并保留源文件路径；`readSdc` 执行会话脚本，`STAInterface` 在读取前清理 SimValues。重读约束后仍需要完整构建和分析；这不是自动增量更新，图弧、拓扑次序和其他时序状态不会仅因约束 setter 调用而全部刷新。

旧 `TimingConstraint`、`HierarchyConstraints` 和 `TimingExceptionMatcher` 已移除。调用者直接使用上述领域类，不保留兼容别名或旧头文件。

`test_ista_sdc_core` 无需初始化单例，检查手算的 fanout 负载/限值/顺序、重复求值、设计间隔离和例外优先级。`test_ista_domain_ownership` 覆盖独立 Network/Graph、复制/移动、inout driver/receiver、层次查询和 falling-edge launch 匹配。层次和 SDC 全流程由 `test_ista_hierarchy_sdc` 与 `test_ista_sdc_compatibility` 覆盖。

`test_ista_logic` 使用 const 输入领域运行真实逻辑传播，覆盖反复约束修改、用户/网表常量、未知/冲突多驱动、独立 inout 值、三态恢复、条件弧及顺序单元时钟弧；`test_ista_arc_binding` 覆盖图和库的复制/移动/扩容及重复端口弧身份。

`ExceptionThroughIndex` 拥有分层候选名字，匹配时不持有数据库内的字符串地址；约束或层次连接改变后必须重建。StartpointInitializer、DataArrivalPropagator 和 PathAnalyzer 在阶段入口重建，PathSearcher 借用本次稳定索引。调用 `initState` 必须提供实际 launch edge，状态 tag 也保留该边沿。

I/O delay 仅有输入/输出两组规范记录。标量 getter 是摘要，不能用于还原某个数据边沿/捕获时钟的约束；输入与输出的代表时钟分别查询，避免 inout 端口互相覆盖。精确播种/检查只消费匹配记录；缺少输出边沿的普通检查不生成路径，显式匹配的 min/max delay 例外仍可约束它。既有同边沿 min-from-max 政策仍保留，未声称其与所有 reference 场景等价。
