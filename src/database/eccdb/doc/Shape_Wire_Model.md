# 可编辑的 Wire Shape 模型（2026-10-03）

当前分支 `refactor/eccdb-unified-geometry-20261003`，基于 `b335ec6079e6d797eec7a3db8281aedf9496f74f`。2026-10-02 的实验数字仅为历史记录，不能视为本轮性能结果。

参考 TritonRoute 的 `frNet`：Net 保存逻辑连接，Wire 保存状态与物理对象列表，每个 Shape 为独立 EnTT entity。普通线段、Via、Patch 可以按 typed ID 独立定位和编辑。这里没有对象池，也不保存第二份 PathRecord/扁平点数组或永久 Graph。

```text
DesignNet                         逻辑网络：InstanceTerm / BlockTerm
    └─ DesignWire                 net / status / shield_net / shapes[]
           └─ DesignRouteShape    wire / layer / flags / source_path
                  └─ geometry    Segment | Via | Patch | Point
                  └─ attributes  可选 Component：width / mask / taper_rule / shape / style
```

## 数据含义

| 内容 | 保存的信息 |
| --- | --- |
| Segment | 两个端点，包含各自 extension 与 virtual 标记；routing layer 位于 Shape |
| Via | 原点、Tech/Design via definition ID、方向、mask、array；entry layer 位于 Shape，exit layer 用于导出续接 |
| Patch | 绝对矩形；anchor 仅帮助生成 DEF 相对 RECT，不决定物理位置 |
| Point | 无法归属到线段的单点/虚拟点，保留语法而不制造金属 |
| source_path | 原始 path 分组提示，与 shapes 顺序配合；不是稳定 Path ID，也不表示电气连通 |

Shape ID 是独立 `GeometryEntity` 的 typed handle，使用 64 位 EnTT 编码（32 位槽位 + 32 位 generation），不是 vector 下标。Tech、Library、Design 各自有 GeometryRegistry，句柄需要与所属存储一起使用。移除一个 Shape 不改变其余 Shape 的 ID；组件地址仍受 EnTT 默认删除/重排策略约束，不能把 ID 稳定误认为指针永久稳定。

Wire 的 `shapes` 只保存 ID，Shape payload 存在 registry。可选 attributes 用普通 aggregate Component 保存，没有索引包装层。普通 routed geometry 不需要字符串字段。

公共 `GeometryShape` 元数据保存 owner、kind、layer、flags、source_path，实际 payload 是普通 route variant 或 SPECIALNET rectangle/polygon/Via。`DesignGeometryRectangle/Polygon/NetVia` 是既有 `DesignNet*` payload 的类型别名；写入和读取共用同一个类型，不保留第二套 DTO。

参考 KLayout 的 `dbShape.h` 只读 proxy 与 `dbShapes.h/.cc` 的 replace/transform、dirty bbox/update：`DesignGeometryStorage` 提供 `shape`、`bounds`、`moveShape`、原地替换、删除和顺序重排。`DesignStore::geometryIndex()` 持有按 revision 延迟重建的 Boost.Geometry R-tree，可按全设计、Net 或 Wire 查询候选 ShapeId。它保存派生 BBox 和筛选关系，几何不复制到索引中。修改 NDR 引用或 Design NDR 宽度也通知 revision；直接修改底层 registry 或 Tech 定义需显式 `invalidate()`。

GUI 可以用 `geometryIndex().query(box)` 选候选 ID，读取 `geometryStorage().shape(id)`，然后通过 `moveShape/updateRouteShape/replaceShape/replaceNetShape/destroy` 编辑。payload 只在当前只读访问期间借用；编辑后用 ID 重新取视图。现阶段没有 undo manager、自动连线修复或永久拓扑 Graph。

本轮完整迁移将 Design 的 GeometryRegistry 所有权移到 `DesignRegistry`，routing、netlist、constraint、fill、global、floorplan、guide storages 都借用它。BlockPin 矩形/多边形/Via、固定 DesignVia 几何、Region、Blockage、Fill、Instance 内联 Region bounds、Die/Core boundary、Guide/Track assignment 均改为独立 Shape entity；对应所有者只保存 ID。`Design*Input` 是写入边界的临时值，不参与 binary snapshot，不是第二份数据库。

BlockPin payload 保留 DEF pin-local 坐标；`localBounds` 返回局部边界，`bounds` 应用 pin origin/orientation，`moveShape` 的位移按设计坐标解释并转换回 pin-local 坐标。Pin shape 编辑会清除该 Pin 的 PA 候选缓存。固定 Via 定义是局部模板，完整设计的空间查询不把它当作放置后的版图对象；`query(GeometryOwner, box)` 可以按定义所有者查询。非 Net 所有者不会被误解释成 Net，Guide/Track 的 Net 和 Pin 的 Term→Net 关系明确解析。

Row/Tracks/GCellGrid 的周期格网参数、generated-Via 公式参数，以及 PA 点和逃逸路径仍作为参数/算法结果保存，不展开成大量新的版图 Shape。Tech/Library 的矩形/多边形以及 Library PORT/OBS 中的 Via placement 使用独立 GeometryRegistry；三个域的 ID 空间仍分开。

## 访问与编辑

`DesignRoutingStorage` 提供 `createWire`、`createShape`、`shape`、`shapeAttributes`、`updateShape`、`destroyShape`、`shapeBoundingBox`、`forEachShape`。删除 Wire 会级联删除它的 Shape。内部 `shape`/遍历遵循有效 ID 契约，直接 `registry.get`；公开写入与恢复时检查必要的关系和字段。

BBox 按当前 layer/default/NDR width、端点 extension 和 Via 定义计算；水平/垂直 Segment 返回实际边界，斜线返回保守边界。当前 Tech model 没有独立 wrong-direction width，不能把这个查询当成完整 DRC 几何求解器。Patch 返回保存的绝对矩形。

