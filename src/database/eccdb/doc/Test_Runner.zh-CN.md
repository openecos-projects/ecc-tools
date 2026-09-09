# EccDB 测试脚本

脚本：[run_tests.sh](run_tests.sh)；配置示例：[test_config.example.sh](test_config.example.sh)。
数据源 GitHub、官网、Google Drive 下载地址和推荐布局见 [数据来源](Data_Sources.zh-CN.md)，也可运行 `./run_tests.sh sources`。
测试含义和 oracle 边界见 [差分测试指南](Differential_Testing.zh-CN.md)，性能指标见 [Benchmark README](../benchmarks/README.md)。

## 直接使用

```bash
cd /path/to/workspace/ecc-tools
RUNNER="$PWD/src/database/eccdb/doc/run_tests.sh"

"$RUNNER" doctor                   # 检查配置的数据与 OpenDB 模块
"$RUNNER" test unit                # 单元测试
"$RUNNER" test io                  # LEF/DEF importer、exporter、round-trip
"$RUNNER" test binary              # 二进制固定点；默认大用例会明确 SKIP
"$RUNNER" test diff                # SI2、legacy iDB、LEF/DEF/OpenDB 差分
"$RUNNER" test irt --filter 'Wrap.*:SelfCheck.*'
"$RUNNER" test idrc
"$RUNNER" test route-smoke         # ISPD18 sample，完整 route + writeback
```

`test` 默认先检查依赖、增量配置/构建，再执行。独立测试与根工程使用不同构建目录；不需要手动重复 export 或输入 CMake 参数。脚本可以从任何目录调用，源码路径默认由脚本位置推导，workspace 默认是源码目录的父目录。配置中的相对文件路径按启动目录解析；工具命令随后在本次结果目录执行，避免工具的默认日志写入源码目录。工具用例串行运行，构建并行度默认 `nproc`。

脚本需要 Bash 4+、Python 3、CMake、Ninja；C++ 编译依赖仍是项目要求的 Boost、GoogleTest 等，配置阶段会给出缺失项。脚本不会自动下载 PDK/OpenROAD、安装依赖或修改 gitconfig，不需要新建 venv，也不使用 Docker。

## 推荐目录布局

以下示例将源码、PDK、比赛数据和 OpenROAD 分开存放；请将 `/path/to/workspace` 替换为自己的工作目录：

```text
/path/to/workspace/
├── ecc-tools/                 ECCDB_SRC：当前源码
├── scripts/foundry/           ECCDB_TEST_DATA_ROOT 下的数据
│   ├── sky130/lef/
│   └── ihp130/ihp-sg13g2/libs.ref/
├── reference/
│   ├── ispd2018/             ECCDB_ISPD18_ROOT
│   └── ispd2019/             ECCDB_ISPD19_ROOT
└── OpenROAD/                  OPENROAD_SOURCE_DIR
    └── bazel-bin/src/odb/     OPENDB_PYTHON_MODULE_DIR / OPENDB_PYTHONPATH
        ├── odb.py
        └── _odb.so
```

`ECCDB_TEST_DATA_ROOT=/path/to/workspace` 是 **CMake 编译时**的 PDK 数据根，不能填成 `reference` 或 `scripts`。改变这个变量后需要重新配置并编译；运行时只改 export 对已经编译的路径不起作用。

`OPENDB_PYTHON` 必须是可执行文件名或解释器绝对路径，不能包含额外命令参数。脚本会将 `python3` 等命令名解析成绝对路径，因为当前 C++ oracle 检查要求解释器是实际文件路径；`OPENDB_PYTHONPATH` 应是一个包含 `odb.py`、`_odb.so` 的目录。脚本用该解释器实际执行 `import odb` 和创建数据库，能发现 `_odb.so` 的 Python ABI 或动态库加载问题。模块目录既传给 CMake，也通过运行时环境传给测试；不会修改全局 `PYTHONPATH`。本脚本走 Python oracle 路径；需要 `odbtcl` 时按差分指南手动配置。

