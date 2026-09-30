# EccDB 检查点与交接：2026-10-01

本检查点收拢自父提交 `5a020f4318656e77cd9476b6b8344567e2655028` 以来的当前成果，包括先前多轮实验遗留的源代码、native DRT 补丁、trace/replay 工具和紧凑结果。它不是“仅今天代码”的提交。分支仍为 `experiment/eccdb-embedded-instance-pins`；最终完整 commit SHA、变更清单和检查日志归档于 `/home/zhuxu1/workspace/daily/2026年10月1日/`。

## 下个 agent 先读

1. 本文：当前实现、尝试、已知限制和复现入口。
2. [Term/Pin 当前模型](OpenROAD_Term_Pin_Model.md)。
3. [最新 34 项 ODB 基准加速比](results/Term_Access_Optimization_20260930/EccDB_ODB_iDB_Speedup_20260930.md) 与 [原始结果](results/Term_Access_Optimization_20260930/optimized.json)。
4. [Trace 捕获与回放](TritonRoute_Trace_Replay.md)、[native 集成](Native_TritonRoute_Integration.md)。
5. 历史的内嵌 Pin、去缓存/恢复缓存尝试看 [Embedded 实验](Embedded_Instance_Pins_Experiment.md) 和 daily 的 9 月 27/28 日记录。该文档的 schema 4、内嵌 records 等属于历史，不代表当前代码。

精确源码以此 Git 提交为准；daily 旧日目录保存的是当时快照，不会自动跟随后续源码更新。

## 当前对象模型和缓存

| 对象 | 当前存储 | OpenDB 对应 |
| --- | --- | --- |
| LibraryCellMaster / MasterTerm / MasterPin | LibraryRegistry 的独立 entity；Term 持有物理 Pin ID | dbMaster / dbMTerm / dbMPin |
| DesignInstance | DesignRegistry 独立 entity；有序 Term ID 关系由 DesignInstanceTerms component 保存 | dbInst |
| DesignInstanceTerm | 独立 entity；保存 Instance、MasterTerm、普通/特殊 Net ID | dbITerm |
| DesignBlockTerm / DesignBlockPin | 分别是独立 entity；Term 持有 Pin ID，Pin 保存物理几何组、placement 和可选 AP | dbBTerm / dbBPin |
| RECT / POLYGON / VIA、wire paths | geometry/path 值数组；本轮没有为每个几何值新增 entity | 物理 geometry |

`DesignInstancePin`、`DesignIoPin`、`DesignIoPinPort`、`LibraryMasterPort` 是兼容 alias，不是另一份数据。Net 连接逻辑 InstanceTerm/BlockTerm，而不是某个几何 RECT。完整 OA 风格可编辑 Wire/Route/Junction 模型尚未在这次提交中实现。

当前按名查找是“每个 Master 共享 name→ordinal 的 dense_map → Instance 的有序 Term ID → EnTT storage 中的独立 Term”。每个 Instance 不复制一份 pin-name map。

本轮 Term 访问优化**没有新增 Instance/Net → InstanceTerm* 的指针缓存**：Term ID 的 sparse-set 访问仍存在；后续读实例、名称使用此前已有的 `instance_ptr` 与 `master_term_ptr`。已有 `master_term_name_index` 也保留。iRT wrapper 自己的短期、只读 `InstancePinPointerTable` 是包装期间的局部加速表，不是本轮三端 trace 性能中新建的 Service 二级索引。

## 本提交收拢的代码

以下路径相对 `src/database/eccdb/`，根 CMake 另注明。

