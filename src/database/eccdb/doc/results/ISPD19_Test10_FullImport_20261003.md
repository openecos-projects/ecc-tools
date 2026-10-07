# ISPD2019 test10 全量 DEF 导入 EccDB（2026-10-03）

本次使用参赛选手的 routed DEF `12.t10.def` 做一次完整 EccDB 导入，并用 `/usr/bin/time -v` 记录整个进程的峰值 RSS。

## 输入

| 项目 | 路径 | 大小 |
|---|---|---:|
| LEF | `/home/zhuxu1/workspace/reference/ispd2019/ispd19_test10/ispd19_test10.input.lef` | 326,014 bytes |
| routed DEF | `/home/zhuxu1/workspace/reference/ispd2019/test10/def/12.t10.def` | 2,641,862,207 bytes（约 2.46 GiB） |

DEF 规模：899,404 个 instance、895,253 个 regular net、5,753,641 个 instance pin。该文件属于接近百万实例/百万网络的 routed 设计。

## 结果

| 指标 | 结果 |
|---|---:|
| LEF 导入 | 13.848 ms |
| DEF 导入 | 69.015 s |
| benchmark 记录的总导入 | 69.029 s |
| 进程墙钟时间 | 70.33 s |
| 用户态 / 内核态 CPU 时间 | 62.49 s / 7.69 s |
| 峰值 RSS | **10,775,660 KiB = 10,523.1 MiB = 10.276 GiB** |
| 峰值 RSS（十进制） | 11.034 GB |
| Swap | 0 |
| Exit status | 0 |

RSS 是整个导入进程的峰值 resident set，包含 EccDB 对象、EnTT registry、Geometry payload、解析缓冲和运行时索引；它不是只计算最终持久化 payload 的大小。

导入后计数：

```text
layers          18       routing_layers  9       cut_layers 8
tech_vias       67       masters         71      master_terms 636
rows            1281     track_grids     18      instances 899404
instance_pins   5753641  io_pins         3221    regular_nets 895253
special_nets    2        design_vias     12      blockages 0
```

## 复现命令

```bash
/usr/bin/time -v taskset -c 24 \
  bin/eccdb_ispd18_import_benchmark \
  --lef /home/zhuxu1/workspace/reference/ispd2019/ispd19_test10/ispd19_test10.input.lef \
  --def /home/zhuxu1/workspace/reference/ispd2019/test10/def/12.t10.def \
  --output build/adapter-differential/test10/eccdb-import.json
```

原始测量文件：

- `build/adapter-differential/test10/eccdb-import.json`
- `build/adapter-differential/test10/eccdb-import.time.log`

本次只测 EccDB 全量导入 RSS，没有在同一轮运行 ODB/iDB 对照；因此该数字应作为当前 unified Geometry 分支的基线，不能直接解释为三端横向 RSS 对比。
