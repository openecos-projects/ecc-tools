# iSTA 对外接口

其他工具从本目录调用 iSTA 并取得时序数据。文件按职责划分：

| 文件 | 职责 |
| --- | --- |
| `STAInterface.hpp` / `.cpp` | 共享 STA 会话的生命周期、运行编排、就绪检查和数据导出入口 |
| `TimingData.hpp` | 引脚、时序弧、时钟、Liberty 单元及查找表的公开值类型 |
| `TimingEndpointQualification.hpp` | max 分析端点的逐转换有效性和原因码，供分析模块和消费者共用 |

本目录的 `TimingDataExporter`、`LibertyDataExporter` 和 `EndpointQualifier` 负责从内部数据库生成这些值。
`source/module/TimingAnalyzer` 编排建图、时钟、数据、路径和电气检查；报告与写出由入口按操作调用。消费者无需包含这些实现头。

## C++ 与 CMake 用法

```cmake
target_link_libraries(my_tool PRIVATE ista_interface)
```

无需手工添加 iSTA 内部 include 路径。仅处理已有数据、无需运行 iSTA 的组件可链接头文件目标
`ista_timing_data`；它只提供本目录和 C++20 要求，没有数据库、日志或解析器依赖。
静态链接 `ista_interface` 时仍会链接运行 iSTA 所需的实现库，这些库的编译接口是私有的。

```cpp
#include "STAInterface.hpp"

// 前置条件：调用方已通过项目数据管理器加载设计和库。
auto& sta = ista::STAInterface::getInst();
sta.initSTA(config);  // std::map<std::string, std::any>，现有 init_sta 配置项
sta.writeSdc("effective.sdc");  // 输出当前有效约束，无需先运行时序分析
sta.runSTA();
ista::TimingData timing = sta.exportTimingData();
sta.destroySTA();
// timing 自己持有字符串和数组，仍可读取。
```

共享会话由流程所有者负责初始化和销毁。若流程已执行 `runSTA()`，数据消费者直接调用
`exportTimingData()`，不重复运行或销毁其他工具正在使用的会话。
`destroySTA()` 释放分析资源并重置就绪状态；`destroyInst()` 删除入口单例本身，不能替代资源清理。
入口调用与设计修改需要串行协调；这些类不提供并发会话或线程安全的全局数据库访问。

`isInitialized()` 和 `isTimingReady()` 表示当前入口记录的状态。成功执行 runSTA 或 extractLib 后才能导出；重算开始即撤销就绪状态，失败后禁止导出部分结果，成功后恢复。未就绪时
`exportTimingData()` 抛出 `std::runtime_error`。直接修改 iDB 或内部数据库不会自动通知这些状态，
此时流程所有者需要重新建立有效的分析结果。

重新初始化在首次修改会话数据前撤销 initialized/ready；读入设计或 SDC 失败后，必须重新成功初始化才能分析或写出。纯参数校验若在任何修改前拒绝请求，则保留原会话状态。

`write_sdc effective.sdc [-nosplit]` 同时在项目 Tcl 入口和内部 SDC 解释器注册；两者共用命令实现。
`source/module/sdc/WriteSdc` 处理参数，`source/module/writer/sdc` 从当前有效约束生成 SDC；
校验和写入成功后才替换目标文件。旧 output_sdc 命令已删除，其他尚未支持的选项明确报错。

## 导出数据约定

- `TimingData` 是一次导出的独立值，持有全部字符串、数组及查找表，不保留数据库指针。
  修改返回值不更新 iSTA；之后的分析也不会刷新已经返回的数据。
- 当前 `schema_version` 为 2。结构字段、默认值和编码未随本次目录调整改变。
- 名称连接对象：弧的 `source_pin` / `sink_pin` 对应 `pins[].name`；`library_cell` 对应
  `lib_cells[].name`。`library_arc_id` 是该单元 `arcs` 内的索引，`-1` 表示未建立映射，不能直接索引。
  引脚名和库单元名用于建立调用方映射；不要把本次导出数组的下标当成跨分析会话的永久编号。
- 每个条件检查变体单独导出；检查映射同时区分 CK/D、检查类型、capture edge 和内部条件/表身份。无法唯一对应时返回 `library_arc_id=-1` 并记录诊断，不能退回第一张同名表。条件变体的导出数量可能增加，消费者应按当前导出索引取表。
- `*_ps`、`*_pf` 已分别转换为 ps、pF；`*_dbu` 使用设计数据库单位。
  `time_unit_ps`、`capacitance_unit_pf`、`resistance_unit_ohm` 记录源库一个单位对应的物理量，
  不应再乘到已转换的字段上。单位和寄生来源同时记录在 provenance、SDC/SPEF 路径及诊断字段中。
- Liberty 表的 `values` 是展平数组；二维表按 `axes[0]` 为行、`axes[1]` 为列排列。
  导出器将常规二维 delay/slew 表整理为输入转换时间、负载电容，将 check 表整理为相关转换时间、
  受约束转换时间；时间轴及表值使用 ps，电容轴使用 pF。缺失表保留为空；标量表无轴且有一个值。
- `max_qualification.valid` 和 `reason` 按 **数据转换 `[rise, fall]`** 排列。
  消费者应结合有效性和 `diagnostics` 使用结果，不能仅凭有限的 RAT 数值推断端点已受约束；
  缺少 timing point 或 required time 非有限时，导出器的 RAT 回退值为 `9.0e7` ps；
  `TimingPinData` 的 RAT 字段初值为 `1.0e30`，它与运行时导出回退值不同。

现有整数字段的编码如下；消费者不需要包含内部枚举头：

| 字段 | 编码 |
| --- | --- |
| `direction` | 0 未指定，1 输入，2 输出，3 双向 |
| `arc_type` | 0 未指定，1 cell，2 net |
| `sense` | 1 positive，-1 negative，0 non-unate 或未指定 |
| `trigger` / `timing_type` | 1 rise，-1 fall，0 未指定 |
| `check_type` | 0 none，1 setup，2 hold，3 recovery，4 removal，5 width，6 period |
| `max_qualification.reason` | 见 `TimingEndpointQualification.hpp` 的显式枚举值 |

## 已有消费者与验证边界

仓内 Python 布局桥接从 `PyPlaceDB.cpp` 调用 `STAInterface::exportTimingData()`，
`PyPlaceDBTiming.cpp` 将返回值转换为 Python 数组，包含 DreamPlace 使用的数据。
这条路径应通过 `ista_interface` 依赖公开类型，不依赖 `TimingDataExporter` 或内部 `Database`。

公开头独立编译、`py_imp` 构建和 iSTA 导出测试用于检查接口边界；完整的 DreamPlace 运行验证另行进行。
