# EccDB C++ API 使用指南

English version: [API_Usage.en.md](API_Usage.en.md)

## 1. API 定位

EccDB 公共 API 是底层 EnTT Registry、连续 Pool 和各类 Storage 之上的稳定访问层。外部程序只需包含 `eccdb` 公共头文件，不需要了解 EnTT 实体、内部组件或 Storage 的拆分方式。

当前第一版公共 API 提供：

- 使用 LEF/DEF 或 EccDB binary 打开数据库；
- Net、Special Net、Instance、Instance Pin、IO Pin、Wire 和 DEF design VIA 的查询与增删改；
- Pin 与 Net 的连接关系操作；
- Wire path、路径点、VIA placement 和矩形扩展的值类型；
- DEF 和 binary 导出；
- 可选的 C++ `Ref` 便利句柄。

当前尚未提供完整的 technology/library 公共查询与编辑 API。因此，从零创建 Instance、Wire 或 design VIA 时所需的 `CellMasterId`、`RoutingLayerId`、`LayerId`、`TechViaId` 等通常来自已经导入的数据库对象。第一版主要面向“导入现有 LEF/DEF 或 binary，然后查询和编辑设计”的工作流。

## 2. 引入与链接

推荐统一包含：

```cpp
#include <eccdb/eccdb.h>
```

安装后的 CMake 项目使用：

```cmake
find_package(EccDB CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE EccDB::eccdb)
```

源码树内的构建和安装方法见 [BUILDING.md](../BUILDING.md)。

## 3. 打开数据库

### 3.1 LEF/DEF 输入

```cpp
#include <eccdb/eccdb.h>
#include <stdexcept>

eccdb::Config config{
    .input = eccdb::LefDefInput{
        .lef_files = {"technology.lef", "cells.lef"},
        .def_file = "design.def",
    },
    .runtime = eccdb::RuntimeOptions{
        .polygon_mode = eccdb::PolygonMode::kNative,
    },
};

if (!eccdb::Database::supportsLefDef()) {
  throw std::runtime_error("this EccDB build has no LEF/DEF parser");
}

auto database = eccdb::Database::open(config);
```

`PolygonMode::kNative` 保留原生 polygon；`PolygonMode::kRectangularized` 在导入时将可转换的 polygon 矩形化。

### 3.2 导入后同时生成 binary cache

```cpp
eccdb::BinaryFiles cache{
    .technology = "technology.edb",
    .library = "library.edb",
    .design = "design.edb",
};

eccdb::Config config{
    .input = eccdb::LefDefInput{
        .lef_files = {"technology.lef", "cells.lef"},
        .def_file = "design.def",
    },
    .binary_cache = cache,
};

auto database = eccdb::Database::open(config);
```

Technology、Library 和 Design 使用三份 binary 文件，因为它们是相互关联但独立的存储域。

### 3.3 Binary 输入

```cpp
auto database = eccdb::Database::open({
    .input = eccdb::BinaryInput{
        .files = {
            .technology = "technology.edb",
            .library = "library.edb",
            .design = "design.edb",
        },
    },
});
```

## 4. ID、Data 和 Ref

公共 API 有三类核心类型：

| 类型 | 示例 | 语义 |
| --- | --- | --- |
| 强类型 ID | `NetId`、`WireId`、`InstanceId` | 数据库中实体的身份；可复制、比较、hash 和长期保存 |
| Data 值 | `NetData`、`InstanceData`、`WireRoutingData` | 拥有内容的独立快照；修改副本不会自动写回数据库 |
| Ref 句柄 | `NetRef`、`WireRef`、`InstanceRef` | `DatabaseState* + ID` 组成的非 owning C++ 便利入口 |

Database 拥有真实实体。下面的代码只修改本地快照：

```cpp
eccdb::NetId net_id = database.findNetId("clk");
auto data = database.netData(net_id);

if (data) {
  data->name = "clk_main";
  // 数据库中的名字仍然是 clk。
}
```

显式调用 `updateNet` 后才写回：

```cpp
database.updateNet(net_id, *data);
```

