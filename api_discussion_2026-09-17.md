# ECCDB API 讨论整理

## 讨论目标

本次讨论围绕 ECCDB 如何为 Rust viewer 和其他本地工具提供稳定 API 展开。结论是：先保持现有 C++ 数据库内核和 storage，不让业务代码直接接触 EnTT 或内部存储；在此基础上分层提供 C ABI 和 Rust crate。

## C++ public API 的基础类型

`include/eccdb/Types.h` 定义了 ECCDB 的公共基础类型。

`ObjectId<Tag>` 是保存 `uint64_t` 的强类型 ID。`InstanceId`、`NetId`、`WireId`、`LayerId` 等通过不同的空标签类型区分，底层数值相同也不能在 C++ 中混用。这种技术叫 Tag Type、Phantom Type 或 Strongly Typed ID。

ID 是 database-local 的实体句柄。当前 Design/DEF 使用 EnTT 的 64 位 entity，通常包含 entity index 和 generation；Tech/Library/LEF 当前使用 32 位 entity。`StorageConversions` 对有效 ID 直接转换 packed 数值，对无效值单独处理。API 层的无效值是 `UINT64_MAX`，EnTT 内部使用 `entt::null`。

`packed()` 取出 ID 内部的打包整数，既用于跨层转换，也用于哈希。`std::hash<ObjectId<Tag>>` 使 ID 可以作为 `unordered_map` 或 `unordered_set` 的键。

`Point` 和 `Rect` 是整数 DBU 坐标的几何值类型。`Rect` 提供合法性、面积、中心、平移、扩张、包含、相交和包围盒等操作。枚举类型统一表达方向、放置状态、Net 用途、来源和 Wire 状态。

## 数据结构与所有权

`DesignData.h` 定义 `NetData`、`NetOptions`、`InstanceData`、`InstancePinData` 和 `IoPinData`。这些都是脱离数据库存储的值快照，修改快照不会自动改变数据库，必须调用 `update*` 或关系操作提交。

`RoutingData.h` 定义 `WireMetadata`、`WirePoint`、`WirePathData`、`WireRoutingData`、Via 和设计 Via 数据。C++ 结构使用 `std::string`、`std::vector`、`std::optional` 和 `std::variant`，因此内存所有权由 C++ 对象本身管理。

`ViaDefinitionId = std::variant<TechViaId, ViaId>` 表示布线中的 Via 引用可以来自 LEF 技术库，也可以来自 DEF 设计库。`ViaPlacementData` 表示 Via 放置在 Wire path 的哪个点；它引用 Via 定义，而不是重复保存 fixed/generated 标志。Via 是 fixed 还是 generated，应查询其定义。

## Net 与 Pin 的关系

`NetData` 只保存 Net 自身属性，不包含所连接的 Pin 列表。Net 到 Pin 的关系由 storage 中的 `DesignNetInstancePins` 和 `DesignNetIoPins` 组件维护，底层是按 Net 保存的 ID vector，不是额外的全局 hashmap。

展示一个 Net 的连接对象时，查询链路是：

```text
NetId
  -> InstancePinId[] / IoPinId[]
  -> InstancePinData / IoPinData
  -> InstanceId
  -> InstanceData.name
```

按 Net 找到索引接近 O(1)，遍历 k 个连接是 O(k)，逐个读取 Pin 和 Instance 通常是 O(1)。因此完整展示成本是 O(k)，而不是 O(1)。名称到 ID 的查找才使用类似 hashmap 的索引。

## Config、Database 与 Ref

`Config.h` 只描述数据库如何打开：从 LEF/DEF 读取还是从 binary 读取、LEF 文件列表、DEF 文件、binary 三件套、多边形模式和可选缓存。

`Database.h` 是真正的 public facade。它拥有 `DatabaseState`，后者再管理 technology、library 和 design storage。典型流程是：Config 打开数据库，使用 typed ID 查询 Data 快照，修改后通过 `update*` 提交，再写出 DEF 或 binary。