doctor 检查完整 Sky130/IHP130 LEF 清单（从当前 `LefPdkCorpus.h` 提取）、ISPD 目录及 OpenROAD 核心语料；更细的 DEF 语料文件仍由测试检查。`doctor unit`、`doctor idrc` 等可只检查某一组。它不会把“目录存在”等同于差分通过。

## 自定义配置

```bash
cp src/database/eccdb/doc/test_config.example.sh /tmp/my-eccdb-tests.sh
# 编辑 /tmp/my-eccdb-tests.sh：所有数据/构建路径建议用绝对路径
"$RUNNER" --config /tmp/my-eccdb-tests.sh doctor
"$RUNNER" --config /tmp/my-eccdb-tests.sh test diff
```

优先级：显式命令行选项覆盖配置文件；配置文件中赋值覆盖父 shell 环境；未设置的变量使用脚本默认值。配置文件是会被执行的 Bash 文件，只使用可信文件。示例使用占位路径，使用前需按实际目录修改。

如果只需要环境变量，保留自己现有的编译/CTest 命令：

```bash
"$RUNNER" env                         # 查看最终配置
source <("$RUNNER" env)               # 加载到当前 Bash
source <("$RUNNER" --config /tmp/my-eccdb-tests.sh env)
```

`UV_INDEX_URL` 和 `PIP_INDEX_URL` 默认使用清华镜像，仅对脚本及其子进程生效；`source ... env` 才会进入当前终端。不写入用户级配置。

## 分组、构建与过滤

| 组 | 范围 | 构建树 |
| --- | --- | --- |
| `unit` | geometry、design/API、library、tech | standalone |
| `io` | LEF importer/exporter 与 DEF round-trip | standalone |
| `binary` | Tech/Library 和 Design binary archive | standalone |
| `diff` | 5 个 differential 二进制 | standalone |
| `irt` | `Wrap.*:SelfCheck.*:Writeback.*` | tools |
| `idrc` | `Wrap.*:Shapes.*:SelfCheck.*` | tools |
| `route-smoke` | `IspdUnder100k/*/Ispd18Sample` | tools |
| `route` | `IspdUnder100k/*` | tools |
| `all` | 上述组，但 route 只跑 smoke | 两者 |

`all` 的范围不包含完整 routing、memory comparison 或 benchmark。普通测试失败后继续其他测试组并汇总为非零退出；缺依赖或构建失败会直接停止。过滤器不会放宽组级数据检查。

```bash
"$RUNNER" configure standalone
"$RUNNER" configure tools
"$RUNNER" build diff
"$RUNNER" test diff --no-build --filter '*Complete*'
"$RUNNER" test idrc --no-build --filter 'Shapes.*:SelfCheck.*'
"$RUNNER" test all
```

默认构建目录为 `build/eccdb-differential`、`build/adapter-differential`，配置 Release、开启 EccDB tests；tools 另开启 benchmark。已有 cache 的编译器/toolchain 参数保留；新目录优先使用已安装的 GCC 13，可提前设置 `CC/CXX`。切换编译器请使用新构建目录。根工程默认把可执行文件写到仓库 `bin/`，即使改变构建目录也可能共享此位置，不要同时在不同构建树重建相同工具目标。

`--no-build` 仅用于明确知道产物与当前源码一致时，不保证自动检测源码过期；standalone 会检查已编译的 PDK 数据根是否匹配。根工程自定义二进制输出目录时配置 `ECCDB_TOOLS_BIN`。

`--filter` 使用 **GTest 模式**而不是 CTest 正则。对当前组所有二进制应用；全部未匹配会报错，不会显示通过。日志中记录实际选中的模式。

## 超大测试与已知失败

```bash
"$RUNNER" doctor --large
"$RUNNER" test diff --large
"$RUNNER" test binary --large --filter '*LargeIspd19RoutingPool*'
"$RUNNER" test route --filter 'IspdAround100k/*'
```

