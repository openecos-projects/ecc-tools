# Term / Pin 独立实体：详细 v3 trace 三端回放（2026-09-30）

这是本轮 Term/Pin 改造后应对照的详细结果。先前 [8 项回放](Term_Pin_Entity_Trace_Replay_20260930.md) 误选了早期 v1 trace，不能代表 9 月 28 日那份详细记录。本报告用恢复出的同一份 v3 操作序列，对当前独立 entity 模型作全量回放。

## 恢复原始记录

9 月 28 日原始 v3 JSONL 当时作为临时文件删除，但 [原结果 JSON](DrtTrace_Test6_V3_OneRun_20260928.json) 保留了哈希。本轮使用原始 ISPD18 test6 LEF/DEF/Guide、相同 ODB recorder、64 个布线线程，启用 Guide 更新与 Net track 写回，重新录制一次。

新 trace 为 `build/term-pin-v3-20260930.trace.jsonl`，1,164,033,483 bytes；SHA-256 为 `0cfd87eb11d2a08cfc05d4fdd901b9659885319aea5bd099f27ffdb2c59887b8`，**与 9 月 28 日原始 trace 完全相同**。此次保留 JSONL，便于后续回放。

实际 ODB 布线 152.628 s，0 DRC；107,919 Instances、107,701 普通 Nets。DRT 后 RSS 7,958,820 KiB，snapshot 前峰值 7,985,212 KiB。这些是真实 ODB route 的指标，不是 replay API 计时，也不是三后端各自路由的结果。

## 测量与覆盖范围

- 声明 40 组 API，实际出现 34 组，共 1,878,138 条记录。
- ODB/EccDB 完整执行 34 组；iDB 执行共同的 18 组，缺少的 16 组明确标为 unsupported。
- 三端各一个新进程、仅一轮，并发运行，固定 ODB/EccDB/iDB 到 CPU 24/25/26，进程地址空间上限 500 GiB。
- 使用 `std::chrono::steady_clock` 包围每条记录的 `backend.execute(event)`；包含 adapter 调用及返回字段构造。
- 不包含 LEF/DEF/Guide/JSONL parser、建初始数据库、binding、canonicalization、逐事件与最终状态校验。
- 表中 ms 是一整组的累计时间，不是单次 API 延迟。某些读取组只记录 1 次，但这一次会遍历一整批层、库或端口对象。
- 此轮有并发内存带宽竞争，不能用单一样本声称改造前后有稳定的加速比。JSON 中 `median_*` 只是沿用字段名，`repeat=1` 时即唯一样本。

## 34 项完整数据

`—` 表示 iDB 不支持，`<0.001` 表示实际累计时间小于 0.001 ms；它们含义不同。

| API group | 次数 | ODB ms | EccDB ms | iDB ms |
| --- | ---: | ---: | ---: | ---: |
| find_pin | 690,219 | 171.744 | 188.274 | 185.364 |
| find_instance | 107,919 | 110.356 | 54.083 | 85.458 |
| read_instance | 107,919 | 27.049 | 31.417 | 42.901 |
| read_instance_pins | 107,919 | 53.114 | 65.129 | 88.176 |
| replace_instance_pin_access | 107,919 | 2648.763 | 872.555 | — |
| net_pins | 107,701 | 197.172 | 220.261 | 252.457 |
| read_guides | 107,701 | 328.766 | 375.706 | — |
| read_net_attributes | 107,701 | 89.006 | 70.021 | — |
| read_wire | 107,701 | 14.499 | 24.747 | 18.920 |
| replace_guides | 107,701 | 554.414 | 639.495 | — |
| append_track_assignment | 107,589 | 399.297 | 404.574 | — |
| replace_wire | 107,589 | 670.874 | 1340.155 | 772.691 |
| append_bterm_pin_access | 1,211 | 2.102 | 1.720 | — |
| read_bterm_pin_access | 1,211 | 0.333 | 0.195 | — |
| replace_master_pin_access | 117 | 73.748 | 112.937 | — |
| scan_nets | 3 | 15.353 | 7.154 | 22.286 |
| read_blockages | 1 | 0.003 | <0.001 | <0.001 |
| read_bterms | 1 | 4.161 | 1.306 | 1.142 |
| read_cut_layer_rules | 1 | 0.007 | 0.012 | — |
| read_design_vias | 1 | <0.001 | <0.001 | <0.001 |
| read_die | 1 | <0.001 | 0.004 | <0.001 |
| read_gcell_grid | 1 | 0.002 | 0.002 | 0.001 |
| read_library_masters | 1 | 0.079 | 0.073 | 0.073 |
| read_library_mpin_geometry | 1 | 1.875 | 1.767 | — |
| read_library_obstructions | 1 | 0.042 | 0.040 | — |
| read_library_terms | 1 | 0.558 | 0.493 | — |
| read_ndrs | 1 | 0.003 | 0.003 | — |
| read_routing_layer_rules | 1 | 0.032 | 0.033 | — |
| read_tech_header | 1 | 0.003 | 0.005 | — |
| read_tech_layers | 1 | 0.020 | 0.042 | 0.016 |
| read_tech_vias | 1 | 0.034 | 0.030 | 0.026 |
| read_via_generate_rules | 1 | 0.001 | 0.005 | — |
| scan_instances | 1 | 4.560 | 5.280 | 8.999 |
| scan_tracks | 1 | 0.006 | 0.003 | 0.004 |

