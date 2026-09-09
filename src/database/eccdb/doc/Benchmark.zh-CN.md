# EccDB 性能测试指南

[English](../benchmarks/README.md) | [统一测试脚本](Test_Runner.zh-CN.md) | [数据来源](Data_Sources.zh-CN.md)

Benchmark 测量当前实现的耗时、驻留内存和工作量。正确性需要另行运行差分测试；性能程序正常退出或输出 `ok=true`，不能证明两条路径的全部数据库语义一致。

## 1. 三类 benchmark

| 可执行文件 | 测量范围 | 对照与边界 |
| --- | --- | --- |
| `eccdb_benchmark` | LEF/DEF 导入、连接关系/几何遍历、导出、追加 routing 数据 | 支持独立 iDB/EccDB；不是 iDB→EccDB 转换测试 |
| `irt_input_benchmark` | 已加载的源数据库转换为 iRT `Database` | iDB/EccDB wrapper；计时不包含 LEF/DEF 解析，不执行布线算法 |
| `eccdb_binary_archive_benchmark` | 文本导入、Tech/Library/Design 存档写入和恢复；也可从存档导出 DEF | 仅 EccDB，没有 iDB 二进制对照；恢复后检查实体与 routing 计数 |

这里没有独立的 iDRC 性能目标。`test idrc` 是正确性差分，不能等同于 iDRC benchmark。`test route` 才会执行实际 iRT 布线差分，也不是本文的 wrapper 性能测量。

## 2. 通过脚本运行

在源码根目录设置脚本位置，数据配置方法见统一测试脚本文档：

```bash
cd /path/to/workspace/ecc-tools
RUNNER="$PWD/src/database/eccdb/doc/run_tests.sh"

"$RUNNER" doctor benchmark
"$RUNNER" benchmark
"$RUNNER" benchmark --case ispd19_test8 --case ispd19_test10 --repeat 3
"$RUNNER" benchmark --no-build --case ispd19_test10 --repeat 3
"$RUNNER" --config /path/to/test-config.sh benchmark \
  --case ispd19_test8 --repeat 3 --output /path/to/results
```

默认先检查输入文件，再配置 Release 根工程并增量构建三个目标。`--no-build` 只适用于确认现有二进制与待测源码一致时；不会自动保证产物未过期。

| 脚本参数/配置 | 含义 |
| --- | --- |
| `--case NAME` | ISPD2019 case，可重复指定；默认 `ispd19_sample3`，不要重复填写同一名称 |
| `--repeat N` | 每个 case 的重复次数，默认 1；至少 3 次便于观察波动 |
| `ECCDB_ISPD19_ROOT` | 扁平输入目录根，查找 `NAME/NAME.input.lef` 和 `NAME/NAME.input.def` |
| `--jobs N` / `ECCDB_BUILD_JOBS` | 构建并行度；不是同时运行 N 个 benchmark |
| `OMP_NUM_THREADS` | OpenMP 线程设置；不表示所有测量步骤都会使用这些线程 |
| `--output DIR` | **结果父目录**，脚本每次创建唯一子目录 |

每个 case 每轮运行 5 个独立进程，全部串行：通用 benchmark 的 iDB/EccDB、iRT wrapper 的 iDB/EccDB，以及一次二进制存档。偶数轮交换 iDB/EccDB 的先后顺序。因此两个 case、各 3 轮共运行 `2 × 3 × 5 = 30` 次。

当前脚本固定使用 `.input.def`，并给通用 benchmark 传入 `--writes 256`；不支持将 `--source`、`--lef`、`--def`、`--writes` 直接透传。需要单独测某一路径、自定义 routed DEF 或改变追加次数时，按下一节直接调用可执行文件。

`--large` 控制正确性测试的压力用例，不控制 benchmark；选择 `--case ispd19_test10` 就会测量该大型输入。`test all` 不包含 benchmark。

## 3. 单独构建和运行

三类目标一起构建需要完整 ecc-tools 根工程及其依赖，不能只用独立 EccDB 构建替代 iRT 依赖：

```bash
repo=/path/to/workspace/ecc-tools
build="$repo/build/eccdb-benchmark"
cmake -S "$repo" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DECCDB_BUILD_BENCHMARKS=ON -DECCDB_BUILD_TESTS=OFF
cmake --build "$build" --parallel "$(nproc)" --target \
  eccdb_benchmark irt_input_benchmark eccdb_binary_archive_benchmark

bin="$repo/bin" # 根工程默认输出位置，自定义构建请按实际位置调整
corpus=/path/to/workspace/reference/ispd2019
case_name=ispd19_test8
lef="$corpus/$case_name/$case_name.input.lef"
def="$corpus/$case_name/$case_name.input.def"
results=$(mktemp -d /tmp/eccdb-benchmark.XXXXXX)
```