| 文件 / 目录 | 主要职责和改动 |
| --- | --- |
| `storage/design/common/DesignTypes.h`、`netlist/model/NetlistComponents.h` | Term/Pin 命名、独立 entity 边界和父子 ID；保留旧别名。 |
| `storage/design/netlist/storage/DesignNetlistStorage.*` | 实例 Term 与 BTerm/BPin 生命周期、连接双向关系、BPin CRUD/回滚、名称查询、缓存恢复。 |
| `storage/library/common/LibraryTypes.h`、`master_port/` | MasterPin 语义及旧 MasterPort API 转发；库对象仍共享。 |
| `storage/design/pin_access/` | BPin entity 上的候选 AP；几何改动只失效对应 BPin 的 AP，保留旧 ordinal 入口。 |
| `service/query/PhysicalDesignService.*` | Term/Pin 和规则/几何读取，generated VIA 展开、net abutment/jumper、NDR、wire/guide 等投影。 |
| `service/context/RoutingContextService.*`、`storage/design/guide/` | Guide、Track Assignment、Pin Access 读写与批量操作。 |
| `service/routing/DesignViaService.*` | 固定/生成设计 VIA 的 CRUD 和引用保护。 |
| `service/routing/NetRouteService.*`、`storage/design/routing/` | routed wire 的读取/替换、端点 extension、taper 和 primitive 投影；不是 wire 底层重新设计。 |
| `io/def/DefDesignImporter.cpp`、`DefDesignExporter.cpp` | 独立 BPin PORT 读写、placement/shape/via、断旧连接再直连和路由输出。 |
| `storage/tech/global/`、`io/lef/LefTechImporter.cpp`、`LefTechExporter.cpp` | USEMINSPACING OBS/PIN 等 global 字段的存储、导入/导出、回滚；PIN 旧扩展限制见下文。 |
| `io/binary/BinarySchema.h`、`storage/design/DesignStore.cpp` | Tech schema 9、Library 2、Design 6；runtime cache 不写入，加载后重建。 |
| `adapters/irt/RTInterfaceEnTT.cpp`、`adapters/idrc/DRCInterfaceEnTT.cpp` | 适配独立 Term/BPin；iRT 包装期 Term 指针表；BPin geometry 遍历。 |
| `adapters/tritonroute/NativeEccdb*.cpp`、`NativeRouteMain.cpp` | EccDB 自行读取 LEF/DEF，投影 Tech/Library/Design 到 DRT 的 fr*，无 GUI 路由。 |
| `adapters/tritonroute/Native*Snapshot.*` | 语义/placement 快照，对照与 DEF readback；校验在计时外。 |
| `adapters/tritonroute/OdbTrace*` | 显式 DRT 调用点记录、v3 输入/写回投影、runtime 设计 VIA 事件。 |
| `adapters/tritonroute/OpenRoad*.diff`、`OpenRoadLocalEnv.sh`、shell scripts | 外部 DRT/构建补丁、可恢复的本地环境脚本、构建/fixture/import/route 入口。 |
| `benchmarks/DrtTrace.h`、`DrtTraceEccdbIdb.cpp`、`DrtTraceOdb*` | 流式读取、三端执行、字段投影、支持项判断、即时和最终状态验证。 |
| `benchmarks/DrtTraceComparison.py` | 三端 fresh-process、串行/并行 CPU affinity，支持项和次数守恒、精确 ns 汇总。 |
| `benchmarks/DbOnly*`、`IdbComparison*`、其他 benchmark 适配 | parser 外 fixture 工作负载、三端创建/查询/遍历及 wire 基准。 |
| `tests/unit/design/`、`tests/io/`、`tests/differential/` | 独立对象身份、关系、PA 失效、生成 VIA、DEF/二进制回环与语义对照。 |
| 根 `CMakeLists.txt` | Release C++ flags 补齐 `-DNDEBUG`，修复覆盖默认 flags 时漏掉 NDEBUG。 |

## 尝试与取舍

