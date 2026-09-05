# EccDB 集成计划：链接方式与 Rust FFI

English version: [Integration_Plan.en.md](Integration_Plan.en.md)

本文记录当前工具访问 EccDB 的两种链接方式，并说明未来 iRT/iDRC 和 Rust
程序的集成路线。它是一份计划文档，不表示其中所有目标边界都已经实现。

> **当前状态：探索性接口。** `eccdb_api` 目前尚未稳定，仍处于公共 API
> 设计和验证阶段。LEF/DEF、布线、几何和规则等功能尚未完整覆盖，接口名、
> 数据模型、ABI 和 CMake 导出目标都可能继续调整。当前不应把它当作稳定的
> 第三方 SDK 或长期兼容的 ABI；本文中的动态链接和 Rust 接入方案属于目标
> 方向，实际使用应固定到明确的源码提交并接受接口变化。

当前计划中的未实现项目：

- **TODO：** 稳定公共 API，并验证干净外部工程中的安装、导出和动态链接流程；
- **TODO：** 为 `libeccdb.so` 补齐跨环境的运行时部署和版本兼容策略；
- **TODO：** 增加 CXX bridge 及 Rust wrapper，目前仓库尚未提供可用的 Rust crate。

## 1. 两种访问方式

### 1.1 直接访问 Store：内部静态链接

当前 iRT 和 iDRC adapter 直接使用内部 Store 类：

```cpp
void setDesignSource(
    eccdb::DesignStore* design,
    eccdb::TechStore* tech,
    eccdb::LibraryStore* library);
```

它们在 CMake 中链接 EccDB 内部目标，例如：

```cmake
target_link_libraries(irt_interface PRIVATE
    eccdb_design
    eccdb_tech_database
    eccdb_library)
```

这些 Storage 目标是普通的 CMake library，在主 ecc-tools 构建中当前生成为
静态归档：

```text
libeccdb_design.a
libeccdb_tech_database.a
libeccdb_library.a
```

对应关系是：

```text
iRT/iDRC adapter
    -> 内部 Store 和 EnTT 组件
    -> EccDB 静态归档
```

这种方式适合 EccDB 内部代码，或者需要直接访问内部数据的实验性 adapter。
它不是稳定的外部 API，因为调用方依赖 Store 布局、EnTT entity、组件类型和
Storage 的拆分方式。

### 1.2 访问公共 API：实验性动态链接目标

当前公共 API target 被构建为动态库：

```cmake
add_library(eccdb_api SHARED ...)
```

生成文件：

```text
libeccdb.so
libeccdb.so.0
libeccdb.so.0.1.0
```

外部 C++ 程序只使用安装后的公共头文件和 CMake 导出目标：

```cpp
#include <eccdb/eccdb.h>
```

```cmake
find_package(EccDB CONFIG REQUIRED)
target_link_libraries(my_tool PRIVATE EccDB::eccdb)
```

目标关系是：

```text
外部工具
    -> EccDB 公共 Database/API
    -> libeccdb.so
    -> 内部 Store、Pool 和 EnTT
```

调用方拿到的是公共 ID、Data 值和受控 API 操作，不会拿到可变的 EnTT
Registry 或内部 Storage 引用。

这只表示当前构建中存在一个共享库 target，不表示动态链接、安装包、运行时
搜索路径和 ABI 兼容策略已经完成。稳定的外部动态链接流程仍是 **TODO**。

## 2. TODO：iRT/iDRC 的未来接入方式

长期目标是让 iRT 和 iDRC 使用公共 API，并链接共享库：

```cmake
target_link_libraries(irt_interface PRIVATE EccDB::eccdb)
target_link_libraries(idrc_interface PRIVATE EccDB::eccdb)
```

它们的接口应逐步从 Store 指针：

```cpp
eccdb::DesignStore*
eccdb::TechStore*
eccdb::LibraryStore*
```

迁移到公共数据库和 identity 类型：

```cpp
void setDesignSource(eccdb::Database& database);
```

迁移时不能让同一个工具同时链接 `libeccdb.so`，又静态编译第二份相同的
EccDB Storage 实现。两套 Registry 或两份数据库实现会导致状态不一致、符号
重复和生命周期问题。

在迁移之前，公共 API 需要覆盖布线和 DRC 所需操作：Net/Pin 连接、Wire/Path
编辑、Via 解析、技术层规则、几何查询和受控回写。完成这些能力之前，现有
adapter 可以继续使用内部静态 Store 目标。

## 3. TODO：Rust 与 CXX 接入

Rust 接入目前尚未实现。未来只需要增加一层薄的 Rust wrapper，通过 CXX
bridge 调用 `libeccdb.so`，不需要重新实现 EccDB 内核：

```text
Rust 应用
    -> 安全的 Rust EccDB wrapper
    -> CXX FFI bridge 和少量 C++ shim
    -> libeccdb.so
```

### 3.1 TODO：Rust wrapper

Rust wrapper 只负责把 C++ 的公共 API 转成 Rust 风格的所有权、错误和借用
接口。它可以只覆盖渲染器或编辑器实际需要的功能，不需要复制全部 C++ API。
数据库状态、ID 语义、Storage 和 EnTT 仍由 `eccdb_api` 负责。

### 3.2 TODO：动态库部署

Rust 程序运行时需要能够找到 `libeccdb.so`。开发阶段可使用
`LD_LIBRARY_PATH`，正式部署时再确定 RPATH/RUNPATH 或应用私有 library 目录。

核心原则是：**只有一份权威 EccDB 状态、一套公共所有权模型，Rust 不重写
数据库内核。**
