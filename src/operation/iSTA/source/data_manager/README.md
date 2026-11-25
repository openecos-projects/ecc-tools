# STA 数据领域与所有权

`DataManager` 继续作为会话单例管理 `Config` 和 `Database`。`Database` 组合各领域所有者；网表容器属于 `Network`，图和传播状态属于 `Graph`，查询对象借用它们实际需要的领域。领域对象没有指向 Database 的回指；Graph 复制时重新绑定顶点到副本的接收/驱动状态，移动保留其拥有的 map 节点。

```text
data_manager/
├── DataManager.cpp/.hpp   会话、配置读取及设计数据整理
├── Database.hpp          组合领域对象与已发布结果
├── Config.hpp            运行配置
├── common/               跨领域共用的分析类型/跳变方向/路径来源/case 值枚举
├── network/              Network、Instance、Pin、Net、HierarchyPin；网表、层次查询和连接整理
├── liberty/              LibertyLibrary、LibertyCell、LibertyPort；库弧、表和逻辑
├── parasitics/           Parasitics、ParasiticNet、节点和电阻；寄生输入数据
├── sdc/                  时钟、I/O 约束、例外及约束查询规则
├── graph/                Graph、顶点、边、索引、次序、传播状态及 TimingStateQuery
├── path/                 已提取的路径、端点、分组及 PathStats
└── check/                FanoutCheck 等检查结果
```

`TimingStateQuery` 是借用 `Database&` 的查询对象，不由 `Database` 持有；其生命周期不能超过被查询的数据库。建图操作由 `module/graph_builder` 负责，查询目标不依赖建图模块。

## 所有者与使用者

| 对象 | 持有的数据 | 主要使用者 |
| --- | --- | --- |
| `Network` | 设计名、实例、pin、net、层次映射 | reader、图构建、约束查询 |
| `LibertyLibrary` | 库单位、cell/port/arc/table 和逻辑模型 | 库读取、延迟计算、逻辑传播、检查 |
| `Parasitics` | 寄生网络、节点、电容和电阻 | SPEF 读取、RC 计算 |
| `Sdc` | 当前已支持的约束和查询规则 | Tcl 命令、传播、路径评价、检查 |
| `Graph` | 顶点、图边、邻接索引、遍历次序、起终点、pin 上的传播状态 | 图构建、时钟/数据传播、路径搜索 |
| `Database` 中的结果 | 路径组、`PathStats`、fanout 检查结果 | PathGroupBuilder/TimingAnalyzer 发布，reporter/导出读取 |

```cpp
auto& network = database.network();
auto& graph = database.graph();
const auto& pin = network.pins().at(pin_name);
auto& source = graph.sourcePoint(arc);
const auto* port = network.libertyPort(database.liberty(), pin);
```

`Graph::sourcePoint/sinkPoint/driverPoint` 保留 inout pin 的 driver/receiver 身份和已有 fallback。`Network::libertyPort` 显式接收库，避免网表对象借用整个 Database。可变集合供读取、构建和计算阶段使用；const 访问表达只读借用，不提供自动失效管理或并发写保护。

图边通过 `LibertyArcId{cell,index}` 绑定库弧，使用当前 `LibertyLibrary::findCellArc` 解析。复制 Database 后不再指向原库；同端口的多个弧集由序号区分。库替换或重排仍需重建图。

## 容易混淆的边界

- `liberty/TimingArc` 是库中定义的时序弧；`graph/Arc` 是实例化设计中的图边。
- `graph/TimingPoint`、`graph/TimingPathState` 保存顶点上 min/max、rise/fall 等传播状态；`path/TimingPathPoint`、`path/TimingPath` 保存提取出的具体路径。
- `path/PathStats` 保存路径统计；`violating_path_count` 和 TNS 沿用当前对负 slack 路径的计数/累加规则，不表示不同违例端点的数量。旧 Summary 和五个空阶段 summary 已删除。
- `sdc/TimingException` 是用户路径约束，`sdc/ExceptionMatcher` 解释匹配规则；`module/path_analyzer/PathSearcher` 负责搜索，`module/timing_checker/PathChecker` 负责具体端点评价。
- `check/FanoutCheck` 保存结果；`module/timing_checker/FanoutChecker` 执行检查。
- `module/graph_builder/LogicSimulator` 只读 Network、LibertyLibrary 和 Sdc，向 Graph 写入 `SimValues` 和有效弧。GraphBuilder 显式运行 LogicSimulator；DataPropagator 只读其结果，不再链接仿真实现。
- `sdc/Sdc` 只保存用户指定的 case 值，`network/Pin` 保存网表常量；`graph/SimValues` 按 pin 保存传播出的 receiver/独立 driver 值和三态关闭状态。未知 inout driver 不回退为 receiver。
- `common` 只放跨领域共享的基础枚举；库单位留在 liberty，pin direction 留在 network，arc type 留在 graph。按具体含义选择目录，避免重新形成杂项集合。

