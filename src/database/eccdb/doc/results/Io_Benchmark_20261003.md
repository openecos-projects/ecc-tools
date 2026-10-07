# LEF/DEF 与 binary I/O 基准（2026-10-03）

本轮针对统一 Geometry 后的当前分支，重新编译了 `eccdb_ispd18_import_benchmark` 和 `eccdb_binary_archive_benchmark`。所有时间均为 `steady_clock` 的 fresh-process 中位数，CPU 绑定 24；计数校验、JSON 输出和进程启动不计入操作计时。

## LEF/DEF 导入

输入是 ISPD18 test6 的同一份修复后 LEF（283,615 bytes）。普通导入使用原始 `ispd18_test6.input.def`；routed I/O 使用 `/tmp/ispd18_test6.routed.def`（160,720,773 bytes，107,589 wires、2,959,046 paths）。

| 输入 | 操作 | 中位数 |
| --- | --- | ---: |
| test6 input DEF | LEF 导入（Tech + Library） | 11.334 ms |
| test6 input DEF | DEF 导入 | 666.064 ms |
| test6 input DEF | LEF + DEF | 677.398 ms |
| routed DEF | LEF 导入（Tech + Library） | 11.226 ms |
| routed DEF | DEF 导入 | 4,484.695 ms |

routed DEF 的额外时间主要来自约 296 万条 route path 的解析和 Geometry/route entity 构造，不能和无 routing 的 input DEF 混为一项。

## Binary 写入与读取

从 routed DEF 导入完成后，将 Tech、Library、Design 分别写成三个 `.edb` 文件；随后释放内存并从 binary 重载。三份文件总大小为 **435,586,491 bytes（约 415.5 MiB）**。

| 操作 | 中位数 | 数据量 |
| --- | ---: | ---: |
| binary export Tech | 0.180 ms | 10,475 bytes |
| binary export Library | 0.651 ms | 354,034 bytes |
| binary export Design | 606.934 ms | 435,221,982 bytes |
| binary export total | **607.753 ms** | 435,586,491 bytes |
| binary import Tech | 0.363 ms | 10,475 bytes |
| binary import Library | 1.104 ms | 354,034 bytes |
| binary import Design | 1,741.510 ms | 435,221,982 bytes |
| binary import total | **1,742.959 ms** | 435,586,491 bytes |
| binary import（独立进程） | **1,732.566 ms** | 435,586,491 bytes |
| binary → DEF export | **627.304 ms** | 143,547,019 bytes |

恢复后的对象计数与文本导入一致：107,919 instances、690,219 instance pins、107,701 nets、107,589 wires、2,959,046 paths、1,391,495 vias、444,308 rectangles。

## 与旧导入结果的关系

2026-09-23 的旧 ODB/EccDB import comparison 曾记录：ODB LEF 8.139 ms、DEF 738.037 ms、总计 746.176 ms；旧 EccDB 总计 812.959 ms。当前 EccDB 对无 routing 的同类输入为 677.398 ms，但旧 ODB 是不同时间构建的历史结果，当前环境缺少 `_odb.so`，因此本轮不宣称新的 ODB 对照结论。

当前 branch 已删除旧的通用 `eccdb_benchmark`，所以本报告采用仍可从当前 CMake 重建的两个 I/O benchmark；binary 没有 IDB 对照，属于 EccDB 自身持久化速度。

## 复现入口

```bash
cmake --build build/adapter-differential --target \
  eccdb_ispd18_import_benchmark eccdb_binary_archive_benchmark -j8

bin/eccdb_binary_archive_benchmark \
  --lef <repaired-test6.lef> --def /tmp/ispd18_test6.routed.def \
  --archive-dir <archive-dir> --output <text-binary.jsonl>

bin/eccdb_binary_archive_benchmark \
  --source-archive-dir <archive-dir> \
  --def-export <reexport.def> --output <binary-to-def.jsonl>
```

本轮中位数机器结果保存在 `build/adapter-differential/idb-comparison-geometry/ispd18-test6-io-median.json`。
