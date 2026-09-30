# Term / Pin 独立实体改造与三端 trace 回放（2026-09-30）

本报告仅使用旧 v1 的 8 组操作。9 月 28 日的详细 v3 trace 已重新恢复并补测，34 组完整记录及新结果见 [详细 v3 回放报告](Term_Pin_Entity_V3_Trace_Replay_20260930.md)。两份 trace 覆盖范围不同，勿混用总计。

本轮按 OpenROAD 的命名和逻辑/物理分层完成 Term / Pin 模型，随后用上次真实 TritonRoute trace，在 ODB、EccDB、iDB 上各并行回放一轮。当前工作分支为 `experiment/eccdb-embedded-instance-pins`，但这次改造已将 InstanceTerm 恢复为独立 entity，不再内嵌 InstancePin payload。

## 本轮模型和代码

完整对象说明见 [OpenROAD_Term_Pin_Model.md](../OpenROAD_Term_Pin_Model.md)。

| 对象 | 对应 OpenDB | 本轮状态 |
| --- | --- | --- |
| `LibraryCellMaster` | `dbMaster` | 独立 entity，已有。 |
| `LibraryMasterTerm` | `dbMTerm` | 独立 entity，已有；持有 MasterPin ID。 |
| `LibraryMasterPin` | `dbMPin` | 独立 entity；将原 MasterPort 类型明确为 MasterPin，旧名称保留 alias。 |
| `DesignInstance` | `dbInst` | 独立 entity；持有独立 InstanceTerm 的有序 ID 列表。 |
| `DesignInstanceTerm` | `dbITerm` | 独立 entity；引用 Instance、MasterTerm 和 Net，保留热点 runtime pointer。 |
| `DesignBlockTerm` | `dbBTerm` | 独立 entity；保存逻辑 I/O 属性和 BlockPin ID 列表。 |
| `DesignBlockPin` | `dbBPin` | 新的独立 entity；保存自身 placement、geometry/via、所属 BlockTerm。 |

几何 RECT/POLYGON/VIA 仍是 Pin 内的 vector 值。本轮未把每个几何值都创建为 entity，也未改 Wire/Path 的存储模型。Net 引用逻辑 Term，物理 Pin 提供可接入的几何。

| 代码 | 职责 |
| --- | --- |
| `storage/design/common/DesignTypes.h`、`storage/design/netlist/model/NetlistComponents.h` | 明确 Term/Pin 类型、独立 ID、所属关系；旧类型作为兼容 alias。 |
| `storage/design/netlist/storage/DesignNetlistStorage.{h,cpp}` | 创建/更新/删除独立 ITerm、BTerm、BPin；维护 owner 和有序 ID 关系；提供简单的 canonical API 名称。 |
| `storage/library/common/LibraryTypes.h`、`storage/library/master_port/` | MasterPin 的 canonical 类型和 API；沿用原有独立库实体。 |
| `storage/design/pin_access/`、`service/context/RoutingContextService.{h,cpp}` | BPin 的 AP component 直接挂在 BPin entity；原 BTerm+ordinal API 仅作入口转换。 |
| `io/binary/BinarySchema.h` | Design schema 6；分别序列化 Term、Pin 和 ID；runtime pointer 不入盘，加载后重建。旧 schema 的设计快照需重新生成。 |
| `io/def/DefDesignImporter.cpp`、`DefDesignExporter.cpp` | DEF PORT 与独立 BlockPin 的读写；保留分组和独立 placement。 |
| `service/query/PhysicalDesignService.{h,cpp}` | 提供 Term、Pin 查询和单个 BPin geometry 遍历，保留原聚合入口。 |
| `adapters/tritonroute/NativeEccdbDesign.cpp`、`NativeDatabaseSnapshot.cpp` | 从新模型投影到 DRT；沿用 ODB reader 对 signal 多 BPin 的拒绝、supply 多 BPin 的聚合策略。 |
| `adapters/irt/RTInterfaceEnTT.cpp`、`adapters/idrc/DRCInterfaceEnTT.cpp` | 更新独立 BPin 的读取；iRT 的 InstanceTerm 指针表以 entity slot 索引并保留完整 generation ID。 |
| `benchmarks/DrtTraceEccdbIdb.cpp`、`DrtTraceComparison.py` | 更新 replay 消费路径；新增 `--parallel`，三端使用不同 CPU 并发运行。 |

没有增加新的影子表。Master 的名称索引仍共享，InstanceTerm 的 Instance/MasterTerm pointer cache 保留；缓存不随二进制保存。Instance/InstanceTerm 使用 in-place deletion；BlockPin 的长期关系用 ID 保存，不新增长期 BlockPin pointer cache。

## 数据和计时范围

- 使用原始 ISPD18 test6 LEF/DEF：107,919 Instances、107,701 Nets。
- 复用 `build/drt-trace-test6-20260928.W8Z1f3/trace.jsonl`，共 1,229,052 条记录；未重新生成大 trace，也未重新跑三遍布线。
- 每端仅一轮，并行固定 CPU：ODB 24、EccDB 25、iDB 26；虚拟地址空间上限 500 GiB。
- 使用 `std::chrono::steady_clock` 包围每条记录的 `backend.execute(event)`。
- 下表是整组 API 的累计毫秒数，包含 adapter 调用和返回字段构造；不是单次 API 延迟。LEF/DEF/JSONL parser、初始化/binding、canonicalization、正确性校验和最终状态比较均不在计时内。
- Trace 捕获的是真实 DRT 数据库 API 组。这份旧 trace 覆盖以下 8 组，没有覆盖完整 Tech/Library、SpecialNet、Guide、PA 和 DRC API；新增 BPin/AP 实体行为另由定向测试验证。

