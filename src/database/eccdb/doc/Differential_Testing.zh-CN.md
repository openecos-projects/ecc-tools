# EccDB 差分测试指南

[English](Differential_Testing.en.md) | 简体中文

本文说明 EccDB 当前有哪些差分测试、各自比较什么、如何配置构建环境、需要准备哪些 LEF/DEF 语料，以及怎样判断一次测试是否完整通过。

## 1. 什么是差分测试

差分测试不是比较两个导出文件的原始文本。LEF/DEF 中对象顺序、空白、默认值和 VIA 表示方式可能不同，文本不同不一定表示数据库语义不同。

当前测试使用四类 oracle：

| Oracle | 比较方式 | 主要用途 |
| --- | --- | --- |
| OpenROAD SI2 golden | reader 输出逐字节比较 | 验证完整 LEF/DEF 5.8 语法 reader |
| legacy iDB | 两条导入路径最终都转成 EccDB 对象，再逐字段比较 | 验证直接 LEF importer 和 iDB adapter 的物化一致性 |
| OpenDB | OpenDB 读写并规范化 DEF，再由 EccDB 重读并比较语义快照 | 验证 DEF 与独立数据库实现的一致性 |
| iRT/iDRC legacy wrapper | 同一 LEF/DEF 分别经 iDB 和 EccDB adapter 进入工具 | 验证工具看到的数据库、回写结果和几何结果一致 |

比较代码会按稳定 key 排序集合，消除合法的对象迭代顺序差异。排序只存在于测试快照和 differ 中，不应放入 iRT/iDRC 生产 wrapper 来掩盖真实输入差异。

## 2. 必须运行的测试层次

| 层次 | 测试目标/源码 | 输入 | 核心检查 |
| --- | --- | --- | --- |
| 单元与 IO 基线 | `tests/unit`、`tests/io` | 合成 LEF/DEF 和 PDK LEF | Storage、不变量、LEF/DEF round-trip、binary 固定点 |
| SI2 完整语法 | `eccdb_opendb_design_differential_test` | OpenROAD `complete.5.8.lef/def` 及 `.au` | SI2 reader 输出与 OpenROAD golden 逐字节相同 |
| legacy iDB 转换 | `eccdb_lef_idb_conversion_test` | 合成语料、Sky130 | iDB parser 到 EccDB adapter 的字段物化 |
| 直接 LEF 对象差分 | `eccdb_direct_lef_differential_test` | 合成 qualifier、Sky130 | direct importer 与 iDB adapter 的 layer/via/macro/pin/port 等逐字段一致 |
| LEF 全语料语义差分 | `eccdb_lef_full_corpus_semantic_test` | Sky130、IHP130、OpenROAD gscl45 | direct/import-via-iDB 的共同字段、矩形和规范化导出的固定点 |
| DEF 语义固定点 | `eccdb_def_full_corpus_semantic_test` | OpenROAD ODB/DRT/GPL DEF | `import -> export -> import` 后对象快照和全部 net routing 语义一致 |
| OpenDB DEF 差分 | `eccdb_opendb_design_differential_test` | 合成、OpenROAD routed DEF、ISPD | EccDB 原始导入和导出结果经 OpenDB 规范化后仍保持已建模语义 |
| iRT adapter 差分 | `irt_adapter_differential_test` | ISPD2018/2019、外部 LEF/DEF | wrapper 输入、布线快照、DEF 回写和 DRC 几何集合 |
| iDRC adapter 差分 | `idrc_adapter_differential_test` | ISPD2018/2019、合成几何 | wrapper 数据、wire/via shape 和 violation 规范集合 |

内存 benchmark 和 memory comparison 不属于正确性差分。它们可以在正确性测试通过后单独运行。

DEF 结构快照当前比较 component 数量以及 global、row、track grid、gcell grid、instance、instance pin、IO pin、design VIA、NDR、regular/special net、region、group、blockage 和 fill。路由比较还会逐 net 检查 wire/path primitive、point/via/rectangle 附加信息和展开后的几何。LEF 差分按名称定位 technology layer、VIA/VIA rule、NDR、site、macro、term、port 和 obstruction，再比较已物化字段与几何。

