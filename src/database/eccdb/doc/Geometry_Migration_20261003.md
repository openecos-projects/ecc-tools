# 完整几何迁移（2026-10-03）

分支：`refactor/eccdb-unified-geometry-20261003`，HEAD：`b335ec6079e6d797eec7a3db8281aedf9496f74f`。在此前 Shape 重构的工作树上继续，保留已有修改；本轮尚未提交。本文记录用户“全部迁移”的后续实施，更新当天 012 交接记录中的未迁移项。

后续状态：以上是 2026-10-03 当时的记录；迁移及后续 GUI 服务在 2026-10-07 汇总为 checkpoint。当前范围、构建规则和剩余限制见 [Unified_Geometry_Checkpoint_20261007.md](Unified_Geometry_Checkpoint_20261007.md)，最终 commit 与本次验证结果见当天 daily。

## 数据组织

`DesignRegistry` 持有唯一 Design `GeometryRegistry`，所有 storage 共用；Tech 与 Library 各自的 GeometryRegistry 保持独立。每个物理 primitive 有自己的 `GeometryShapeId`，所有者保存有序 ID 关系，不再内嵌 canonical 坐标数组。

| 所有者 | GeometryRegistry payload | 所有者关系 |
| --- | --- | --- |
| Wire / SPECIALNET | 既有 route / net rectangle、polygon、via | `shapes` |
| BlockPin | `DesignPinRectangle/Polygon/Via` | `rectangles/polygons/vias` IDs |
| 固定 DesignVia | `DesignViaRectangle/Polygon` | `rectangles/polygons` IDs |
| Region / Blockage / Fill | `GeometryRectangle/Polygon` | 既有字段改 ID 数组 |
| Instance 内联 Region bounds | `GeometryRectangle` | `region_bounds` IDs |
| Die / 显式 Core | `GeometryBoundary` | 一个 boundary ID |
| Net Guide / Track assignment | `DesignRouteGuide/DesignTrackAssignmentShape` | `values` IDs |
| Library PORT / OBS 中的 Via | `LibraryViaPlacement` | `vias` IDs，位于 Library GeometryRegistry |

输入 aggregate `Design*Input` 保留 parser/API 的临时值。写入时创建几何实体，成功后替换所有者关系并删除旧实体；失败清理本次新实体。正常读取直接解引用有效 ID，不增加重复的 owner/身份校验。所有者删除负责级联释放几何；GUI 删除单 Shape 同时移除所有者关系。

Row、TRACKS、GCELLGRID 的周期参数和生成 Via 的公式参数保持参数表达。PA candidates/escape segments 是算法结果，仍保存在 PA 数据中，不复制成另一份版图。

范围限定为 EccDB 的 canonical owner geometry。`src/database/data/geometry` 的 `ecc::geometry::GeometryStore/ShapeRecord` 是另外一条从 iDB 构建的 query/snapshot/LOD 管线，仍用于 Tcl/Python 导出和 iZH metal-fill 查询，没有连接 EccDB GeometryRegistry；本轮没有把它删掉，也不把它计为 EccDB 的第二份原生几何。后续 GUI 数据源切换需要单独接入。

## 编辑与查询

`DesignGeometryStorage::shape` 返回借用视图；`replaceShape`、`moveShape`、`destroy` 保持统一编辑入口。Pin-local 几何经 Pin placement/orientation 得到设计坐标 BBox；全局位移会逆变换回局部坐标。Pin 几何修改使该 Pin 的 PA 候选失效。电气拓扑和邻接线段不自动修复。

空间索引按 owner domain 解析归属，不把 Region/Blockage 等 ID 当作 Net。固定 Via 模板只能通过 owner 查询命中，避免局部模板混入全设计版图查询。所有几何和相关 placement/net 关系变更通知 epoch，索引下次查询时重建。

## 读写链路

LEF/DEF importer/exporter、PhysicalDesignService、native TritonRoute、iRT/iDRC、DesignViaService、RoutingContextService 均消费新的 ID 关系。二进制 `DesignGeometryBinaryComponents` 包含新增 typed payload，Design schema 从 11 升到 12；Library 由 4 升到 5，独立的 `LibraryGeometryBinaryComponents` 保存 Via placement；Tech 保持 11。恢复时核对几何所有权，不持久化指针、epoch 或 BBox 索引。旧 Design/Library binary 需从 LEF/DEF 重建。

DEF 5.8 已废弃 COMPONENT 的内联 REGION 矩形语法。导出时把 Instance 的内联 bounds 规范化成一个名称无冲突的、仅用于此次导出的 FENCE Region，COMPONENT 引用该名称；不修改源数据库、不降低 DEF VERSION。再导入后是命名 Region 关系，矩形及 FENCE 约束保留；binary 恢复仍保持原来的 inline bounds IDs。这是格式边界的语义转换，不保证 DEF 文本或实体 ID 一一相同。