通用 benchmark，分进程比较两条路径：

```bash
"$bin/eccdb_benchmark" --lef "$lef" --def "$def" \
  --source idb --writes 0 --output "$results/access-idb.jsonl"
"$bin/eccdb_benchmark" --lef "$lef" --def "$def" \
  --source entt --writes 0 --output "$results/access-entt.jsonl"
```

`--source both` 可以做冒烟检查，但两条路径共享同一进程的 allocator/峰值历史，不适合做公平的 RSS 对照。`--writes N` 仅对 EccDB 的追加操作有效；`0` 表示不测该操作。

iRT wrapper：

```bash
"$bin/irt_input_benchmark" --lef "$lef" --def "$def" \
  --source idb --output "$results/wrap-idb.jsonl"
"$bin/irt_input_benchmark" --lef "$lef" --def "$def" \
  --source entt --output "$results/wrap-entt.jsonl"
```

二进制写入和恢复：

```bash
"$bin/eccdb_binary_archive_benchmark" --lef "$lef" --def "$def" \
  --archive-dir "$results/archives" --output "$results/binary.jsonl"
```

保存的 `technology.edb`、`library.edb`、`design.edb` 可以作为另一次运行的输入，例如测量存档加载和 DEF 导出：

```bash
"$bin/eccdb_binary_archive_benchmark" \
  --source-archive-dir "$results/archives" \
  --def-export "$results/restored.def" --output "$results/binary-to-def.jsonl"
```

二进制程序要求输入二选一：`--lef/--def` 或 `--source-archive-dir`；输出模式二选一：`--archive-dir` 或 `--def-export`。存档来源和目标应使用不同目录。

直接调用时，`--output` 是 **JSONL 文件路径**，与脚本的结果父目录选项不同。通用和 iRT input 程序追加写入已有 JSONL；binary 程序覆盖指定 JSONL。每次使用独立输出路径，避免旧测量混入新结果。三个程序均支持 `--help`。

## 4. 选择输入数据

| 输入 | 适合测量的场景 |
| --- | --- |
| `ispd19_sample3.input.def` | 快速确认导入、wrapper 和存档入口可运行 |
| `ispd19_test1` 到 `ispd19_test10` 的 `.input.def` | 逐步扩大实例/net 规模，测加载、连接关系、放置几何与 wrapper |
| sample 包的 `.solution.good.def` | 小型已布线几何遍历 |
| 官方最终参赛作品 `test10/def/12.t10.def` | 大规模 routed wire/path/point/via 遍历与存档压力 |

输入数据需要单独获取，不假定随源码仓库提供。大型参赛作品的 Google Drive 入口和组合目录见 [数据来源](Data_Sources.zh-CN.md)。普通 `.input.def` 不包含完整信号布线，不能用它代表大规模 routed geometry 的访问性能。

例如，直接测量队伍 12 的最终作品，而不是同名 case 的原始输入：

```bash
lef="$corpus/input/ispd19_test10/ispd19_test10.input.lef"
def="$corpus/solutions/extracted/test10/def/12.t10.def"
"$bin/eccdb_benchmark" --lef "$lef" --def "$def" \
  --source entt --writes 0 --output "$results/routed-entt.jsonl"
"$bin/eccdb_benchmark" --lef "$lef" --def "$def" \
  --source idb --writes 0 --output "$results/routed-idb.jsonl"
```

这类输入的路由记录数量和资源开销远高于原始 `.input.def`，应分别报告。

## 5. 操作名称与计时范围

| operation | 测量内容 |
| --- | --- |
| `lef_read` / `def_read` | 通用 benchmark 的 LEF 技术/库导入和 DEF 设计导入 |
| `net_pin_forward` | 普通 net 到实例/IO pin 的遍历 |
| `pin_net_reverse` | 实例/IO pin 到 net 的遍历 |
| `placed_geometry` | 放置后的实例 OBS、pin/port 几何遍历 |
| `regular_route_geometry` / `special_route_geometry` | 普通/特殊 net 的布线数据遍历 |
| `tech_floorplan_access` | 技术层、VIA、row、track/GCell grid 数据访问 |
| `lef_tech_write` / `lef_library_write` / `def_write` | LEF/DEF 导出；前两项仅 EccDB |
| `routing_batch_append` | 向 EccDB 的一个普通 net 追加 N 个两点 routing path，没有对应 iDB 对照 |
| `irt_database_wrap` | 从已加载源库调用 iRT wrapper；不含源文件解析及完整布线 |
| `lef_text_import` / `def_text_import` | binary benchmark 的文本导入前置步骤 |
| `binary_export_total` / `binary_import_total` | Tech/Library/Design 三份存档写入/恢复计时的合计 |
| `binary_export_*_total` / `binary_import_*_total` | 单份 Tech、Library、Design 存档的计时 |
| `source_binary_import_*` / `def_text_export` | 使用已有存档作为输入时的加载，以及可选 DEF 导出 |
| `restored_counts` | 实体/routing 数量和存档总字节数；不是计时记录 |