| 尝试 | 现在的状态与证据 |
| --- | --- |
| 把 InstancePin payload 内嵌进 Instance、取消逐 Pin entity | 历史实验曾测创建速度和 RSS，代码/结果留在旧 daily；当前为对齐 OpenDB 语义，已恢复独立 InstanceTerm entity。不能用 branch 名判断布局。 |
| 去掉内嵌 Pin 指针缓存，和 ODB/iDB 比较，再恢复 | 历史实验；当前保留已有 runtime Instance/MasterTerm/index 缓存。不要把它与此次新增 Term* 索引混淆。 |
| 对标真实 TritonRoute 数据库操作，代替手写 fixture 负载 | 实施 v3 显式 trace；只覆盖声明的调用组，不自动拦截所有 getter 或 router 内部 fr* 操作。 |
| 补工艺/生成 VIA、NDR、SpecialWire、Guide、AP 等 Service 能力 | 当前 API/Storage 和 replay 已补到该 trace 的支持集；非空验证范围仍受 fixture 限制。 |
| 让 native EccDB route 不创建 ODB 数据库对象 | 已实现受支持无 GUI 路径；仍使用/链接 ODB 的值类型、枚举、头文件和库。 |
| 放大到百万实例完整 route + JSON trace | 已取消大 JSON 路线，保留 import-only 历史记录；本次没有新增百万级 route/replay。 |
| 热点 Term 读取减少重复检查、typed storage 按 Net 复用 | 已实施；canonical `instanceTerm` 直接 get；legacy `instancePin` 仍有 checked-ID 行为。 |
| 返回名称时复用 MasterTerm 指针；修正 Release NDEBUG | 已实施；与访问路径优化一起重编译，单轮结果不能拆开归因。 |
| 把加速比统一对标 ODB | 已修正为 ODB/EccDB、ODB/iDB；完整 34 项与三端共同 18 项分开。 |
| 本次提交前检查计时/支持元数据与构建可恢复性 | 每 event 计时描述修正；`all_supported_events` 从实际计数推导；历史文档加状态提示；补存必需 env 脚本。未重新测性能。 |

## 最新性能证据：保留 9 月 30 日实测

ISPD2018 test6：107,919 Instances、107,701 普通 Nets。同一 v3 trace 有 1,878,138 条事件，40 组声明、34 组实际出现；iDB 支持共同 18 组、1,336,980 事件。每端仅一轮，ODB/EccDB/iDB 进程并行固定在 CPU 24/25/26。

| 范围 / 操作 | ODB ms | EccDB 优化前 ms | EccDB 优化后 ms | iDB ms | ODB/EccDB 后 | ODB/iDB 后 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 完整 34 组 | 5400.417 | 4417.523 | 3487.538 | 缺 16 组，不能比全量 | 1.548× | — |
| 三端共同 18 组 | 1259.628 | 1937.960 | 1547.323 | 1430.554 | 0.814× | 0.881× |
| find_pin | 168.163 | 188.274 | 152.648 | 172.395 | 1.102× | 0.975× |
| read_instance_pins | 47.216 | 65.129 | 36.396 | 89.746 | 1.297× | 0.526× |
| net_pins | 201.317 | 220.261 | 221.362 | 243.450 | 0.909× | 0.827× |
| replace_wire | 667.519 | 1340.155 | 1027.890 | 744.512 | 0.649× | 0.897× |

ms 是操作类别累计时间，不是一次 getter 延迟。每 event 使用一对 `steady_clock::now()`；只计 `backend.execute(event)` 及返回字段构造，parser、初始建库、binding、`supports(event)`、规范化和校验均排除。因此不是完整 adaptor/replay wall time。完整 34 组优势主要来自 Instance Pin Access 写回；Net-Pin 本轮未改善，Wire 写回仍是共同项的主要差距。

ODB/EccDB 1,878,138 支持事件全通过，iDB 支持的 1,336,980 事件全通过、541,158 明确 unsupported。最终 107,701 个 Net-Pin 检查、107,701 个 Wire 检查、107,589 条写回 Wire、2,957,852 primitives；ODB/EccDB 还对比 1,099 个 extension 字段。Guide/Track/AP 写入有即时 readback，但没有这些类别各自独立的最终全局快照；不要扩大正确性声明。

六组没有触发：append_bterm_stack_wire、clear_wire、define_design_via、read_master_pin_access、read_preferred_pin_access、read_special_wire。该 test6 的 SpecialNet、NDR、via-generate rule 数量为 0；大表中的空集合读取不证明非空规则全覆盖。runtime VIA 等另有小 fixture。