## 3. 构建依赖

独立 EccDB 差分构建需要：

- 支持 C++20 的编译器；
- CMake 3.20 或更新版本；
- Ninja；
- Boost 1.75 或更新版本，其中使用 Boost.PFR；
- GoogleTest；
- 仓库中的 `src/third_party/lefdef` 和 `src/third_party/entt`。

OpenDB 不链接进 EccDB。它只在运行测试时作为外部 oracle，推荐使用 OpenROAD 构建出的 Python 模块：

```text
OpenROAD/bazel-bin/src/odb/
├── odb.py
└── _odb.so
```

也可以使用独立的 `odbtcl`，但 Python 路径是当前工作区已经验证的配置。

## 4. 所需数据及目录结构

### 4.1 Ecc-tools PDK LEF

`ECCDB_TEST_DATA_ROOT` 必须指向包含以下目录的 ecc-tools 源码/数据根目录：

```text
ECCDB_TEST_DATA_ROOT/
└── scripts/foundry/
    ├── sky130/lef/
    │   ├── sky130_fd_sc_hd.tlef
    │   ├── sky130_fd_sc_hd_merged.lef
    │   ├── sky130_fd_sc_hs.tlef
    │   └── ... IO/SRAM LEF
    └── ihp130/ihp-sg13g2/libs.ref/
        ├── sg13g2_stdcell/lef/
        ├── sg13g2_io/lef/
        └── sg13g2_sram/lef/
```

这些文件用于 Sky130/IHP130 的 importer、exporter、legacy iDB 和 binary 固定点测试。配置了相应测试后，文件缺失通常是失败，不是可接受的跳过。

### 4.2 OpenROAD 源码语料

`OPENROAD_SOURCE_DIR` 指向 OpenROAD 源码根目录。测试会使用：

```text
src/odb/src/lef/TEST/complete.5.8.lef[.au]
src/odb/src/def/TEST/complete.5.8.def[.au]
src/odb/test/data/gscl45nm.lef
src/odb/test/data/{design,design58,ndr,parser_test,virtual_route}.def
src/odb/test/data/gcd/*.def
src/odb/test/data/sky130hd_multi_patterned.def
src/odb/test/data/sky130hd/sky130hd_multi_patterned.tlef
src/gpl/test/nangate45.lef
src/gpl/test/large01.def
src/drt/test/aes_nangate45_preroute.def
test/Nangate45/{Nangate45_tech,Nangate45_stdcell}.lef
```

`complete.5.8` 是故意覆盖边界语法的 grammar corpus，内部包含悬空名称、非法表尺寸等内容。因此它只做 SI2 reader 与 golden 的比较，不应直接作为一致的 technology/library/design 导入。

### 4.3 ISPD2018/2019 wrapper 语料

iRT/iDRC wrapper 使用扁平的解压目录：

```text
ECCDB_ISPD18_ROOT/
├── ispd18_sample/ispd18_sample.input.{lef,def}
├── ispd18_sample2/...
├── ispd18_sample3/...
└── ispd18_test1 ... ispd18_test10/

ECCDB_ISPD19_ROOT/
├── ispd19_sample ... ispd19_sample4/
└── ispd19_test1 ... ispd19_test10/
```

每个 case 至少需要同名的 `.input.lef` 和 `.input.def`。

### 4.4 ISPD2019 超大 routed solution

OpenDB 最大语料和 Design binary stress test 使用另一种官方包布局：

```text
ECCDB_ISPD19_ROOT/
├── input/ispd19_test10/ispd19_test10.input.lef
└── solutions/extracted/test10/def/12.t10.def
```

这和 wrapper 的扁平目录不是同一布局。可以建立一个同时包含两种子目录的组合根目录，或者在运行不同测试组前分别设置 `ECCDB_ISPD19_ROOT`。仅有 `ispd19_test10.input.def` 不能替代 `12.t10.def`。