默认 `diff` **明确排除** `*test10_team12*`：当前 C++ 对这个 ISPD19 用例没有使用 `ECCDB_RUN_LARGE_DEF_TESTS` 来控制执行，仅不 export 变量仍会跑它。`--large` 才将它纳入。OpenROAD 的 large01/AES 和 binary stress 则由现有 C++ 开关控制，不启用时会报告 SKIP。

重要：底层测试判断环境变量是否存在，原始 shell 中 `export ECCDB_RUN_LARGE_DEF_TESTS=0` 仍会开启大测试。脚本把 `0/false/OFF` 转为 `unset`，把 `1/true/ON` 转为 `1`；`--large` 同时启用两个大测试开关。

超大 routed solution 需要组合目录：

```text
reference/ispd2019/input/ispd19_test10/ispd19_test10.input.lef
reference/ispd2019/solutions/extracted/test10/def/12.t10.def
```

扁平目录里的 `.input.def` 不能替代 `12.t10.def`。这类用例可能耗时几十分钟，不能按普通 wrapper 用例估算。

当前版本注意：

- iRT 的 wrapper 包围盒检查已移到路由后快照；wrapper 阶段保留 pin/shape 输入比较。
- `Writeback.IdbGeneratedViaPreservesOriginAndOffset` 在旧 iDB 回退后曾因 DEF writer 未输出 ORIGIN/OFFSET 而失败，`test irt` 不会隐藏它。只测 wrapper 可使用前面的过滤器。
- 完整 `route` 仍包含差分指南记录的历史失败，脚本不将它们变成 PASS 或预期失败。
- 路由测试内部使用 `fork`；在同一进程先跑 wrapper 后再路由，曾出现线程锁停滞。脚本通过 GTest 列出所选路由用例，然后 **每个用例启动一个全新进程**，不与 `Wrap.*` 混跑。

## Benchmark

完整的测试范围、参数、JSONL/CSV 字段和比较方法见 [性能测试指南](Benchmark.zh-CN.md)。

```bash
"$RUNNER" benchmark                       # sample3 冒烟
"$RUNNER" benchmark --case ispd19_test8 --case ispd19_test10 --repeat 3
"$RUNNER" benchmark --no-build --case ispd19_test10 --repeat 3
```

每个用例每轮运行 5 个独立进程：通用 benchmark 的 iDB/EccDB、iRT input 的 iDB/EccDB、二进制存档。各进程串行；偶数轮交换 iDB/EccDB 顺序。结果含原始 JSONL、存档和中位数/最小/最大值 CSV。

这里测量的是数据访问和物化，不是完整布线速度。`routing_batch_append` 只在 EccDB 路径测 256 次，不能与 iDB 做成对比较。输入 DEF 通常没有完整普通 net 路由；special route 等操作的两条路径工作量计数可能不同，需结合 benchmark README 解释。读回是写出之后的文件缓存场景，不会清理整机缓存。

## 结果与清理

每次运行创建唯一目录，例如 `/tmp/eccdb-test-results/test-diff.ABC123/`：

- `environment.sh`、`commit.txt`、`openroad-commit.txt`、`worktree.patch`：配置和代码状态；
- `commands.sh`、编号 `.log`：实际命令和输出；
- `.xml`、`summary.json`：测试通过/失败/跳过计数；
- benchmark `.jsonl`、`summary.csv`、`*-archives/`：性能原始数据与存档；
- `tmp/`：临时文件，默认 `ECCDB_KEEP_TEMP=1` 保留 iRT 定位材料。

`--output /path/to/results` 指定父目录，每次仍创建唯一子目录。脚本不自动清理源码、构建树、reference、PDK 或别人的临时文件。确认不再需要日志/存档后，可手动删除本次打印的完整结果目录。

默认 SKIP 单独显示，不算 PASS；`--strict` 让任何 SKIP 都返回非零。崩溃或断言失败会保留非零退出码，不会因为 `tee` 输出日志而被吞掉。结果文件中的字段级正确性仍取决于测试实际覆盖，不能把性能运行成功当成差分通过。
