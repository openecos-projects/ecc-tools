# EccDB 原生 C++ 接口

2026-10-08 起，EccDB 直接使用 Storage 中定义的类型和 ID，不再提供第二套 Data/Ref 对象模型。旧 C ABI 和 Rust API 已移除。接口仍在演进，消费者应固定源码版本、编译配置和依赖版本。

## 对象读取与业务修改

```cpp
#include <eccdb.h>
#include <instance/InstanceService.h>

auto database = eccdb::Database::open({
    .input = eccdb::LefDefInput{
        .lef_files = {"technology.lef", "cells.lef"},
        .def_file = "design.def",
    },
});

auto& context = database.context();
auto& netlist = context.design().netlistStorage();
const eccdb::DesignInstanceId instance = netlist.findInstance("u1");
const eccdb::DesignInstance& value = netlist.instance(instance);
const eccdb::Point previous_origin = value.origin;

eccdb::InstanceService instances{context};
instances.move(instance, 100, 0);
```

`DesignInstance`、`DesignNet`、原生 Term/Pin/Wire/Shape 及其 ID 就是数据库模型。读取直接返回原生对象、引用或 span，调用方需要副本时可以显式复制原生值，不经过 `InstanceData/NetData` 转换。

原生 Component 的字段约定仍需遵守，例如设置 Net 的 `weight` 时同时设置 `DesignNetFlag::kHasWeight`。业务 Service 沿用原生数据语义，不再经过另一套 DTO 的默认值或 flag 转换。

持久保存对象身份使用原生 ID。引用、指针、span 和 string_view 是短期借用；删除对象或改变对应存储后应重新取值。ID 仅在所属 Store/Registry 域内有意义，相同数值不表示来自同一数据库。

跨 Storage 的业务修改通过 Instance、Connectivity、Floorplan、Constraint、Routing 和 Geometry 服务完成。这样可以维护连接关系、级联删除、Bounds、空间索引、overview 与 Context 通知。原生 Storage 的低层写原语主要供 importer 和业务实现使用；上层不应直接修改 Component 字段代替这些业务操作。

## InstanceTerm 身份与所有权

`DesignInstanceTerm` 是现有 DesignRegistry 中的独立 EnTT entity component。`DesignInstanceTerms::values` 按 master 顺序保存 TermId，`names` 是名称到 TermId 的派生索引；Instance 不再内嵌 Term payload。Net 的端点列表继续保存这些 TermId。ID 的槽位回收和 generation 由 EnTT 管理，不再维护单调 Term OID 或全局 Term 指针表。

`findInstanceTerm(instance, name)` 直接查询名称索引；`instanceTerm(id)` 返回原生 Component，批量遍历使用 `forEachInstanceTerm` 或 registry view。Term 可以按需附着工具 Component。InstanceService 删除时先断开连接、清理 PA 和引用，再级联销毁所属 Term entity；compatible master swap 保留 Term ID 与连接。

Instance 和 InstanceTerm 使用 `in_place_delete`，无关同类对象的删除不移动既有 Component。现有 instance/master 指针缓存只用于运行时，Library 使用期间须保持约定的地址稳定性；禁止对有借用指针的 storage 执行 sort/compact。删除所属 Instance/Term、重置 Store 或读入快照后，旧指针失效。实例 Pin geometry 仍引用共享 MasterPin 并应用 Instance transform，不因 Term 独立而复制几何。

## 最小数据库持有容器

`Database` 负责：

- `open(Config)`：从 LEF/DEF 或三个 binary 文件构造原生 Store。
- `context()`：提供该数据库长期共享的原生 Context。
- `diagnostics()`：提供导入诊断。
- `writeDef()` / `writeBinary()`：导出当前原生数据。
- `supportsLefDef()`：查询构建是否包含直接文本导入器。

它不提供 Net/Instance/Pin/Wire 的 CRUD 转发，也不持有另一套对象副本。`Config` 只描述输入和可选 binary_cache；不再提供未实际接入的 PolygonMode/RuntimeOptions。

源码中的数据库持有功能集中在 `database/Database.h`、`database/Database.cpp` 和 `database/DatabaseConfig.h`；根 `eccdb.h` 是原生入口。旧 `api/`、`api/internal/` 和 `include/` 源目录已删除。

`Database.cpp` 内的私有 `Database::State` 只保存三个 Store 的所有权、Context 和导入诊断，导入/导出直接调用原生 IO。它不再是另一层提供 open/write 方法的 DatabaseState 类。Context 在三个 Store 导入完成后一次构造，无需 optional 的未初始化状态。

数据库持有 TechStore、LibraryStore 和 DesignStore。销毁顺序保证 Context 和 Design 先于 Library、Tech 释放。移动 `Database` 转移其内部持有指针，已经借用的 Context 和 Service 地址保持有效，但新持有者必须活得比这些借用更久。移动赋值会销毁目标原先持有的数据库，其旧借用也随之失效。

已有原生 Store 的工具直接构造 `DatabaseContext{technology, library, design}`；不需要制造一个借用型 Database facade。单线程 Context 不提供并发修改隔离，观察者不能重入业务写操作。

## 二进制与 DEF

```cpp
eccdb::BinaryFiles files{
    .technology = "technology.edb",
    .library = "library.edb",
    .design = "design.edb",
};
database.writeBinary(files);
database.writeDef("edited.def");
auto restored = eccdb::Database::open({
    .input = eccdb::BinaryInput{.files = files},
});
```

三个文件对应独立的技术、库和设计 Registry。`binary_cache` 在成功文本导入后写快照，不自动判断或命中已有缓存。读取 binary 时不能再指定文本导入缓存。

当前 Design binary schema 为 **13**：分别保存 Instance、Term entity、所有权列表和连接，恢复时重建名称与指针缓存。旧 schema 12 的内嵌 Term 快照会明确拒绝读取，需要从原始 LEF/DEF 重建；本轮没有旧快照转换器，Tech/Library schema 不变。

## CMake 消费

```cmake
find_package(EccDB CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE EccDB::database EccDB::core)
```

`EccDB::core` 提供原生 Storage/Context/Service；`EccDB::database` 提供持有容器及其 IO 链接。调用方也可仅使用原生 Store 和对应 IO target，无需创建 Database。安装包传递原生头文件、EnTT、Boost 与 ID 位宽配置；消费者必须使用一致的 C++20/ABI 配置。

安装包按原生模块安装 `database/`、Storage、Service 和 IO 头；私有 State 只定义在实现文件中。包消费者验证位于 `tests/package/consumer`。构建方法见 [BUILDING.md](../BUILDING.md)。

## Python 与 GUI

当前 Python 扩展继续提供 `eccdb_viewer.Database.open_lef_def/open_binary`、保存方法和基于 Context 的原生几何服务。批量数组是视口查询的派生数据，由绑定管理生命周期；没有第二份可独立修改的数据库。颜色、相机、选择、Qt/GPU 运行状态由 GUI 管理。

本轮完成原生接口清理及独立 InstanceTerm 迁移；未执行 workspace 开发区搬迁、项目拆仓或 benchmark 外置。
