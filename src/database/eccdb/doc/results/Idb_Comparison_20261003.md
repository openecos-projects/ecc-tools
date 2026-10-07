# IDB / EccDB benchmark after unified Geometry migration (2026-10-03)

这次复用了 `src/database/eccdb/benchmarks/IdbComparison.py` 的两种负载，重新编译当前分支后的三个 benchmark binary，然后在 CPU 24 上各交替运行 3 次，报告中位数。导入、fixture 校验、wire 快照和 JSON 写出都不计入 API 计时。

## DB-only logical-netlist

输入是现有 ISPD18 test6 fixture：117 masters、107,919 instances、690,219 instance pins、107,701 nets、474,330 connections。这个模式不包含几何和 routing。

| 操作 | IDB ms | EccDB ms | EccDB / IDB |
| --- | ---: | ---: | ---: |
| create instances | 219.699 | 58.948 | 0.268x |
| create nets | 53.462 | 19.363 | 0.362x |
| connect | 249.128 | 40.798 | 0.164x |
| find instances | 25.178 | 7.678 | 0.305x |
| find nets | 15.531 | 6.812 | 0.439x |
| find instance pins | 184.020 | 105.273 | 0.572x |
| traverse instances | 0.152 | 0.218 | 1.432x |
| traverse net pins | 4.910 | 4.368 | 0.890x |
| traverse nets | 0.146 | 0.183 | 1.248x |

所有对象计数相等。EccDB 在创建、连接和查找项目仍然更快；遍历 instances/nets 的微小差距属于纳秒级 workload，不能作为主要结论。

## Native regular-wire read

LEF/DEF 使用 ISPD18 test6 routed input；三次正式运行的共同几何计数为 107,589 wires、2,959,046 paths、4,082,289 points、1,391,495 vias、444,308 rectangles。两端 canonical warmup snapshot 字节一致。

| 操作 | IDB ms | EccDB ms | EccDB / IDB |
| --- | ---: | ---: | ---: |
| read regular wire payload | 133.339 | 273.839 | 2.054x |
| read special wire payload | 0.00084 | 0.00064 | no workload |

test6 没有 SPECIALNETS，所以另用 ISPD19 test6 的 SPECIALNET-only DEF 补测：2 special wires、8,378 paths、6,840 vias。

| 操作 | IDB ms | EccDB ms | EccDB / IDB |
| --- | ---: | ---: | ---: |
| read special wire payload | 0.05984 | 0.79355 | 13.261x |

这说明当前迁移后的主要回归在 route payload 扫描路径，而不是逻辑网表查询。当前 `IdbComparisonBenchmark` 只遍历 `routing.forEachShape` 的 regular/special route payload；它不测 Pin/Region/Fill/Die/Core/Guide/Track 等新增 GeometryRegistry owner，也不测空间索引重建或查询。

## RSS（完整导入进程）

| 输入 | IDB peak RSS | EccDB peak RSS |
| --- | ---: | ---: |
| ISPD18 routed test6 | 1,583.3 MiB | 756.1 MiB |
| ISPD19 special-only | 12.9 MiB | 15.0 MiB |

RSS 是完整导入进程值，不是单独数据库对象大小。

## 与 2026-09-27 IDB/EccDB 基线

当时 regular-wire EccDB 为 77.363 ms，本次为 273.839 ms，约 **3.54x**；两次 routed DEF 的 path 数分别为 2,957,852 和 2,959,046，属于近似但不是字节相同的输入，因此这里只作回归信号，不能当作严格 A/B 定论。DB-only 中 `create_instances` 为 31.655→58.948 ms，`find_instance_pins` 为 60.722→105.273 ms。说明统一 Geometry 迁移后，wire 扫描和实例 pin 创建/查找需要单独剖析；本报告没有把旧版和新版混入同一中位数。

## 复现

```bash
taskset -c 24 python3 src/database/eccdb/benchmarks/IdbComparison.py \
  --mode db-only \
  --fixture build/eccdb-differential/ispd18_test6_logical.json \
  --idb bin/eccdb_db_only_idb_benchmark \
  --eccdb bin/eccdb_db_only_benchmark \
  --repeat 3 --keep-raw \
  --output build/adapter-differential/idb-comparison-geometry/ispd18-test6-db-only.json

taskset -c 24 python3 src/database/eccdb/benchmarks/IdbComparison.py \
  --mode wire \
  --lef /home/zhuxu1/workspace/reference/ispd2018/ispd18_test6/ispd18_test6.input.lef \
  --def /tmp/ispd18_test6.routed.def \
  --wire-benchmark bin/eccdb_idb_comparison_benchmark \
  --repeat 3 --keep-raw \
  --output build/adapter-differential/idb-comparison-geometry/ispd18-test6-wire.json
```

权威 JSON：

- `build/adapter-differential/idb-comparison-geometry/ispd18-test6-db-only.json`
- `build/adapter-differential/idb-comparison-geometry/ispd18-test6-wire-rerun.json`
- `build/adapter-differential/idb-comparison-geometry/ispd19-test6-special-wire.json`
