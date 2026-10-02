# 可编辑的 Wire Shape 模型（2026-10-02）

分支 `refactor/eccdb-shape-layer-20261002`，基于 Term 实验提交 `61d1d87a`。

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

Shape ID 是现有 `DesignEntity` 的 typed handle，不是 vector 下标。移除一个 Shape 不改变其余 Shape 的 ID；组件地址仍受 EnTT 默认删除/重排策略约束，不能把 ID 稳定误认为指针永久稳定。

Wire 的 `shapes` 只保存 ID，Shape payload 存在 registry。可选 attributes 用普通 aggregate Component 保存，没有索引包装层。普通 routed geometry 不需要字符串字段。

## 访问与编辑

`DesignRoutingStorage` 提供 `createWire`、`createShape`、`shape`、`shapeAttributes`、`updateShape`、`destroyShape`、`shapeBoundingBox`、`forEachShape`。删除 Wire 会级联删除它的 Shape。内部 `shape`/遍历遵循有效 ID 契约，直接 `registry.get`；公开写入与恢复时检查必要的关系和字段。

BBox 按当前 layer/default/NDR width、端点 extension 和 Via 定义计算；水平/垂直 Segment 返回实际边界，斜线返回保守边界。当前 Tech model 没有独立 wrong-direction width，不能把这个查询当成完整 DRC 几何求解器。Patch 返回保存的绝对矩形。

`PhysicalDesignService::forEachNetWireShape` 是无需还原 path 的读取入口。`NetRouteService::snapshot/replaceRouted` 直接投影/写入 Shape，native DRT 直接构建 `frPathSeg/frVia/frPatchWire`。`snapshot` 按 RouteShape DTO 支持的物理字段投影，不再逐 Shape 校验 metadata，也不携带 mask/array/style 等完整 DEF 属性；这些字段仍保存在 Storage 并由 DEF writer 读取。point-only Shape 仍无法投影到该 DTO。

2026-10-02 写回优化中，`replaceRouted` 保留 DTO 转换层的 layer/Via/Patch 检查，直接把每项转换结果写入临时新 Wire 的独立 Shape entity，不再分配完整候选 Shape vector。每 Wire 只取得一次 Shape storage，预留 Shape ID vector，写入后不再通过 registry 读取同一 Shape，连续 source_path 的下一个序号在批末一次维护。Via 的身份校验后直接访问已校验 payload，省去 accessor 中重复的身份检查。公开 Shape CRUD、DEF 导入和 binary restore 的校验继续保留。全局 EnTT storage 不按每条 Wire 的精确大小反复 reserve。

新 Wire 完整写入成功后才删除旧 routed Wire；转换、分配或写入失败时删除临时 Wire 及其已创建 Shape，旧 routed/fixed Wire 和 Net 的索引保持不变。Wire 删除时移动出 Shape ID vector，不深拷贝整个 Wire。原私有 appendShapesUnchecked helper 已删除，没有新增对象池或索引封装。

写回分段诊断通过 `replaceRouted` 的可选 `NetRouteWriteProfile*` 和 replay 的 `--profile-wire-write` 开启；默认不读取诊断时钟，计数与时间不属于持久化数据。每条 Wire 计时，不逐 Shape 打点。流式写入后，convert_shapes_ns 保留为历史兼容零字段，append_shapes_ns 包含转换、检查、entity/component 写入和 Shape ID 维护。优化前 test6 一轮中两层转换共 790.154 ms（68.44%），Shape 批次写入 278.670 ms（24.14%），旧 Wire/Shape 为 0；首段包括名称解析和字段检查，不能当纯复制开销。历史结果见 `workspace/daily/2026年10月2日/WIRE_WRITE_PROFILE_20261002.md`；旧分段比例不能套用到优化后的阶段。异常中止的计数可能只部分累加，诊断 replay 不输出成功结果。

Adaptor 和 Service 的主入口是 `forEachNetWireShape`、Shape CRUD，以及 `NetRouteService::replaceRouted`；它们直接消费/写入独立 Shape，不构造临时 Path。`DesignWirePath` 及其 view 只在 DEF parser/exporter 的局部转换边界使用，不再作为 storage/service/adaptor 的公共访问接口；`forEachPath/path(i)/materializePaths/updateWire` 已从生产路径删除。直接 Shape API 是编辑和 TritonRoute adaptor 的唯一入口。

## DEF 与二进制

DEF parser 的临时 `DesignWirePath` 转成 Shape；Via 后续线段使用切换后的 layer。DEF writer 直接按 Wire 的 Shape 顺序流式输出，遇到 source path、属性、层或端点不连续时开启 `NEW`，不先构造临时 Path，也不添加跨越断点的线段。

这只负责合法的几何序列化，不从任意几何恢复完整 connectivity。分叉、相交、重叠、跨层连通与 pin attachment 的 Graph 留给后续算法构建。移动 Shape 不自动移动相邻 Wire/Pin，也不自动修复电气断开；这些属于将来的编辑操作/拓扑约束。

Design binary schema 为 **9**，持久化 Wire 的 Shape IDs、独立 Shape payload 与可选属性，保留原 runtime pointer 不落盘约定；旧 schema 8 不自动迁移。路径和 BBox 均可重建，不额外保存缓存几何。

SPECIALNET 独立 `+ RECT/+ POLYGON/+ VIA` 仍在原 `DesignNetGeometry` Component 中，保留其 clause/status 语义。本轮拆分的是 Wire path 内的物理对象，没有声称整个版图的所有 Shape 都已经改成独立 entity。

## 验证边界

测试覆盖 Shape CRUD、ID 保留、BBox、Wire 级联删除、Via 层切换、编辑断开后 DEF 重导入、attributes、Shape writer 以及 binary fixed point。完整 ODB `JUNCTION/SHORT/VWIRE/ITERM/BTERM/RULE` decoder/encoder 尚未纳入本轮；当前 adaptor 合同限定为 TritonRoute 普通 Shape 几何子集。实验日志与分支交接记录在 `workspace/daily/2026年10月2日`。