## 5. 配置变量

| 变量 | 设置阶段 | 作用 |
| --- | --- | --- |
| `ECCDB_ECC_TOOLS_ROOT` | CMake | EccDB 所在 ecc-tools 根目录；提供 third_party 和 legacy iDB 源码 |
| `ECCDB_TEST_DATA_ROOT` | CMake | 提供 `scripts/foundry` 测试数据，可与源码根目录不同 |
| `OPENROAD_SOURCE_DIR` | CMake 或运行时 | OpenROAD 语料根目录；运行时变量覆盖编译默认值 |
| `OPENDB_PYTHON_MODULE_DIR` | CMake | 包含 `odb.py`、`_odb.so` 的目录 |
| `OPENDB_PYTHON` | 运行时 | Python 解释器；覆盖 CMake 检测结果 |
| `OPENDB_PYTHONPATH` | 运行时 | OpenDB Python 模块目录；覆盖 CMake 默认值 |
| `OPENDB_TCL` | 运行时 | 可选 `odbtcl` 路径；没有 Python oracle 时使用 |
| `ECCDB_ISPD18_ROOT` | 运行时 | ISPD2018 解压根目录 |
| `ECCDB_ISPD19_ROOT` | 运行时 | ISPD2019 解压/组合根目录 |
| `ECCDB_RUN_LARGE_DEF_TESTS=1` | 运行时 | 启用 `large01`、AES preroute 和超大 OpenDB DEF |
| `ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS=1` | 运行时 | 启用 ISPD2019 test10 Design binary 固定点 |
| `ECCDB_NET_COMPARE_THREADS` | 运行时 | 大型 net 语义比较线程数，建议 `128` |
| `ECCDB_RT_THREAD_NUMBER` | 运行时 | iRT 内部布线线程数，当前测试要求设为 `128` |
| `ECCDB_KEEP_TEMP=1` | 运行时 | iRT 失败时保留临时 DEF、snapshot 和日志 |
| `ECCDB_IRT_WRAP_LEF/DEF` | 运行时 | 给 `Wrap.ExternalLefDef` 指定一个自定义 case |

## 6. 独立 EccDB 差分构建

以下示例适用于当前工作区：

```bash
export ECCDB_SRC=/home/zhuxu1/workspace/ecc-tools-eccdbv1
export ECCDB_DATA=/home/zhuxu1/workspace/ecc-tools
export OPENROAD_ROOT=/home/zhuxu1/workspace/OpenROAD
export ECCDB_DIFF_BUILD="$ECCDB_SRC/build/eccdb-differential"

cmake -S "$ECCDB_SRC/src/database/eccdb" -B "$ECCDB_DIFF_BUILD" -G Ninja \
  -DECCDB_BUILD_TESTS=ON \
  -DECCDB_STANDALONE_LEGACY_IDB=ON \
  -DECCDB_TEST_DATA_ROOT="$ECCDB_DATA" \
  -DOPENROAD_SOURCE_DIR="$OPENROAD_ROOT" \
  -DOPENDB_PYTHON_MODULE_DIR="$OPENROAD_ROOT/bazel-bin/src/odb"

cmake --build "$ECCDB_DIFF_BUILD" \
  --target eccdb_differential_tests --parallel 128
```

`ECCDB_STANDALONE_LEGACY_IDB=ON` 会自动启用 `ECCDB_STANDALONE_LEF_DEF`。它只构建 legacy LEF reader 所需的最小 iDB 子集，不会拉入完整 DEF/Verilog/GDS/iRT/iDRC 工程。

如需按推荐顺序先跑 IO 和 binary 基线，还要显式构建这些目标：

```bash
cmake --build "$ECCDB_DIFF_BUILD" --parallel 128 --target \
  eccdb_lef_tech_importer_test \
  eccdb_lef_library_importer_test \
  eccdb_lef_tech_exporter_test \
  eccdb_lef_library_exporter_test \
  eccdb_def_design_roundtrip_test \
  eccdb_binary_database_archive_test \
  eccdb_design_binary_database_archive_test
```