## 输入依赖

| 入口 | 显式接收的 iSTA 数据 | 行为 |
| --- | --- | --- |
| `readConfig` | Config | 读取配置 |
| `readNetwork` | Network | 转换网表；SDC 文件路径由 STAInterface 设置 |
| `readHierarchy` / `linkHierarchy` | Network | 读取或绑定层次映射 |
| `ReadLiberty` | LibertyLibrary、const Network | 转换库；按实例选择参考库 |
| `ReadParasitics` | Parasitics、const Network | 转换 SPEF，使用网表身份解析节点 |

读取器仍通过项目 iDB 单例取得外部输入，不因此支持并发会话。读取次序和单位规则保持不变。内部 helper 用 `read*`、`is*`、`*Name` 等说明具体操作；旧 Import 文件、wrap helper 和 Database 容器 getter 已删除，没有兼容别名或转发头。

## 头文件与构建

所有模型头通过 `data_manager` 根目录查找，跨领域依赖直接写明归属：

```cpp
#include "network/Pin.hpp"
#include "liberty/LibertyCell.hpp"
#include "sdc/Sdc.hpp"
#include "graph/TimingPoint.hpp"
#include "path/TimingPath.hpp"
```

没有 `advance`、`basic`、`adapter`、`constraint` 兼容目录，也不导出每个领域的 include 路径。`ista_model` 是数据头文件集合；计算实现和外部读取依赖分别由以下目标管理：

| 目标 | 编译内容 | 主要依赖 |
| --- | --- | --- |
| `ista_model` | 模型头文件，包括 Graph | 公共类型和现有内联诊断所需日志 |
| `ista_network` | Network 查询 | ista_model |
| `ista_graph_query` | TimingStateQuery：查询图、时钟和路径状态 | ista_model |
| `ista_sdc` | ClockEdges、Sdc、ExceptionMatcher | ista_network |
| `ista_logic_simulator` | LogicSimulator | ista_model |
| `ista_fanout_checker` | FanoutChecker | ista_sdc |
| `ista_data_manager` | DataManager（含配置读取） | 模型、约束及现有日志/工具 |
| `ista_reader` | ReadNetwork、ReadHierarchy、ReadLiberty、ReadParasitics | 外部设计和文件解析库，私有链接 |

读取器统一位于 `module/reader`，由 `ista_reader` 编译。使用模型、约束或检查不需要链接 Verilog、Liberty、SPEF、IDB 解析器。

SDC 借用生命周期见 [SDC 内核说明](sdc/README.md)。PathSearcher 的捕获选择已保存在局部 PathEnd，diversion 仍改写共享路径状态，DelayCalculator 仍有实例内的可变缓存；领域所有权明确不等于它们已可按端点并行。本批不改变计算次序、报告排名、路径配额、数值算法或 DataManager 单例生命周期。完整重算由 TimingAnalyzer 清理上次派生状态，避免旧端点/路径累积。

计算阶段及其数据依赖见 [模块说明](../module/README.md)。

## 时钟与端点评价

- `graph/ClockState.hpp`：每个图点、每个时钟一份有效/物理传播值。TimingPoint 的 defaultClock 是时钟传播选定的引用，launchClock 是标量数据 arrival 的来源；数据传播不会重选默认时钟，查询不会创建缺失状态；`arrival` / `slew` 的 optional 区分未传播和有效零。
- `graph/TimingPathState.hpp`：数据路径传播结果和前驱、launch clock、例外匹配进度，不存捕获时钟。
- `path/PathEnd.hpp`：一次 path/capture 配对的 required/slack 结果，只借用 const 路径，生命周期限于同步 PathSearcher。不能保存到 Database，也不能跨图重建或分支路径修改持有。
- `path/TimingPath.hpp`：输出使用的自有路径值；`PathGroup::endpointPaths()` 按端点直接持有 vector<TimingPath>。旧 TimingPathEnd 包装及其重复端点名已删除。

本次先保持原有两级候选归约：同一路径选捕获时钟，再跨路径比较；ista::kTimingTolerance 比较不满足可任意重组的结合律，未来任务完成顺序不得决定最终赢家。

配置由 `DataManager::readConfig` 直接写入其持有的 Config；独立 ReadConfig 文件及自由函数已删除。STAInterface 仍按“读取配置 → 读取设计 → buildDesign”顺序调用；配置默认值、选项优先级、重复读取和 OpenMP 设置保持原有行为。Database、Config 的独立定义与领域目录不变。

抽取结果 `liberty/TimingModel.hpp` 是抽取算法与 Liberty writer 共享的纯值模型，通过 `ista_model` 使用；不再以算法模块的 include 目录暴露这个合同。

精确路径检查始终携带选中的捕获身份；图点 required 是用于反向传播的摘要，不应代替完整路径评价。