此次 iRT 差分还发现并修复了临时 `OrientedGeometryCache` 的生命周期错误：`entt::dense_map` 增加元素会移动已存的 cache 对象，工作列表保存其裸指针导致 OpenMP 阶段悬空。现在工作项只保存已有的 master/orientation，串行构造缓存完毕后，并行只读阶段按这两个值取内容；没有新增逐 Shape 的防御检查。

Library PORT/OBS 的 materialize→Component 插入路径补齐异常清理，防止分配失败留下孤立几何。Library binary 恢复只在入口检查 owner 关系、exact-one payload 和 payload→metadata 的双向对应；损坏快照拒绝测试覆盖双 payload 以及脱离 owner 的孤立 payload。正常读取不执行这些全量检查。

## 主要代码入口

以下路径均相对 `src/database/eccdb/`。

| 代码 | 作用 |
| --- | --- |
| `storage/design/DesignRegistry.h`、`DesignStore.*` | Design 统一几何所有权与 storage 绑定、恢复入口 |
| `storage/geometry/GeometryEntity.h`、`GeometryRegistry.h`、`DomainGeometryComponents.h` | 独立 EnTT Shape ID、owner/kind 元数据、typed payload |
| `storage/design/geometry/DesignGeometryStorage.*` | 借用读取、坐标变换、编辑、owner 关系清理、恢复一致性 |
| `storage/design/geometry/DesignGeometrySpatialIndex.*` | 根据 owner 解析 Net/Wire 关系、延迟重建 Boost.Geometry R-tree |
| `storage/design/{netlist,constraint,fill,floorplan,global,guide,routing}/` | 所有者改为 ID；输入值 materialize；删除级联释放 |
| `storage/library/{master_port,master_term,cell_master}/`、`common/LibraryGeometry.h` | PORT/OBS Via placement 改独立实体；Port/Term/Master 删除清理 |
| `service/query/PhysicalDesignService.*`、`service/context/RoutingContextService.*`、`service/routing/DesignViaService.*` | 服务直接解析新 canonical 几何 |
| `io/def/DefDesign{Importer,Exporter}.cpp`、`io/lef/LefLibraryExporter.cpp` | parser 临时值与 canonical IDs 的转换；DEF/LEF 写出 |
| `io/binary/Binary{Schema,Payload,DatabaseImporter}.*` | 新 payload/schema、typed owner 恢复检查 |
| `api/internal/StorageConversions.*`、`api/Database.cpp`、`api/Via.cpp` | C++ API 输入转换与 Via 读取 |
| `adapters/{irt,idrc,tritonroute}/` | iRT/iDRC/native DRT 读取新几何；iRT 临时缓存修复 |
| `tests/unified_geometry/`、`tests/unit/design/`、`tests/io/`、`tests/differential/` | 迁移测试、编辑/持久化测试及差分合同更新 |
| `doc/run_tests.sh` | 恢复 design/DEF/binary/diff 测试组；all 不重复跑同一 focused target |

`OpenRoadNativeEccdb.diff` 保存外部 OpenROAD 集成改动；生成源码位于 `build/openroad-native-eccdb-src`。本轮未改本地上游 OpenROAD/KLayout 仓库。

## 验证

使用现有 Release 构建目录 `build/adapter-differential`；native DRT 使用 `build/openroad-native-eccdb-build`。本轮构建通过，包含 `eccdb_database`、下表测试目标和 `eccdbNativeRoute`。没有运行性能 replay、百万级 routing 或重新生成大型 JSONL。

| 验证 | 当前结果 | 范围 |
| --- | ---: | --- |
| GeometryRegistry | 2/2 通过 | 自由 ID、typed payload |
| Unified Geometry | 20/20 通过 | 编辑、BBox/owner/Net/Wire 筛选、Pin 坐标、级联清理、回滚、恢复一致性 |
| Geometry persistence | 4/4 通过 | 全部新增 Design payload；编辑后 binary/DEF 恢复 |
| Design/Service | 69/69 通过 | netlist/constraint/floorplan/fill/guide/via/PA/service |
| Design binary | 6/6 通过 | 排除大规模 ISPD19 stress case |
| Tech/Library binary | 5/5 通过 | 包含 PORT/OBS Via IDs、快照与级联删除、损坏 payload 恢复拒绝；排除完整 PDK stress case |
| DEF round-trip | 12/12 通过 | 另有 2 项既有 DISABLED 测试，未执行 |
| OpenROAD golden/语义差分 | 3/3 通过 | LEF 5.8、DEF 5.8 完整语法 fixture 与差异检测自检 |
| iDRC 选定差分 | 3/3 通过 | 自检、GeneratedVia/regular Wire、ISPD18 sample |
| iRT 选定 EccDB 相关测试 | 5/5 通过 | 两项自检、native writeback/回环、GeneratedVia wrap、ISPD18 sample wrap |
| Native DRT ODB/EccDB fixture | 通过 | orientation/geometry/placement；两端写出再由两端读入的交叉回环 |

