# EccDB 原生 Service 层（2026-10-07）

此版本共用借用 Store 的 `DatabaseContext`。业务修改入口为 Instance、Connectivity、Floorplan、Constraint、Routing 五组；Geometry 是原生查询/修改支撑与 session 编辑机制，不能把六个目录理解成六个同级业务领域。Service 负责关联操作；Storage 保持原生实体、几何 payload、索引和局部 CRUD。只读调用可直接使用 Storage 的引用、span、view 或业务查询，不再复制一套 getter 数据库。

## 业务分组

| 分组 | 入口 | 关联职责 |
|---|---|---|
| Instance | `instance/InstanceService.h` | 创建/删除/放置/移动/重命名；compatible master swap；清理自身 Term 连接、PA、Group/Blockage 引用；失效 occurrence Bounds |
| Connectivity | `connectivity/ConnectivityService.h` | Net/BTerm 生命周期、regular/special 两类连接、单端点重连和完整端点集替换；source-Net 范围和 MUSTJOIN 约束 |
| Geometry 支撑 | `geometry/` 下 Query/Payload/Occurrence/HitTest/Edit/PhysicalDesign | 原生批量查询、精确几何、层次 occurrence、共用 owner mutation 后处理、Fill 生命周期与 session 删除恢复 |
| Routing | `routing/RoutingService.h` | 设计 VIA、Wire/Shape CRUD；普通 Net 的 routed 部分替换；Guide、Track Assignment、PA candidate/group/preference |
| Floorplan | `floorplan/FloorplanService.h` | Die/Core、Row/Track/GCell、BPin 物理 CRUD/选定或全部 PORT 替换、Instance halo；批量替换和几何/PA 生命周期 |
| Constraint | `constraint/ConstraintService.h` | Region/Group/Blockage CRUD、成员替换与 region 分配；删除时清理引用并销毁所属几何 |

旧 `service/query/` 已删除。`NetRouteService`、`DesignViaService`、`RoutingContextService` 已由一个 `RoutingService` 替代，调用方使用同一原生 Context，不保留旧类 alias。

Geometry 的多个入口属于同一业务组，各自保留不同工作负载的批量查询或编辑接口，不加一个重复包装所有方法的总 façade。`RouteShape`/`NetRouteSnapshot` 是路由器边界上的物理投影，不是第二份 canonical Storage；原始 mask、path metadata 等由 native Shape/attributes 保存并用于 DEF 导出。

GUI 的 `ModelEntity` 层次面板投影移到 `viewer/DesignModelAdapter`。Python 中原有 `DesignModelService` 名称暂保留，但对应的是这个 viewer adapter；颜色、相机、selection 和工具状态不进入业务 Service。

## Context 和通知

`DatabaseContext(design)` 支持仅处理 Design 的工具；`DatabaseContext(technology, library, design)` 提供完整跨库引用。Store 必须比 Context 活得更久，Context 必须比持有它的 Service 活得更久。Database 的 owning load/write façade 在 State 中持有稳定 Context；Service 不包含 API 的私有 State。

`DatabaseChange` 提供 domain 位集、借用的 Instance/Net/Shape ID span、被整体替换的 geometry owner 和永久删除的 Shape span。观察者同步消费这些 span，若需要保存则自行复制。`notifyAfter` 在成功修改后推进 runtime revision；修改失败不产生成功通知。删除的 ID 仅用于变化描述，旧对象只能在适用的 `beforeChange` 中读取。观察者不得重入业务修改；GeometryEdit 的内部 observer 清理 detached journal 保留槽位，并在永久删除成功后修正剩余撤销项的 owner 序号。

单线程版本没有锁、线程池或并发读写保证。直接调用 Storage 的原语不会自动产生业务通知；编辑器/工具需要关联语义时应使用 Service。不要同时绕过 Service 修改关系又假定 Context revision 已同步。

原生引用、span 和 EnTT view 是借用访问，不能跨越对应 Storage 的结构修改长期保留；packed component 的删除可能移动其他 component，即便它们的 ID 未变。跨修改保存关系使用 ID，修改后重新取引用。Python 的批量数组持有 detached batch，不借用可移动的 EnTT 内存。