二进制 `*_total` 包含 archive header 和 payload 的序列化，不代表包含进程启动、完整析构、计数核验和所有文件系统开销的端到端时间。iDB 的 `saveLef` 是设计 macro 导出，与 EccDB 的技术/库导出不等价，因此没有成对比较。

## 6. 结果字段与 CSV

不同程序的 JSONL schema 有差别，并非每条记录都具有下列所有字段：

| 字段 | 含义 |
| --- | --- |
| `elapsed_ns` | 操作耗时，纳秒；除以 `1e9` 得秒 |
| `input_bytes` / `output_bytes` | 输入/输出文件大小，字节；不是内存占用 |
| `records` / `edges` / `shapes` | 操作定义的工作量计数；先核对口径再比较吞吐量 |
| `throughput_*_per_s` | 对应工作量除以计时；零工作量不能解释为有效访问性能 |
| `rss_before_kib` / `rss_after_kib` | 操作前后进程的驻留内存，KiB |
| `rss_delta_kib` | 程序报告的 RSS 增量，不是独立对象分配量，也不保证呈现负增量 |
| `peak_rss_kib` | 截至该时刻的进程峰值，可能来自之前的操作 |
| `allocator_*_kib` | allocator 报告的内存，不等于 RSS 或存活对象的精确大小 |
| `nets` / `pins` / `materialized_shapes` | iRT wrapper 的实际物化数量；形状包含 pin routing/cut shapes 及 routing/cut obstacles |
| `threads` | iRT input 程序记录的 OpenMP 最大线程设置，不是实测 CPU 利用率 |
| `archive_bytes` | 三份存档的文件总字节数 |
| `ok` | 通用 benchmark 的执行状态；仍需核对退出码与非空写出，不是语义正确性证明 |

脚本生成 `summary.csv`：

```text
case,source,operation,samples,median_seconds,min_seconds,max_seconds,median_rss_mib
```

- 按 `case/source/operation` 汇总，`samples` 为该项实际记录数，应核对是否等于预期重复次数。
- `median_seconds` 是重复运行的操作耗时中位数；最小/最大值帮助判断波动。
- `median_rss_mib` 来自 `rss_after_kib / 1024`，不是峰值或增量。
- binary JSONL 未包含 `source` 时，脚本在汇总中标记为 `entt`。
- 各程序的 `case` 命名可能保留或去掉 `.input` 后缀；对照时结合原始文件名，不能仅按字符串把不同输入混在一起。
- `restored_counts` 没有 `elapsed_ns`，保留在原始 JSONL，不进入计时 CSV。

## 7. 如何解释比较结果

比较同一输入、同一编译优化、同一线程设置下的重复测量。至少同时报告耗时、工作量和内存；不能只选有利的单次结果。脚本不清理系统文件缓存、不隔离其他工作负载，因此重复测量主要反映缓存可用条件下的性能。

加速比可定义为 `iDB 耗时 / EccDB 耗时`：大于 1 表示 EccDB 更快。RSS 降幅可定义为 `1 - EccDB RSS / iDB RSS`。两者应分别计算，不能用文件大小替代 RSS。

还需要区分：

- wrapper 后的 RSS 包含仍驻留的源数据库和 iRT 数据库，不是 iRT `Database` 的独立大小。
- 两条 wrapper 的派生状态计算时机可能不同，计时反映当前实现的实际成本；配合差分结果解释。
- `special_route_geometry`、`tech_floorplan_access` 等步骤的两条路径可能采用不同遍历与计数口径，不能直接把同名 operation 的耗时比解释成相同工作量的加速比。
- 普通 net 没有 routed 数据时，该项只测空数据遍历；用完整 routed DEF 补测。
- 二进制读回紧接写出，通常命中文件缓存；恢复阶段的进程峰值仍包含此前文本导入的历史峰值。测独立加载应使用 `--source-archive-dir` 的新进程。
- 二进制恢复计数相等不代表全部字段、几何或 byte-exact 固定点一致，仍需 binary/差分测试验证。

## 8. 产物与失败处理

脚本在指定结果父目录下创建唯一子目录，保存命令、环境、源码版本、日志、JSONL、CSV 以及二进制存档。失败命令、`ok=false` 和被检查到的空写出会使脚本返回非零；有 CSV 文件不等于整次运行成功。

长期保留结果时，建议一并记录输入文件 SHA256、编译器版本和机器规格。默认脚本不会生成输入 SHA256 或完整机器清单，需要报告作者另行记录；这些运行信息保存在结果目录，不应直接写入公共使用文档。结束后可以按需删除本次结果目录中的存档；删除前确认仍需保留的日志和原始数据。