运行前设置运行时数据：

```bash
export OPENROAD_SOURCE_DIR="$OPENROAD_ROOT"
export OPENDB_PYTHON="$(command -v python3)"
export OPENDB_PYTHONPATH="$OPENROAD_ROOT/bazel-bin/src/odb"
export ECCDB_ISPD18_ROOT="$ECCDB_DATA/reference/ispd2018"
export ECCDB_ISPD19_ROOT="$ECCDB_DATA/reference/ispd2019"
export ECCDB_NET_COMPARE_THREADS=128
```

先查看实际注册的测试：

```bash
ctest --test-dir "$ECCDB_DIFF_BUILD" -N -L eccdb_differential
```

运行默认差分组：

```bash
ctest --test-dir "$ECCDB_DIFF_BUILD" \
  --output-on-failure --parallel 128 --label-regex eccdb_differential
```

默认不启用超大 DEF。确认完整数据和内存后再运行：

```bash
export ECCDB_RUN_LARGE_DEF_TESTS=1
ctest --test-dir "$ECCDB_DIFF_BUILD" \
  --output-on-failure --parallel 128 --label-regex eccdb_differential
```

## 7. 分组和单例运行

直接执行测试二进制最容易精确过滤：

```bash
"$ECCDB_DIFF_BUILD/tests/differential/eccdb_direct_lef_differential_test" \
  --gtest_filter='DirectLefDifferentialTest.*'

"$ECCDB_DIFF_BUILD/tests/differential/eccdb_lef_full_corpus_semantic_test" \
  --gtest_filter='LefFullCorpusSemanticDifferentialTest.*'

"$ECCDB_DIFF_BUILD/tests/differential/eccdb_def_full_corpus_semantic_test" \
  --gtest_filter='OpenRoadOdbData/*'

"$ECCDB_DIFF_BUILD/tests/differential/eccdb_opendb_design_differential_test" \
  --gtest_filter='OpenRoadOdbData/*'
```

OpenROAD routed DEF 的快速组包括：

- `virtual_route`
- `ndr`
- `sky130hd_multi_patterned`
- `gcd_floorplan`
- `gcd_placed`
- `gcd_pdn`
- `gcd_nangate45_route`
- `gcd_route_via_only_layer`
- `gcd_route_with_power_pins`

`large01` 和 `aes_nangate45_preroute` 受 `ECCDB_RUN_LARGE_DEF_TESTS` 控制。

## 8. iRT/iDRC wrapper 差分

Wrapper 测试依赖完整 ecc-tools 根工程，不能由 EccDB 独立构建产生。使用已完成依赖配置的根工程构建目录：

```bash
export ECCDB_SRC=/home/zhuxu1/workspace/ecc-tools-eccdbv1
export ECC_TOOLS_BUILD="$ECCDB_SRC/build/ecc-tools"

cmake -S "$ECCDB_SRC" -B "$ECC_TOOLS_BUILD" -G Ninja \
  -DECCDB_BUILD_TESTS=ON
cmake --build "$ECC_TOOLS_BUILD" \
  --target irt_adapter_differential_test idrc_adapter_differential_test \
  --parallel 128
```

如果根工程使用项目自己的 toolchain、包管理或额外配置参数，应保留那些参数；这里仅展示与 EccDB 测试相关的开关。

测试二进制通常生成在仓库的 `bin/`。先列出测试，再运行：

```bash
export ECCDB_ISPD18_ROOT=/home/zhuxu1/workspace/ecc-tools/reference/ispd2018
export ECCDB_ISPD19_ROOT=/home/zhuxu1/workspace/ecc-tools/reference/ispd2019
export ECCDB_RT_THREAD_NUMBER=128

"$ECCDB_SRC/bin/irt_adapter_differential_test" --gtest_list_tests
"$ECCDB_SRC/bin/idrc_adapter_differential_test" --gtest_list_tests

"$ECCDB_SRC/bin/irt_adapter_differential_test" \
  --gtest_filter='Wrap.*'
"$ECCDB_SRC/bin/idrc_adapter_differential_test" \
  --gtest_filter='Wrap.*:Shapes.*:SelfCheck.*'
```