## 关键生命周期

**Instance 删除**：先发现受影响的 Net、Group 和 component-linked Blockage；清引用、preferred PA 与 group selection，仅断开这个 Instance 的 ITerm，然后删除 Instance。共享 MasterTerm/Port 和同网其他 Instance 保留。若它是 MUSTJOIN 的端点，需要先删除该 MUSTJOIN Net，不能留下不合法的空网。独立 Net 的现有 Wire 不随 Instance 删除或移动自动 reroute。

**Compatible master swap**：名称、方向、用途匹配后保留原 ITerm ID 和两类 Net 连接，更新 master-term 引用及顺序，清实例派生 PA，失效 Bounds。不通过销毁旧 ITerm 再造新 ID 来实现。

2026-10-08 起 ITerm 是 DesignRegistry 中的独立 entity；Instance 上的 `DesignInstanceTerms` 只保存有序 TermId 和派生名称索引。Instance 删除最后级联销毁这些 entity 与其附加 Component；Net 和 PA 仍通过完整 TermId（含 generation）引用。Term payload 不重复保存自身 ID，旧单调 OID/指针表已移除。名称和 runtime pointer cache 在 schema 13 恢复时重建，详见 [原生接口](Native_API.md#instanceterm-身份与所有权)。

**Net 删除**：断所有本 Net 同类型关系，销毁其 Wire、standalone Net geometry、Guide 和 Track Assignment。BTerm 是独立逻辑对象，保留；另一个 Net 类型的连接保留。

**BTerm 删除**：断 regular/special 连接，销毁所属 BPin、pin geometry 和该 BPin 的 PA。它不销毁被连接的 Net 或共享库端口。

**BPin 物理编辑**：Floorplan 的 create/update/destroy 不重建或断开 BTerm。update 保留 BPin ID，替换旧几何并失效该端口 PA/Bounds/journal；selected PORT replacement 保留未选端口 ID 和相对顺序。所有新端口成功 staging 后才退休指定旧端口；全部替换支持 implicit PORT 到 explicit PORT 的有意转换。失败保留旧端口和连接。

**Fill 生命周期**：Geometry 原生编辑入口调用 FillStorage 的 owner CRUD。update 保留 Fill ID，旧 detached Shapes 不能恢复到新 Fill；删除 Fill 会释放 canonical children 和旧 journal 槽位。不同于临时隐藏单个 Shape 的 session removal。

**Region 删除**：先解除 Instance 和 Group 的 named-region 关系，再删 Region 和其几何。Group 删除不删除成员 Instance；Blockage 删除不删除其引用的 Instance。

**Shape session 删除**：payload 脱离 canonical owner，ID 槽位保留在 GeometryEdit journal；restore 保持 ID 和 owner 顺序。父对象被另一业务 Service 删除、或同 ID owner 的 child collection 被替换后，journal 释放旧槽位，拒绝把旧几何混入新状态。恢复成功后释放 detached payload 副本。保存的设计不包含已移除 Shape。跨父对象替换的全局 undo 历史由上层命令系统负责。

**Tracks 修改**：仅在成功修改后清理派生 PA catalog/group/preference 和实际 Net Track Assignment；用 EnTT storage 的批量清除保留全部实体 ID。失败的批量 staging 不改变旧 PA/Assignment。Guide 和已完成的 Wire 不删除。

**Guide/TA 提交**：`replaceGuideStage(net, optionalGuides, assignments)` 同时准备新 Guide 和新 TA；任一输入失败，两组旧几何都保留。`nullopt` 保留 Guide，只换 TA；单独 `replaceGuides/clearGuides` 成功后清掉同 Net 旧 TA。其他 Net 和 Wire 保留。

**PA 提交**：`replacePinAccess(DesignPinAccessUpdate)` 按明确的 master-port/BPin/Instance 范围提交完整候选、selection 和 preferences。空 payload 清该 owner，未包含 owner 保留；依据 proposed catalog 检查引用，不通过“先清所有旧数据、再逐个写入”实现。输入错误时旧候选与选择保留。

**路由结果提交**：`commitRoutedResults` 借用 native Shapes 与可选并行 attributes，明确区分 Preserve/Clear/Replace/Skipped/Failed。所有新 Wire staging 成功后，只退休请求 Net 的旧 Routed；Fixed/Cover/Shield、special、其他 Net 保留。空结果不等于清旧路由，必须显式 Clear；一次成功批次推进一次 Context revision。单线程 staged commit 不提供并发隔离或全库事务。

## 批量边界与性能

- 单端点 connect 在 detach 前预留目标列表容量，修改后使用栈上受影响 Net 数组，不复制整个 Net。
- 完整端点替换先验证输入和 source scope，每个 source survivor 列表只过滤一次；删除 source component 后重新取得 target 引用，避免 EnTT swap/pop 移动造成悬空引用。
- 高扇出通知 ID 批量收集后 sort/unique，不逐项线性去重。
- Row 批量替换用名称 hash 匹配旧 Row，同名 ID 保留；新分配全部成功后再更新/删除，不用临时命名反复全库扫描。
- Routing replacement 在新 Wire 中 staging，全部 Shape 写入成功后删除旧 routed Wires；Fixed/Cover/Shield 不动。一次成功调用通知一次，不在 Shape hot loop 加通知或时钟。
- Query/Payload 的连续 batch 是可丢弃的读取结果，用于跨语言/渲染批量传输；canonical geometry 仍只在原生 Storage 中保存。

## 最小原生用法

```cpp
#include "context/DatabaseContext.h"
#include "connectivity/ConnectivityService.h"
#include "instance/InstanceService.h"
#include "routing/RoutingService.h"

eccdb::DatabaseContext context(technology, library, design);
eccdb::InstanceService instances(context);
eccdb::ConnectivityService connectivity(context);
eccdb::RoutingService routing(context);

const auto net = connectivity.createNet(eccdb::DesignNet{.name = "N1"});
connectivity.connect(input_term, net);
const auto wire = routing.createWire(eccdb::DesignWire{.net = net});
// 返回原生 ID；实际 payload 可以通过 design 的 Storage 直接读取。
```

加载与保存由原生 `Database` 持有容器负责，旧 `api/` 与重复 DTO/Ref 已删除；业务 Service 直接使用 Storage 中的类型与 ID。当前 Design binary schema 为 13，独立 InstanceTerm 的恢复策略见 [原生接口](Native_API.md)。这不意味着所有 Operation 的内部工作数据库均已迁移；RC/STA 模型仍是独立后续范围。

## Operation 覆盖复核（2026-10-08）

当前第一版业务组织尚非九个 Operation 的完整替代。只读数据可直接取 native Storage；写回应进入共同 Context 的业务边界。iRT/iDRC 有可注入 native adapter；本轮已把 iRT Track/GCell 输出接到 Floorplan、非空详细布线结果接到 Routing 多 Net 提交，仍未补其 routing-blockage 输入。iDRC 全量输入仍漏 blockage/standalone Net geometry，并缺 unplaced 过滤。其他七工具尚无完整 native adapter。

按 EccDB 原生业务梳理后，首版重点是既有 BTerm 下 BPin 图形编辑、Fill owner 生命周期、route result 的修改范围/失败策略、CTS 连贯写回和 native adapter。完整 Instance 可一次 create，不强制拆成 create/place/setSource；Site 选择可先作为工具配置；批量 placement、Site 持久化、standalone special geometry 写入口按实际场景决定。`swapMasterCompatible` 不支持任意不兼容端口替换。

分析工具需要 driver/代表坐标策略、外部 provider 和 native 输入适配；新 power artifact 使用 native 身份与来源约定，不强制保留 iDB ID。LVS 传两个 Context/read view；首版提交后全量重建工具模型即可，不将 timing/RC 私有模型塞入物理库。

需求基线见 [daily 原生业务需求](</home/zhuxu1/workspace/daily/2026年10月8日/ECCDB_Operation_Native_Requirements_20261008.md>)；逐工具源码证据见 [daily 覆盖报告](</home/zhuxu1/workspace/daily/2026年10月8日/ECCDB_Service_Operation_Coverage_Audit_20261008.md>)。此前只读覆盖复核与本轮实施需分开看：本轮改动及验证记录在实施日志中，未跑九工具端到端；Service 单元测试/DRT 适配测试通过不能替代该验证。

## 2026-10-08 的层级修正与实施

用户指出服务职责重叠与抽象层级不一致，两个 agent 复核后，撤销早先“按 Storage 拆平级 PinAccessService/GuideService”的规划。现有 RoutingService 仍包含 Wire/Via、Guide/Track Assignment、PA；先保留 Routing 业务归属，可以内部拆模块，正常工具写回应提供完整阶段结果和引用一致性命令，不能复制一组 has/get/set/clear façade。TrackGrid 归 Floorplan；Track Assignment 归 Routing。

目标层级是 Storage/原生 owner mutation、只读 Query、业务 writer、工具/编辑命令编排。Instance/Connectivity/Floorplan/Constraint/Routing 五个业务 writer 同层，统一处理一项完整设计修改及关联维护。现有 Geometry Query/Payload/Occurrence/HitTest/PhysicalDesign 属只读支撑，GeometryEdit 混有原生 mutation 与 session journal；现有六目录组织不能被理解成六个同级业务领域。Fill owner CRUD 在原生 Geometry mutation 支撑模块补齐，不归 session journal，也不强塞到 Routing/Floorplan/Constraint。

外部 TritonRoute 已实际读写 PA/Guide，是必须保留的消费者；不能把本地 iRT 尚不回写的判断推广到外部 DRT。同步迁移 RouteAdapter/ContextAdapter、native runner 与 replay/benchmark 的真实调用；Guide 更新开关不控制 PA/Track Assignment 的写回。

跨领域操作由一个主业务入口负责。例如删除 Net 由 ConnectivityService 清理连接、Wire、standalone geometry、Guide/Track；移动 Instance 由 InstanceService 维护自身 PA/Bounds；TrackGrid 成功变更由 Floorplan 失效 PA/Track Assignment。内部复用 Storage/helper 后统一通知，避免公共 Service 互调形成重复事件或循环依赖。工具多对象 result 的范围、失败恢复和通知策略在 adapter 输出编排处明确。

通用 Shape move/permanent destroy 对 routing Shape 已经委派 RoutingStorage，校验和部分 payload/metadata/Bounds 不是两套实现。需要在既有委派上统一 owner-aware 后处理：typed Routing 编辑与 generic Geometry 编辑复用缓存/index/epoch、关联失效和受影响对象收集，外层成功操作只通知一次。内部 helper 不发 Context 通知，公共 Service 不相互递归调用。C++ 原生编辑及稳定 ID/detached payload 管理保留，GUI 只编排命令；不把整个 GeometryEdit 当作纯 UI 层迁走。

Routing 已落实 Guide→Track Assignment 的派生关系：Guide 替换成功后清同 Net 旧 Assignment，同时提交新 Guide/Assignment 则组合 staging 后保留新结果。Wire 和其他 Net 保留。外部 DRT 的 `writeContext` 使用同一 Net 的组合命令；`SAVE_GUIDE_UPDATES` 关闭时只换 TA，不清旧 Guide。

外部 DRT adapter、TritonRoute 和 native runner 已改为借用调用方长期 Context，共享 revision 与 observers；PA 输入直接读原生 Storage，输出一次提交明确 owner 的候选和选择。typed Routing 与 generic Geometry Shape 编辑复用 `detail::GeometryMutation` 的 R-tree/overview 增量后处理；helper 不发通知，外层边界通知一次。真实集成状态和验证以 [daily 实施日志](</home/zhuxu1/workspace/daily/2026年10月8日/ECCDB_Service_Consistent_Boundaries_Implementation_20261008.md>) 为准，不能用历史测试替代本轮验证。

iRT 的 `setDesignSource(DatabaseContext&)` 借用调用方 Context，旧三 Store 指针入口持有一个 attachment 生命周期的 fallback Context。没有输出详细布线的 Net 保留现有路由，不能将缺结果解释成 Clear；只替换明确输出 Net 的 Routed Wire，Fixed/Cover/special 保留。Track/GCell 与详细布线分别是业务提交，整个 iRT 输出不是一个覆盖所有领域的全库事务。