## 最终单轮结果

| API 组 | 次数 | ODB ms | EccDB ms | iDB ms | EccDB / ODB |
| --- | ---: | ---: | ---: | ---: | ---: |
| 查 Instance `find_instance` | 107,919 | 91.36 | 38.86 | 74.18 | 0.43 |
| 按名查 InstanceTerm `find_pin` | 690,219 | 143.21 | 160.82 | 168.03 | 1.12 |
| Net-Term 及关联字段 `net_pins` | 107,701 | 245.62 | 239.36 | 262.20 | 0.97 |
| 读 Instance `read_instance` | 107,919 | 21.89 | 30.11 | 39.85 | 1.38 |
| 读 Wire `read_wire` | 107,701 | 15.08 | 24.69 | 18.69 | 1.64 |
| 替换 Wire `replace_wire` | 107,589 | 676.22 | 1314.01 | 770.87 | 1.94 |
| 扫描 Instance `scan_instances` | 1 | 4.18 | 5.70 | 9.86 | 1.36 |
| 扫描 Net `scan_nets` | 3 | 14.60 | 7.13 | 23.35 | 0.49 |
| **总计** | **1,229,052** | **1212.16** | **1820.67** | **1367.03** | **1.50** |

汇总程序的字段沿用 `median_*` 名称，但 `repeat=1`，此处就是唯一样本。并行运行会共享内存带宽，不能把这轮与以前串行三轮的结果当成严格的改造前后加速比。

本轮 EccDB 的 `replace_wire` 占累计耗时 72.2%，是总差距的主要来源；按名查 InstanceTerm 约慢 12%，Net-Term 遍历略快。下一步值得单独分析 Wire 写回中的分配、属性投影和替换成本，不能把这部分差距归因于新 Term/Pin 实体模型。

## 正确性与编译验证

三端均验证 1,229,052 / 1,229,052 条记录，8 组均全量 measured，unsupported 为空。共同最终状态：

- 107,589 条已写回 Wire，2,957,852 个 routing primitive。
- 107,701 次最终 Net-Term 比较和 107,701 次最终 Wire 比较。
- `unvalidated_wire_objects=0`，共同几何/连接校验通过。
- ODB/EccDB 完整比较 1,099 个 endpoint-extension 字段；iDB 仅验证共同 geometry/connectivity，不具备这一项完整 extension 语义比较。
- `final_vias=0` 指本 trace 没有运行时新建的 design VIA 定义，不表示 Wire 中没有 VIA primitive。此次不能证明 runtime VIA 创建路径已被大型负载覆盖。

编译通过：replay、Design/DEF/Binary 测试程序、原生 `eccdbNativeRoute` / `eccdbDrtTraceOdb`，以及更新后的 iRT/iDRC adaptor 编译单元。使用本地 OpenROAD 环境和最多 128 个编译 job。

定向测试结果：

- `eccdb_design_test`：73 个通过；1 个已有 disabled。覆盖独立 ITerm 身份/删除、BPin 增删改及 owner 关系、创建中途失败回滚、编辑一个 BPin 只清除其 AP、删除/更新不影响 sibling AP、单个与聚合 geometry。
- `eccdb_design_binary_database_archive_test`：5 个通过；1 个 opt-in 大设计测试未启用。覆盖独立 BPin ID/owner/AP 保存恢复和 runtime cache 重建。
- `eccdb_def_design_roundtrip_test`：11 个通过；2 个已有 disabled。覆盖 DEF PORT/placement/geometry 回环。
- `eccdb_drt_trace_test`：格式和 canonical geometry 校验通过。
- `NativeRouteGeometry.def` 经原生 TritonRoute ODB/EccDB reader 导入，`snapshot.txt`、`design.txt`、`placement.txt` 逐文件一致。该 fixture 包含 BPin placement、普通/固定 route、RECT、VIA 和 endpoint extension；只做 import，不重新布线。

## 复现和结果文件

最终汇总已归档为 [Term_Pin_Entity_Trace_Replay_20260930.json](Term_Pin_Entity_Trace_Replay_20260930.json)，包括输入/程序 SHA-256、命令、各组 ns、次数和验证范围。工作目录原始输出为：

```text
build/term-pin-entities-replay-20260930.json
build/term-pin-entities-replay-20260930.runs/{odb,eccdb,idb}-1.json
build/term-pin-entities-replay-20260930.runs/{odb,eccdb,idb}-1.log
build/term-pin-native-smoke-20260930/{odb,eccdb}/
build/term-pin-entities-build.log
build/term-pin-native-build.log
build/term-pin-native-final-build.log
build/term-pin-irt-idrc-build.log
build/term-pin-lifecycle-test-build.log
```

回放命令：

```bash
ulimit -v 524288000
python3 src/database/eccdb/benchmarks/DrtTraceComparison.py \
  --trace build/drt-trace-test6-20260928.W8Z1f3/trace.jsonl \
  --lef /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.lef \
  --def /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.def \
  --odb build/openroad-native-eccdb-build/src/drt/eccdbDrtTraceOdb \
  --eccdb-idb bin/eccdb_drt_trace_replay \
  --repeat 1 --parallel --cpu 24 \
  --output build/term-pin-entities-replay-20260930.json
```

Trace SHA-256：`d078acee269d82b5b8b373a1cd6f490b367538283cd338f8d9364aa7963ea4a2`。