| 后端 | 已执行项总计 ms | 三端共同 18 组 ms | 验证事件 | unsupported 事件 |
| --- | ---: | ---: | ---: | ---: |
| odb | 5367.977 | 1269.027 | 1,878,138 | 0 |
| eccdb | 4417.523 | 1937.960 | 1,878,138 | 0 |
| idb | 1478.514 | 1478.514 | 1,336,980 | 541,158 |

iDB 的已执行项总计只覆盖共同项，因此不能与 ODB/EccDB 的 34 组总计直接比较。ODB/EccDB 全量累计分别为 5.368 s 和 4.418 s，EccDB 的主要优势来自 Instance Pin Access 写入；共同组上分别为 1.269 s 和 1.938 s，EccDB 的主要差距仍来自 Wire 写回。

具体热点：`replace_wire` 为 ODB 670.874 ms、EccDB 1340.155 ms；`replace_instance_pin_access` 为 2648.763 / 872.555 ms。当前详细负载的 `find_pin` 为 171.744 / 188.274 ms，`net_pins` 为 197.172 / 220.261 ms。它们与旧 8 项的时序和前置数据状态不同，应按各自 trace 判断。

## 正确性和仍未覆盖的操作

ODB/EccDB 均验证 1,878,138 / 1,878,138 事件；iDB 验证 1,336,980，明确跳过 541,158，不支持项不算通过。

三端共同最终状态一致：107,589 条写回 Wire、2,957,852 个 routing primitives、107,701 个 Net-Term 比较和 107,701 个 Wire 比较，未验证 Wire 对象为 0。ODB/EccDB 另完整核对 1,099 个 endpoint-extension 字段；iDB 只比较共同几何/连接。

34 组已经覆盖实际录到的 Tech/Library 读取、InstanceTerm 列表、BTerm/BPin 几何读取、Master/Instance/BPin Pin Access 写回、Guide 更新、Track assignment 和 Wire 写回。当前独立 BPin entity、BTerm+ordinal 兼容入口和独立 InstanceTerm 模型通过这些实际事件校验。

以下 6 组虽然 recorder 支持，但本数据集没有触发；不能给它们编造性能结果：

| 未出现的 API | 次数 |
| --- | ---: |
| append_bterm_stack_wire | 0 |
| clear_wire | 0 |
| define_design_via | 0 |
| read_master_pin_access | 0 |
| read_preferred_pin_access | 0 |
| read_special_wire | 0 |

此设计的 SpecialNet、NDR、via-generate rule 数量为 0。对应读取接口的成功不证明非空规则的完整语义已由这个设计覆盖；运行时新建 design VIA 也未触发。验证的是记录的字段与路由 primitives，不宣称 DEF 字节顺序或所有 path/opcode 拓扑一致。

Term/Pin 模型及代码修改说明见 [OpenROAD_Term_Pin_Model.md](../OpenROAD_Term_Pin_Model.md) 和 [模型测试报告](Term_Pin_Entity_Trace_Replay_20260930.md)。这次没有为性能重复添加代码改动或增加 benchmark 次数。

## 复现和归档

- [完整回放 JSON](Term_Pin_Entity_V3_Trace_Replay_20260930.json)：输入/可执行文件哈希、每组调用次数/ns、每端 support 和校验字段、实际命令。
- [真实 ODB 布线指标](Term_Pin_Entity_V3_RouteMetrics_20260930.json)。
- 原始输出：`build/term-pin-v3-replay-20260930.runs/{odb,eccdb,idb}-1.json` 与日志。
- 原始 trace：`build/term-pin-v3-20260930.trace.jsonl`，本轮保留。
- route 输出与日志：`build/term-pin-v3-route-20260930/`、`build/term-pin-v3-route-20260930.log`。

重新录制的命令：

```bash
source /home/zhuxu1/workspace/OpenROAD/scripts/eccdb-local-env.sh
ulimit -v 524288000
build/openroad-native-eccdb-build/src/drt/eccdbNativeRoute \
  --backend odb \
  --lef /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.lef \
  --def /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.def \
  --guides /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.guide \
  --threads 64 --save-guide-updates --write-net-tracks \
  --trace build/term-pin-v3-20260930.trace.jsonl \
  --output build/term-pin-v3-route-20260930
```

三端并行回放：

```bash
ulimit -v 524288000
python3 src/database/eccdb/benchmarks/DrtTraceComparison.py \
  --trace build/term-pin-v3-20260930.trace.jsonl \
  --lef /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.lef \
  --def /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.def \
  --guides /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.guide \
  --odb build/openroad-native-eccdb-build/src/drt/eccdbDrtTraceOdb \
  --eccdb-idb bin/eccdb_drt_trace_replay \
  --repeat 1 --parallel --cpu 24 \
  --output build/term-pin-v3-replay-20260930.json
```