所有 ID 都只属于创建它的那一个 `Database`。不要把数据库 A 的 `NetId` 传给数据库 B。实体删除或 Database 销毁后，对应 ID 失效。

## 5. Net CRUD

### 5.1 查询与遍历

```cpp
eccdb::NetId net = database.findNetId("clk");
eccdb::NetId regular = database.findRegularNetId("clk");
eccdb::NetId power = database.findSpecialNetId("VDD");

if (net && database.contains(net)) {
  auto data = database.netData(net);
  bool special = database.isSpecialNet(net);
}

for (eccdb::NetId id : database.regularNetIds()) {
  auto data = database.netData(id);
}
```

找不到对象时，`find*Id()` 返回无效 ID，`*Data()` 返回 `std::nullopt`。

### 5.2 创建、修改和删除

```cpp
eccdb::NetId signal = database.createNet({
    .name = "data_bus",
    .use = eccdb::SignalUse::kSignal,
    .source = eccdb::NetSource::kUser,
});

eccdb::NetId ground = database.createSpecialNet({
    .name = "VSS",
    .use = eccdb::SignalUse::kGround,
});

auto signal_data = *database.netData(signal);
signal_data.weight = 10;
database.updateNet(signal, std::move(signal_data));

bool removed = database.destroyNet(signal);
```

如果 Net 仍连接 Pin 或拥有 Wire，`destroyNet()` 返回 `false`，不会破坏引用关系。

特殊网络的可选属性使用 `NetOptions`：

```cpp
database.setNetOptions(ground, eccdb::NetOptions{
    .original = "VSS_ORIGINAL",
    .voltage = 0,
});
```

## 6. Instance、Pin 与连接关系

### 6.1 查询 Instance 和自动物化的 Instance Pin

```cpp
eccdb::InstanceId instance = database.findInstanceId("u1");
auto instance_data = database.instanceData(instance);

eccdb::InstancePinId input = database.findInstancePinId(instance, "A");
auto pin_data = database.instancePinData(input);
```

创建 Instance 时，底层会根据 `CellMasterId` 对应 master 的 terminals 自动物化 Instance Pins：

```cpp
eccdb::CellMasterId master = instance_data->master;

eccdb::InstanceId created = database.createInstance({
    .name = "u2",
    .master = master,
    .origin = {1000, 2000},
    .orientation = eccdb::Orientation::kN,
    .placement_status = eccdb::PlacementStatus::kPlaced,
});
```

### 6.2 IO Pin 与 Net 连接

```cpp
eccdb::IoPinId io_pin = database.createIoPin({
    .name = "DATA_IN",
    .direction = eccdb::IoDirection::kInput,
    .use = eccdb::SignalUse::kSignal,
});

database.connect(io_pin, signal);
database.connect(input, signal);

auto io_data = database.ioPinData(io_pin);
auto connected_instances = database.instancePinIds(signal);
auto connected_io_pins = database.ioPinIds(signal);

database.disconnect(input, signal);
database.disconnect(io_pin);
```

`IoPinData::net` 和 `IoPinData::special_net` 是连接关系快照。修改连接关系应使用 `connect()`/`disconnect()`；`updateIoPin()` 用于修改名称、方向和 use，并要求传入快照中的连接字段与当前数据库一致。

## 7. Wire、Path 和 VIA

Wire 是有身份的数据库实体，使用 `WireId`。WirePath、点、VIA placement 和矩形是 Wire 内部拥有的值，不拥有独立 ID。

### 7.1 创建 DEF design VIA

下面的 `layer` 假定是从已导入数据库对象获得的 `LayerId`：

```cpp
eccdb::ViaId via = database.createDesignVia({
    .name = "LOCAL_VIA",
    .rectangles = {
        eccdb::ViaRectangle{
            .layer = layer,
            .rectangle = {-5, -5, 5, 5},
        },
    },
});
```

### 7.2 创建 Wire routing

下面的 `routing_layer` 是已获得的 `RoutingLayerId`：