`PhysicalDesignService::forEachNetWireShape` 是无需还原 path 的读取入口。`RoutingService::snapshot/replaceRouted` 直接投影/写入 Shape，native DRT 直接构建 `frPathSeg/frVia/frPatchWire`。`snapshot` 按 RouteShape DTO 支持的物理字段投影，不再逐 Shape 校验 metadata，也不携带 mask/array/style 等完整 DEF 属性；这些字段仍保存在 Storage 并由 DEF writer 读取。point-only Shape 仍无法投影到该 DTO。

2026-10-02 写回优化中，`replaceRouted` 保留 DTO 转换层的 layer/Via/Patch 检查，直接把每项转换结果写入临时新 Wire 的独立 Shape entity，不再分配完整候选 Shape vector。每 Wire 只取得一次 Shape storage，预留 Shape ID vector，写入后不再通过 registry 读取同一 Shape，连续 source_path 的下一个序号在批末一次维护。Via 的身份校验后直接访问已校验 payload，省去 accessor 中重复的身份检查。公开 Shape CRUD、DEF 导入和 binary restore 的校验继续保留。全局 EnTT storage 不按每条 Wire 的精确大小反复 reserve。

新 Wire 完整写入成功后才删除旧 routed Wire；转换、分配或写入失败时删除临时 Wire 及其已创建 Shape，旧 routed/fixed Wire 和 Net 的索引保持不变。Wire 删除时移动出 Shape ID vector，不深拷贝整个 Wire。原私有 appendShapesUnchecked helper 已删除，没有新增对象池或索引封装。

写回分段诊断通过 `replaceRouted` 的可选 `NetRouteWriteProfile*` 和 replay 的 `--profile-wire-write` 开启；默认不读取诊断时钟，计数与时间不属于持久化数据。每条 Wire 计时，不逐 Shape 打点。流式写入后，convert_shapes_ns 保留为历史兼容零字段，append_shapes_ns 包含转换、检查、entity/component 写入和 Shape ID 维护。优化前 test6 一轮中两层转换共 790.154 ms（68.44%），Shape 批次写入 278.670 ms（24.14%），旧 Wire/Shape 为 0；首段包括名称解析和字段检查，不能当纯复制开销。历史结果见 `workspace/daily/2026年10月2日/WIRE_WRITE_PROFILE_20261002.md`；旧分段比例不能套用到优化后的阶段。异常中止的计数可能只部分累加，诊断 replay 不输出成功结果。

Adaptor 和 Service 的主入口是 `forEachNetWireShape`、Shape CRUD，以及 `RoutingService::replaceRouted`；它们直接消费/写入独立 Shape，不构造临时 Path。`DesignWirePath` 及其 view 只在 DEF parser/exporter 的局部转换边界使用，不再作为 storage/service/adaptor 的公共访问接口；`forEachPath/path(i)/materializePaths/updateWire` 已从生产路径删除。直接 Shape API 是编辑和 TritonRoute adaptor 的唯一入口。

2026-10-08 起，原有 NetRoute/Via/Context 服务合并为 `RoutingService`，通过 `DatabaseContext` 注入 Store；关联删除和变更通知见 [Service 层说明](Service_Layer_20261007.md)。本次 Service 迁移没有更改 canonical Shape 模型或 DEF 格式。

## DEF 与二进制

DEF parser 的临时 `DesignWirePath` 转成 Shape；Via 后续线段使用切换后的 layer。DEF writer 直接按 Wire 的 Shape 顺序流式输出，遇到 source path、属性、层或端点不连续时开启 `NEW`，不先构造临时 Path，也不添加跨越断点的线段。

这只负责合法的几何序列化，不从任意几何恢复完整 connectivity。分叉、相交、重叠、跨层连通与 pin attachment 的 Graph 留给后续算法构建。移动 Shape 不自动移动相邻 Wire/Pin，也不自动修复电气断开；这些属于将来的编辑操作/拓扑约束。

当前 binary schema 为 **Tech 11 / Library 5 / Design 12**，分别保存所有者 registry 与 GeometryRegistry；持久化所有者的 Shape IDs、独立 payload 与可选属性。runtime pointer、revision 与 BBox 索引不落盘。Design 11 / Library 4 的嵌套值不能按新 schema 直接读取，需重新从 LEF/DEF 建库；旧版本不自动迁移。

SPECIALNET 独立 `+ RECT/+ POLYGON/+ VIA` 也已改为独立几何 entity；`DesignNetGeometry` 只保存有序 ID 关系。保留 clause/status、mask、shape 和 shield 等 DEF 语义。

## 验证边界

本轮恢复并迁移了 Design/Service、DEF/binary 和差分测试，结合 focused Geometry 测试共 129 个 GTest case 通过；native DRT 的 ODB/EccDB 小 fixture 及交叉 DEF 回环也通过。纯 iDB generated-Via ORIGIN/OFFSET 导出 case 仍失败，DEF 有 2 项既有 disabled case，native frNet 的 VIRTUAL point 与 SPECIALNET POLYGON 仍明确不支持。详细范围、复现命令和代码入口见 [Geometry_Migration_20261003.md](Geometry_Migration_20261003.md)。

旧 GeometryPool 测试和依赖旧 schema 的 DB-only/trace replay 已退出构建，本轮没有 replay 耗时数据。完整 ODB `JUNCTION/SHORT/VWIRE/ITERM/BTERM/RULE` decoder/encoder 尚未纳入；当前 adaptor 合同限定为 TritonRoute 普通 Shape 几何子集。交接记录在 `workspace/daily/2026年10月3日/decisions/03-editable-geometry`。