需要实际调用 iRT 布线和回写时，只运行已登记的 10 万门以下组：

```bash
ECCDB_KEEP_TEMP=1 ECCDB_RT_THREAD_NUMBER=128 \
  "$ECCDB_SRC/bin/irt_adapter_differential_test" \
  --gtest_filter='IspdUnder100k/*'
```

`IspdAround100k/*` 当前只包含约 10.8 万实例的 ISPD18 test6，应作为额外压力测试，不属于“10 万门以下”默认组。

iRT/iDRC 测试在 CTest 中标记了 `RUN_SERIAL TRUE`。原因是工具内部存在全局接口和共享状态。可以给 iRT、iDRC 分别开 tmux session，但不要在同一个测试二进制中并发启动多个 case。`ECCDB_RT_THREAD_NUMBER=128` 指的是单个 iRT case 内部的布线并行度，不是同时运行 128 个 case。

### 8.1 已知的 ISPD iRT 失败

截至 2026-08-27 的 128 线程受控诊断，确认有 **4 个 ISPD case 在 iRT 布线阶段本身失败**：

| Case | 实例数 | legacy iDB | EccDB/EnTT | 失败位置 |
| --- | ---: | --- | --- | --- |
| `Ispd18Test1` | 8,879 | FAIL | FAIL | `PinAccessor::updateAccessPoint` 第二轮 |
| `Ispd18Test2` | 35,913 | FAIL | FAIL | `PinAccessor::updateAccessPoint` 第二轮 |
| `Ispd18Test3` | 35,977 | FAIL | FAIL | `PinAccessor::updateAccessPoint` 第二轮 |
| `Ispd18Test4` | 72,094 | FAIL | FAIL | `PinAccessor::updateAccessPoint` 第二轮 |

这 4 个 case 即使完全不创建 EccDB、只使用原始 iDB wrapper 也会失败；两条路径在失败前的 box/violation 进度一致。因此当前证据将它们归类为 iRT 公共路由算法/并发路径问题，而不是 EccDB parser、Storage 或 adapter 差分。失败发生在 routed DEF 和 snapshot 生成之前，不能描述为“布线结果不一致”。

这些 case 目前没有标记成 GTest `DISABLED_` 或预期失败，运行整个 `IspdUnder100k/*` 时仍会报告失败。这是有意保留的可见问题，不能把它们计入 PASS 或 SKIP。后续 iRT 修复后应重新运行两条路径并删除本节的已知失败记录。

另外需要区分：`Ispd19Sample4` 的两条路由 worker 都能完成，routed snapshot 也一致；目前观察到的问题发生在回写后的最终 DRC 几何集合比较。因此它是“布线后差分失败”，不是 iRT 无法完成布线。`Ispd19Sample3` 已完成 route、EccDB writeback、DEF round-trip 和对象差分，可作为当前 routed smoke case：

```bash
ECCDB_RT_THREAD_NUMBER=128 \
  "$ECCDB_SRC/bin/irt_adapter_differential_test" \
  --gtest_filter='*Ispd19Sample3'
```

## 9. Binary 固定点测试

Binary 测试不是独立 oracle，但必须用于防止 pool、ID generation 和索引在持久化后发生变化：

```bash
cmake --build "$ECCDB_DIFF_BUILD" --parallel 128 --target \
  eccdb_binary_database_archive_test \
  eccdb_design_binary_database_archive_test
ctest --test-dir "$ECCDB_DIFF_BUILD" --output-on-failure \
  -R 'BinaryDatabaseArchiveTest|DesignBinaryDatabaseArchiveTest'
```

超大 ISPD2019 routed solution 需要第 4.4 节的完整目录：