```cpp
eccdb::WireRoutingData routing{
    .paths = {
        eccdb::WirePathData{
            .layer = routing_layer,
            .width = 100,
            .points = {
                eccdb::WirePoint{.position = {0, 0}},
                eccdb::WirePoint{.position = {1000, 0}},
            },
            .vias = {
                eccdb::ViaPlacementData{
                    .point_index = 1,
                    .definition = eccdb::ViaDefinitionId{via},
                },
            },
        },
    },
};

eccdb::WireId wire = database.createWire(
    eccdb::WireMetadata{
        .net = ground,
        .status = eccdb::WireStatus::kFixed,
    },
    std::move(routing));
```

`ViaDefinitionId` 是 `std::variant<TechViaId, ViaId>`：

- `ViaDefinitionId{tech_via}` 表示 LEF technology VIA；
- `ViaDefinitionId{design_via}` 表示 DEF design VIA；
- 每个 placement 必须且只能引用其中一种。

### 7.3 查询与整体写回

```cpp
auto metadata = database.wireMetadata(wire);
auto routing_data = database.wireRoutingData(wire);

routing_data->paths.front().width = 120;
database.updateWire(wire, *metadata, std::move(*routing_data));

for (eccdb::WireId id : database.wireIds(ground)) {
  auto wire_data = database.wireRoutingData(id);
}
```

因为 Path 是 aggregate value，修改路径后通过 `updateWire()` 整体写回。这样不会把内部 Pool 的地址、offset 或 span 暴露给外部调用者。

## 8. Ref 便利层

Ref 适合纯 C++ 调用方进行短生命周期的链式操作：

```cpp
eccdb::NetRef net = database.net(net_id);
if (net) {
  net.rename("clk_main");
  net.setUse(eccdb::SignalUse::kClock);

  for (eccdb::WireRef wire : net.wires()) {
    auto path = wire.pathData(0);
  }
}
```

Ref 本身通常只是一个指针加一个 ID，在栈上按值返回，不需要 `new` 或 `delete`。它不拥有 Database，不能比 Database 活得更久。跨语言绑定和长期缓存应优先保存强类型 ID，而不是 Ref。

`name()` 返回的 `std::string_view` 是短生命周期借用；重命名、更新对应对象或销毁 Database 后不能继续使用旧 view。

## 9. 导出与诊断

```cpp
database.writeDef("edited.def");

database.writeBinary({
    .technology = "technology.edb",
    .library = "library.edb",
    .design = "design.edb",
});

for (const eccdb::ImportDiagnostic& diagnostic : database.diagnostics()) {
  std::cout << diagnostic.source << ": "
            << diagnostic.statement << " x"
            << diagnostic.occurrence_count << '\n';
}
```

## 10. 错误、生命周期和并发约束

- 查询不存在的 ID 时，`contains()` 返回 `false`，Data 查询返回 `std::nullopt`，Ref 查询返回空句柄。
- 非法创建或更新通常抛出 `std::invalid_argument`；使用失效 Ref 会抛出 `std::out_of_range`。
- `destroy*()` 返回 `false` 通常表示对象不存在或仍被其他对象引用。
- ID 只在所属 Database 内有意义；当前 ID 本身不携带 Database cookie。
- Ref 和 view 都不拥有底层数据，Database 销毁后立即失效。
- 当前公共 API 不承诺并发读写安全；多线程调用同一个 Database 时应由调用方同步。
- 当前还没有事务、undo/redo、revision/change set 或稳定的 Rust C ABI；这些属于后续 API 层扩展。

## 11. 公共头文件

| 头文件 | 内容 |
| --- | --- |
| `eccdb/eccdb.h` | 推荐统一入口 |
| `eccdb/Config.h` | 输入、运行选项和 binary 文件配置 |
| `eccdb/Types.h` | 强类型 ID、基础几何和枚举 |
| `eccdb/DesignData.h` | Net、Instance 和 Pin 快照类型 |
| `eccdb/RoutingData.h` | design VIA、Wire 和 Path 值类型 |
| `eccdb/Ref.h` | 非 owning C++ Ref 便利句柄 |
| `eccdb/Database.h` | Database 所有权和核心 CRUD API |

外部程序不应包含 `api/internal`、`storage`、`io` 或 `adapters` 下的头文件。
