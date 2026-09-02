# ecc-tools CI 说明

CI 位于 [.github/workflows/ci.yml](../../.github/workflows/ci.yml),包含两个 job:

```
build-wheel ──► test-python-api
(构建 manylinux wheel)   (安装 wheel,跑 Python API 集成测试)
```

## 触发条件与合并门禁

| 事件 | 触发范围 |
| --- | --- |
| `pull_request` | **所有 PR,无任何过滤**(见下文说明) |
| `push` | 仅 `main` 分支 |
| `workflow_dispatch` | 手动触发 |

`pull_request` 刻意**不配置 `paths` 过滤**:本 CI 的两个 job 是 main 分支
branch protection 的 required status checks,如果加了 `paths` 过滤,不匹配
路径的 PR(如只改 docs)永远不会产生这两个 check,required check 会一直处于
"Expected" 状态,导致 PR 无法合并。仓库为 public 仓库,Actions 分钟数不受限,
因此选择让每个 PR 无条件全量执行。

**配置合并门禁(一次性,网页操作)**:

Settings → Branches → Add branch protection rule `main` → 勾选
*Require status checks to pass before merging* → 选中以下 check(名称必须与
job 的 `name` 字段完全一致):

- `Build Wheel`
- `Test Python API`

## build-wheel

定义在 ci.yml 中,复用组合 action
[.github/actions/build-wheel/action.yml](../../.github/actions/build-wheel/action.yml)。

- 运行环境:`quay.io/pypa/manylinux_2_34_x86_64` 容器,ubuntu-latest runner。
- 步骤:
  1. `dnf` 安装构建依赖(boost/cairo/eigen/tbb/metis/tcl 等 + cmake/ninja/mold)。
  2. 使用 manylinux 自带 CPython 3.11(`/opt/python/cp311-cp311`)。
  3. sccache(编译缓存)+ uv 缓存 + `build/` 目录缓存(按 CMakeLists 哈希)。
  4. `uv build --wheel` + `auditwheel repair`,产出修复后的 wheel 到
     `dist/wheel/repaired/`。
- 产出:artifact `ecc-tools-wheel`,供下游 job 下载。

## test-python-api

`needs: build-wheel`,同样跑在 manylinux_2_34 容器中,`timeout-minutes: 45`。

步骤概要:

1. `dnf install git make curl tar bzip2 unzip`(PDK 克隆与解压所需)。
2. 下载 `ecc-tools-wheel` artifact。
3. `uv venv .venv-test` + 安装 wheel 与 pytest(wheel 声明了
   `matplotlib>=3.4`、`numpy` 依赖,会自动带入),并在 `test/` 目录下做
   `from ecc_tools_bin import ecc_py` 导入冒烟。