```bash
export ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS=1
export ECCDB_ISPD19_ROOT=/path/to/combined-ispd2019-root
ctest --test-dir "$ECCDB_DIFF_BUILD" --output-on-failure \
  -R 'LargeIspd19RoutingPoolReachesByteExactFixedPoint'
```

该用例会导入约 89.5 万 wires、数千万 path/point records，内存和时间消耗远高于普通差分测试。

## 10. 推荐执行顺序

1. 先运行 unit、LEF/DEF IO round-trip 和 binary 小测试，排除本地 Storage/序列化问题。
2. 运行 SI2 complete syntax、legacy iDB conversion 和 direct LEF object differential。
3. 运行 Sky130/IHP130 LEF full corpus。
4. 运行 OpenROAD 小型及 routed DEF semantic/OpenDB differential。
5. 运行 iRT/iDRC `Wrap.*`，确认 adapter 输入一致。
6. 先运行已通过的 iRT `Ispd19Sample3` routed smoke case，再运行 `IspdUnder100k/*` 做完整诊断；后者当前包含第 8.1 节的已知失败。
7. 最后按需启用 large DEF、约 10.8 万门 routing 和 ISPD2019 test10 binary stress。

## 11. 通过标准

一次可声称“默认差分通过”的结果必须满足：

- 所有已运行测试为 `PASS`，没有 crash、timeout 或 assertion failure；
- `SKIP` 都能对应到明确未启用的大测试或明确缺失的可选外部语料；
- OpenDB runtime 已配置时，OpenDB 测试不能因 oracle 缺失而跳过；
- 声称“ISPD2018/2019 全量”时，相应 root、所有 case 和所需 solution 必须存在，不能把 skip 计为通过；
- iRT routed differential 必须同时满足 wrapper snapshot、回写 DEF 语义和最终 DRC 几何集合；只满足前两项不能算完整通过；
- 第 8.1 节列出的 iRT 失败必须单独报告，不能因为 legacy iDB 与 EccDB 同样失败就把差分测试记为 PASS；
- 报告中记录代码 commit、OpenROAD commit、环境变量、测试总数、通过数、跳过数和失败数。

当前差分只保证 snapshot/assertion 已覆盖的字段。增加新的 EccDB 字段时，应同步扩展 snapshot/differ；测试通过不能证明尚未纳入比较的字段正确。

## 12. 失败定位

- 显示 `configure the OpenDB Python module or set OPENDB_TCL`：检查 `odb.py`、`_odb.so`、Python ABI 和 `OPENDB_PYTHONPATH`。
- OpenDB 子进程返回 `127`：通常表示配置的解释器或 `odbtcl` 无法执行，而不是语义差分。
- 显示 `set OPENROAD_SOURCE_DIR`：运行时变量和 CMake 默认值都没有指向有效 OpenROAD checkout。
- ISPD root 存在但断言文件不存在：检查第 4.3/4.4 节的两种目录布局；这种情况会失败而不是自动跳过。
- iRT snapshot 一致但 DRC shapes 不一致：保留临时输出，逐个 net 对照两份 routed DEF 中的 VIA 定义、路径展开、orientation/offset 和 special-net 几何；不能只看 wire 数量。
- 大测试被 OOM：先单独运行一个 case，避免 wrapper case 并发；保留 128 个 iRT 内部线程不代表必须并发运行测试进程。

## 13. 增加新语料时的要求

新增 case 应同时记录：

- 语料来源、版本和许可证；
- LEF、DEF 及附加 library LEF 的相对路径；
- 是否 routed、规模和最低 wire/path/via 数；
- 使用哪个 oracle，以及比较哪些语义字段；
- 是否允许已知且有理由的 normalization；
- 缺少语料时应该 skip 还是 fail；
- 一个能稳定复现问题的小型 regression case。

优先把新语法压缩成小型合成测试，再把真实语料加入 corpus。小测试负责准确定位字段，全语料负责发现组合语法、规模和跨对象引用问题。