Trace 保留在 `build/term-pin-v3-20260930.trace.jsonl`，SHA-256：
`0cfd87eb11d2a08cfc05d4fdd901b9659885319aea5bd099f27ffdb2c59887b8`。
约 1.16 GB，不进 Git、不在 daily 再复制；**不要删除，下一轮先核对哈希后直接复用**。旧 executable 保存为 `build/term-pin-access-before-20260930-replay`。本次检查重新构建了 executable，所以其当前哈希可能与历史 JSON 不同；旧结果不会冒充这次重测。

本轮 v3 trace/native 使用原始 LEF。旧 `IspdImportComparison.py` 仍会在临时目录修 Metal9 PRL 行，是另一种历史 parser benchmark；不要用那个 runner 证明直接处理原始输入。

## 使用契约与审查发现

| 项目 | 结论 / 后续边界 |
| --- | --- |
| Library 指针生命周期 | Design 使用期间库定义冻结，尤其 MasterTerm 不删除/重排；二进制恢复后 rebuildLibraryCaches。Instance/InstanceTerm 的 stable storage 不做 sort/compact；BlockPin 不缓存跨 mutation 的长期裸指针。 |
| 热点 live-ID 访问 | instanceTerm、forEachNetPin、connect 依赖内部维护的有效 ID/关系；connect 前需先断旧连接。公共 Database/C API 目前也直接转发 unchecked connect，不是防御性 handle API。 |
| iRT 包装指针表 | 仅限只读包装期；get 依赖 live-ID，没有对破损/stale net 关系做防护。不声称损坏 snapshot 可安全拒绝一切错误关系。 |
| Binary | Tech=9、Library=2、Design=6；runtime ptr/cache 不落盘，加载后重绑，稳定删除只写 live components。旧 schema 被拒绝，未实现迁移；Design 需要配套 Tech/Library，尚无库 fingerprint。 |
| USEMINSPACING PIN | 已知读写缺口：5.5 PIN 可保存；exporter 固定写 5.8 又输出 PIN，而 SI2 parser 在 5.6+ 忽略该旧属性。当前只对标 ODB writer 文本，不保证此旧字段 LEF 自回环。后续应先定兼容策略，再补 export→import 测试。 |
| iDRC BPin VIA | 顶层 BPin via 尚未展开到环境 shape；旧 iDB iDRC 也如此，不是本次确定回归，但不能宣称该能力完整。 |
| Native 多 BPin 信号端 | 沿用 DRT 投影限制；DB 仍保留物理 PORT 分组与身份。 |
| Trace 边界 | 只有 ODB native driver 能录制；三端 replay 不等于三端各自跑 router。六组未测、iDB 缺项、extension 缺项必须显式保留。 |
| 编译可比性 | EccDB/iDB 和 ODB 均有 O3/NDEBUG，但 LTO、_GLIBCXX_ASSERTIONS 等仍不同；不声称编译配置完全等同。 |

## 本次检查与测试

检查使用现有配置目录 `build/adapter-differential`，Release；构建 64 并行，CTest 32 并行，每进程虚拟地址空间上限 500 GiB。这不是 aggregate RSS/cgroup 上限。

- Design/Trace/DEF/Tech/LEF/Binary 目标编译通过；iRT/iDRC 被改 adaptor 的 object 编译通过。
- native `eccdbNativeRoute`、`eccdbDrtTraceOdb` 编译通过；主要是既有 aggregate 初始化/Boost/optional 编译警告，无编译错误。
- 两批定向 CTest 共 **127 passed、5 failed、1 skipped、3 disabled**；5 项失败都是指定 PDK 文件缺失，不是行为断言不一致，不能写“全套测试通过”。完整 `eccdb_design_test` 单独入口也观察到 73 passed、1 disabled；Tech 9 passed。
- 5 项缺数据：Sky130/IHP130 Tech importer 各一项、exporter 各一项，以及 FullSky130AndIhp130ReachBinaryFixedPoint。缺失根为 `scripts/foundry/`；OpenROAD 有局部 IHP 文件，但不是这些测试期望的完整配套 corpus，未替换数据。
- 大型 ISPD19 binary fixed-point 测试是 opt-in，仍 skipped；3 个原有 disabled 未启用。
- native 小 fixture：orientation、regular route、SPECIALNET、VIRTUAL 与 ODB 快照对照通过；orientation/geometry 的两个 writer × 两个 reader DEF 回读通过；两端都拒绝 SPECIALNET POLYGON。结果路径 `/tmp/eccdb-native-fixtures.a8jfF2`，日志入 daily。
- Python 编译检查、shell `bash -n`、源代码/文档 staged whitespace 检查通过。完整 staged `git diff --check` 仅报告原样 `.diff` 的空白 context 行、CSV 的 CRLF 和 Matplotlib SVG 的路径尾空格；这些生成物保留原始字节，例外日志入 daily。最后 trace 元数据调整后再构建/运行已有 trace 单测；没有再次路由或跑 34 项大性能实验。