4. 浅克隆 [icsprout55-pdk](https://github.com/openecos-projects/icsprout55-pdk)
   并 `make unzip` 解出完整 PDK。
5. 运行测试(工作目录 `test/`):

   ```bash
   python -m pytest data operations -v --maxfail=1
   ```

6. 失败时上传 `test-artifacts/`(各场景的日志、manifest 与输出)。

环境变量:

| 变量 | 作用 |
| --- | --- |
| `ECC_TOOLS_TEST_REPO_ROOT` | ecc-tools checkout 根,fixture 取自 `test/fixtures/gcd/` |
| `ECC_TOOLS_TEST_PDK_ROOT` | icsprout55-pdk 根(需已 `make unzip`) |
| `ECC_TOOLS_TEST_ARTIFACT_ROOT` | 各场景工作目录的父目录,失败时整体上传 |
| `ECC_LOGGER_THROW_ON_ERROR=1` | 原生层报错时抛异常而非仅打日志 |

## 测试架构

三层结构,原生崩溃被隔离在子进程中,不影响 pytest 进程:

```
test/data/test_python_io.py         pytest 入口(3 个 IO 测试)
test/operations/test_python_operations.py   pytest 入口(8 个操作测试)
        │
        ▼
test/support.py                     run_scenario:组 manifest、起子进程、收集结果
        │
        ▼
test/native_scenarios.py            子进程:按场景名调用 ecc_py 原生 API
```

- [conftest.py](../../test/conftest.py):session 级 fixture,从环境变量解析
  `TestRoots`。
- [support.py](../../test/support.py):
  - 每个场景在 `ECC_TOOLS_TEST_ARTIFACT_ROOT` 下建独立临时目录,写入
    `manifest.json`,以 `sys.executable` 子进程执行 `native_scenarios.py`,
    超时(IO 类 120s、操作类 480s)后杀进程并保留日志。
  - gzip fixture(`test/fixtures/gcd/input/*.gz`)解压到场景目录。
  - 渲染配置(`_render_config`)时按以下规则改写 fixture 配置,保证与
    环境解耦:
    | 原值 | 改写为 |
    | --- | --- |
    | `thread_num` / `thread_number` / `-thread_number` | 统一改为 2(保持原类型) |
    | `./icsprout55-pdk/...` | `$ECC_TOOLS_TEST_PDK_ROOT/...` |
    | `./output` | 场景输出目录 |
    | `./fixtures/gcd/config/gcd.sdc` | fixture 绝对路径 |
    | `macro_locations.txt` | 场景工作目录下生成 |
  - 子进程环境 `setdefault`: `ECC_LOGGER_THROW_ON_ERROR=1`、`OMP_NUM_THREADS=2`。
- [native_scenarios.py](../../test/native_scenarios.py):每个场景先
  `db_init → tech_lef_init → lef_init`,再按需 `def_init`/`verilog_init`,
  然后执行场景逻辑;所有返回 bool 的 API 调用结果都会校验。

### 场景清单

| 场景 | 输入 | 主要 API | 断言要点 |
| --- | --- | --- | --- |
| `def_round_trip` | route_in.def | `def_save` | 输出含 `DESIGN gcd`,且 `def_verify` 能重读 |
| `def_verify` | 上一步输出的 def | `def_init` | 写出的 DEF 可重新解析 |
| `verilog_round_trip` | floorplan_in.v | `netlist_save` | 输出含 `module gcd`,且 `verilog_verify` 能重读 |
| `verilog_verify` | 上一步输出的 v | `verilog_init` | 写出的网表可重新解析 |
| `combined_io` | route_in.def + v | `def_save`/`netlist_save`/`gds_save` | 三种格式导出均非空 |
| `floorplan` | floorplan_in.v | `init_fp`/`run_fp` | DEF 含 `DIEAREA` |
| `cts` | cts_in.def + v(+lib/sdc) | `run_cts`/`cts_report` | DEF 非空、报告目录有产物 |
| `routing` | route_in.def | `init_rt`/`run_rt` | DEF 含 `NETS` |
| `drc` | route_in.def + v | `init_drc`/`run_drc`/`save_drc` | drc.bin、violation_map.json(JSON list)、DEF |
| `rcx` | route_in.def + v | `init_rcx(pdk="ics55")`/`run_rcx` | DEF 非空、SPEF 含 `*SPEF` |
| `sta` | route_in.def + v(+typ lib/sdc/spef) | `init_sta`/`run_sta` | `timing_reporter/*.rpt` 非空 |
| `lvs` | harden_in.def + v(`lvs_verilog_init`) | `init_lvs`/`run_lvs` | ilvs.rpt、ilvs.json、DEF |
| `harden` | harden_in.def + v | `write_abstract_lef`/`extract_lib`/`gds_save` | gcd.lef(含 `MACRO gcd`)、gcd.lib、gcd.gds |

## 本地复现

```bash
cd <ecc-tools checkout>
# 需要先拿到修复后的 wheel(本地构建或从 CI artifact 下载)
uv venv .venv-test
uv pip install --python .venv-test/bin/python pytest dist/wheel/repaired/*.whl

export ECC_TOOLS_TEST_REPO_ROOT="$PWD"
export ECC_TOOLS_TEST_PDK_ROOT="/path/to/icsprout55-pdk"   # 需已 make unzip
export ECC_TOOLS_TEST_ARTIFACT_ROOT="$(mktemp -d)"
export ECC_LOGGER_THROW_ON_ERROR=1

cd test   # 必须在 test/ 目录下运行,原因见"已知限制"第 3 条
../.venv-test/bin/python -m pytest data operations -v --maxfail=1
```

## 已知限制

1. **不含 C++ 单测**:`build-wheel` 到 `auditwheel repair` 为止,仓库内的
   C++ 测试(如 iCTS 单测)未在 CI 中执行,也没有 lint / 静态检查。
2. **fork 或首次贡献者的 PR** 会停在 `action_required`,需维护者批准后
   才开始跑(GitHub 防滥用机制,无法用配置绕过)。
3. **必须在 `test/` 目录下运行 python/pytest**:仓库根存在被 git 跟踪的
   `ecc_tools_bin/__init__.py` 源码目录,在仓库根执行
   `from ecc_tools_bin import ecc_py` 会因遮蔽已安装 wheel 而失败。
   CI 中的 import 冒烟与 pytest 步骤均显式 `cd test` / `working-directory: test`。
4. 失败产物 `test-artifacts/` 仅在失败时上传(含每个场景的 `native.log`、
   `manifest.json` 与输出文件)。
5. job 级 `timeout-minutes: 45` 是兜底;场景级超时更短,正常情况下整体
   测试耗时约 1 分钟。