以上按测试名称去重统计为 **129 个 GTest case 通过**，另有 native shell fixture 差分。首次 Unified 有一项 fixture 不合法、persistence 有一项 inline REGION 丢失、iRT sample 有缓存悬空崩溃；修正后只重跑失败或受改动影响的用例。原失败日志与后续通过日志都归档，不能把原日志当最终状态。

仍如实保留的边界：

- `Writeback.IdbGeneratedViaPreservesOriginAndOffset` 失败：纯 iDB writer 丢失 `+ ORIGIN 10 -20` 与 `+ OFFSET 3 4 -5 -6`，该 case 不调用 EccDB。本日之前的基线日报已记录同一问题。本轮不修改 iDB。
- Native DRT 的 `frNet` 不支持 EccDB VIRTUAL route point，明确拒绝；ODB 接受该 fixture。旧脚本期待两端均接受，现修正为显式检查这项能力差异。SPECIALNET POLYGON 仍由两端拒绝。
- DEF round-trip 两项既有 disabled case 涉及 same-point RECT-after-Via 和同一 path 的 mask 变化，未宣称通过。
- 未执行需要 OpenDB Python runtime 的通用 corpus normalization；本轮运行了 golden 比较和实际 native ODB/EccDB 差分。
- 本次完成 canonical 几何迁移与 Storage/Service 编辑闭环；Undo、增量拓扑修复、接上 GUI 前端和完整 DRC 几何求解尚未实现。参数与 PA 派生结果不是遗漏的第二份 canonical Shape。

关键复现命令（从仓库根目录执行）：

```bash
cmake --build build/adapter-differential --target eccdb_unified_geometry_test eccdb_geometry_persistence_test eccdb_database eccdb_design_test eccdb_design_binary_database_archive_test eccdb_binary_database_archive_test eccdb_def_design_roundtrip_test eccdb_opendb_design_differential_test irt_adapter_differential_test idrc_adapter_differential_test -j8
CCACHE_DISABLE=1 cmake --build build/openroad-native-eccdb-build --target eccdbNativeRoute -j8
./bin/eccdb_unified_geometry_test
./bin/eccdb_geometry_persistence_test
./bin/eccdb_design_test
./bin/eccdb_design_binary_database_archive_test --gtest_filter='-DesignBinaryDatabaseArchiveTest.LargeIspd19ShapeRoutingReachesByteExactFixedPoint'
./bin/eccdb_binary_database_archive_test --gtest_filter='-BinaryDatabaseArchiveTest.FullSky130AndIhp130ReachBinaryFixedPoint'
./bin/eccdb_def_design_roundtrip_test
OPENROAD_SOURCE_DIR=/home/zhuxu1/workspace/OpenROAD ./bin/eccdb_opendb_design_differential_test --gtest_filter='StructuredDesignSemanticDifferTest.*:OpenRoadCompleteSyntaxDifferentialTest.*'
ECCDB_ISPD18_ROOT=/home/zhuxu1/workspace/reference/ispd2018 OMP_NUM_THREADS=4 ./bin/idrc_adapter_differential_test --gtest_filter='SelfCheck.*:Shapes.GeneratedViaAndRegularWireMatch:Wrap.Ispd18Sample'
ECCDB_ISPD18_ROOT=/home/zhuxu1/workspace/reference/ispd2018 ECCDB_NET_COMPARE_THREADS=4 OMP_NUM_THREADS=4 ./bin/irt_adapter_differential_test --gtest_filter='SelfCheck.*:Writeback.NativeEnttMatchesLegacyIdbDefAndRoundTrips:Wrap.GeneratedDefViaMaterializesAtSpecialNetPoint:Wrap.Ispd18Sample'
bash src/database/eccdb/adapters/tritonroute/TestNativeFixtures.sh /home/zhuxu1/workspace/reference/ispd2018/ispd18_sample2/ispd18_sample2.input.lef
```

原始日志/JUnit 归档在 `workspace/daily/2026年10月3日/results/geometry-migration-20261003`，本文副本为 `decisions/03-editable-geometry/013-COMPLETE-GEOMETRY-MIGRATION-20261003.md`。`code/geometry-migration-20261003/` 保存 tracked worktree patch 与 untracked source archive；两者共同覆盖当前完整 EccDB 工作树，包括此前未提交的 Shape 重构与本次迁移，不能当成单独已提交的 commit。