本次审查发现的 USEMINSPACING PIN 问题已明确保留为未解决项；没有借提交检查改 Wire 底层或引入新的缓存架构。

## native 外部 fork 的恢复与构建

| 项目 | 精确记录 |
| --- | --- |
| 外部源目录 / 分支 | `build/openroad-native-eccdb-src` / `eccdb-native-ispd` |
| 外部 HEAD | `d2698e82e4536a586a5a75be817968f379eba97f`，其上仍有工作树修改 |
| 完整 DRT patch 基准 | upstream `48e8467bceaf`；OpenRoadNativeEccdb.diff 已核对与该基准到当前全部 src/drt 差异逐字相同 |
| DRT patch SHA-256 | `ee729bdcf92c94c140728de968ea06a4b0800cb055d14f6536ddb29739f9ab10` |
| 本地 yaml-cpp patch SHA-256 | `661c4597c98f7ea145b2d9b5801fa9683015eaffc7009b17d8e312c9373327b4` |
| env 快照 SHA-256 | `a2a6f2a45c9c5421bfc9e88f5297950573a92d8fa05fc011b8119aa6d6536a30` |

Git 提交保存了补丁，不会自动把外部 fork 的脏状态提交到另一个仓库。现有 fork 不重复 apply。新建干净 checkout 时，在基准 48e8467bceaf 上 apply 两份 patch；不要依赖 BuildNativeRoute.sh 自动完成这一步。外部 `MODULE.bazel.lock` 是本地解析漂移，与 CMake native 路径无关，本次不归档其内容。

`OpenRoadLocalEnv.sh` 是外部脚本原样备份，需要位于依赖 checkout 的 `scripts/eccdb-local-env.sh` 后再 source，因为它按文件位置推导 root。依赖的 `../OpenROAD/.local` 目录、fmt source、GCC 13、CMake/Ninja/Boost/json、reference corpus 不包含在此源码提交里。

从工作仓库运行：

```bash
ulimit -v 524288000
cmake --build build/adapter-differential \
  --target eccdb_design_test eccdb_drt_trace_test eccdb_drt_trace_replay \
           eccdb_def_design_roundtrip_test eccdb_design_binary_database_archive_test \
  --parallel 64
bash src/database/eccdb/adapters/tritonroute/BuildNativeRoute.sh
bash src/database/eccdb/adapters/tritonroute/TestNativeFixtures.sh
```

完整 34 项 replay 命令在最新结果文档。用户之前选择的是三端各一轮并行；不主动扩成多轮或百万级 JSON。需要比较完整 route 的时候再使用 TestNativeRoute.sh，不能混用 route 总耗时和这张 parser 外 API execute 表。

## 下一步建议

优先沿 `DrtTraceEccdbIdb.cpp → NetRouteService → DesignRoutingStorage` 分析 Wire 写回的转换、分配、替换成本；Net-Pin 还包括 visitor/std::function 与 JSON 投影，当前结果不能证明差距来自 EnTT 本身。若要进一步加指向 InstanceTerm 的缓存，这是新的实验，应从本 commit 新开分支并保留无缓存基线。

需要增加 API 防护或 Library 可编辑能力时，先重新确定已有冻结/live-ID 契约；不要未经说明把校验塞回计时热点。需要修 USEMINSPACING PIN 时，单独决定 LEF 版本/旧扩展策略，不以文本含该语句当成回环正确性证明。