`Ref.h` 中的 `NetRef`、`InstanceRef`、`WireRef` 是非拥有型便利句柄，内部保存 `DatabaseState*` 和 ID，不能跨越 Database 生命周期。跨语言接口更适合使用 Database + ID + Data snapshot，而不是暴露 Ref。

## ViaMaster 与 Generated Via 的实现边界

LEF 技术 Via 的 `TechViaMaster`、`TechViaGeometry`、`TechGeneratedViaMaster`，以及 `VIARULE ... GENERATE` 的 `TechViaRuleGenerate` 组件，已经在 tech storage 和 LEF importer/exporter 中实现。

但是当前 `eccdb::Database` public API 没有完整暴露 Tech ViaMaster 和 VIARULE Generate 的查询、创建、更新接口。public RoutingData 只能用 `TechViaId` 或 `ViaRuleId` 引用它们。DEF 侧的 `DesignViaData` 则已经通过 Database public API 暴露。

## C ABI 的设计与问题

C ABI 使用不透明的 `eccdb_database_t*` 作为数据库句柄，ID 仍然是带 `uint64_t` 的值结构。为了表达 C++ 的 string/vector，C 头文件把复杂快照展开为 `char*`、`T* + count` 和显式 `*_free()` 函数。

这些指针不是 storage 内部对象指针，而是 C ABI 为返回快照分配的副本；输入指针只在一次调用期间借用。当前设计能工作，但同一个结构既作为借用输入又作为拥有输出，容易发生忘记释放、重复释放、重复查询覆盖旧指针和部分分配失败后的清理问题。

因此后续更稳妥的 C ABI 方向是：简单数据继续用值结构，复杂 Wire/Via 查询结果考虑使用不透明的 snapshot handle，由 C++ RAII 管理内部 vector/string；输入和输出类型明确分离。

## Rust crate 分层

业务 Rust 工程不应直接管理 `libeccdb.so` 或 C 头文件，而应依赖 `eccdb` crate：

```text
Rust application
  -> eccdb Rust API
  -> raw C FFI / cxx bridge
  -> C ABI 或 C++ shim
  -> eccdb C++ public API
  -> storage
```

早期可以把 raw FFI 和 safe wrapper 放在一个 crate 中；规模扩大后可拆为 `eccdb-sys`（原始绑定）和 `eccdb`（安全 Rust API）两个 crate。Rust 对外应提供 `Database`、typed ID、`String`、`Vec`、`Option` 和 `Result`，不暴露 unsafe 指针。

Rust 的 `Drop` 负责关闭数据库和释放 C 输出快照，输入字符串和数组由调用期间的 arena 保持有效。Rust API 仍采用 Rust -> C++ 的调用方向；C++ -> Rust callback 属于以后插件架构的需求。

## Routing 读取与编辑的性能结论

`Database::wireRoutingData()` 返回独立快照，storage 到 C++ `WireRoutingData` 会复制一次；当前 C ABI 和 Rust 解码还会产生额外复制。读取和编辑语义清晰，但高频鼠标拖动不应每个 mouse move 都调用 `updateWire`。

更合适的交互流程是：读取一次 Wire snapshot，在 Rust 编辑会话中连续修改并预览，鼠标释放或明确提交时再调用一次 `updateWire`。Wire 本身有稳定 `WireId`，但 path、point、via placement 当前是聚合值，没有独立稳定 ID，因此暂时适合“读取整条 Wire、修改后整体替换”。

## 当前总体结论

ECCDB 已经形成了清晰的三层模型：typed ID 表示实体身份，Data 表示可编辑快照，storage 维护真实关系和不变量。下一步重点不是重写 storage，而是完善 Tech/Library public query API、收敛 C ABI 的所有权模型，并在 Wire 需要细粒度 GUI 编辑时再设计 segment/node 的稳定身份和 EditSession/undo 机制。
