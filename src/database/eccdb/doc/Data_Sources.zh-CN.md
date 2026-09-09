# 测试数据来源与下载布局

配合 [测试脚本](Test_Runner.zh-CN.md) 使用：`./run_tests.sh sources` 可在终端查看本文，`./run_tests.sh doctor --large` 检查落盘后的核心依赖。以下下载命令用于首次准备数据，已有数据可直接通过配置指定。示例中的 `/path/to/workspace` 需要替换为实际工作目录。

## 来源和用途

| 数据/工具 | 官方仓库或发布入口 | 推荐位置/配置 |
| --- | --- | --- |
| OpenROAD / OpenDB 源码、SI2 golden、gscl45/Nangate45 和 DEF 语料 | [The-OpenROAD-Project/OpenROAD](https://github.com/The-OpenROAD-Project/OpenROAD) | `workspace/OpenROAD`；`OPENROAD_SOURCE_DIR` |
| OpenDB Python oracle | 与上面同一 OpenROAD 源码构建；[官方构建说明](https://github.com/The-OpenROAD-Project/OpenROAD/blob/master/docs/user/Build.md) | `OpenROAD/bazel-bin/src/odb`；不是随意安装同名 PyPI 包 |
| IHP130 / SG13G2 | [IHP-GmbH/IHP-Open-PDK](https://github.com/IHP-GmbH/IHP-Open-PDK) | `workspace/scripts/foundry/ihp130/ihp-sg13g2/libs.ref` |
| Sky130 原始 PDK | [google/skywater-pdk](https://github.com/google/skywater-pdk) | 原始 PDK 的组织方式不等于测试需要的 merged LEF 集合 |
| 本测试使用的 Sky130 LEF 集合 | [ecc-tools 仓库](https://github.com/openecos-projects/ecc-tools)，包含 `scripts/foundry/sky130` 的历史版本 | `workspace/scripts/foundry/sky130/lef` |
| ISPD2018 输入 benchmark | [ISPD2018 官网](https://www.ispd.cc/contests/18/)的 Benchmarks 表 | `workspace/reference/ispd2018` |
| ISPD2019 输入 benchmark | [ISPD2019 官网](https://ispd.cc/contests/19/)的 Benchmarks 表 | `workspace/reference/ispd2019` |
| ISPD2019 最终参赛作品 | 同一官网的 Final Solution Files 表，实际文件托管于 Google Drive | `reference/ispd2019/solutions/extracted` |

比赛输入与提交结果的权威入口是官网和 Drive，不是一个统一的 GitHub 数据仓库。不要把其他 GitHub 工程中的 benchmark 路径示例当作最终参赛作品的下载来源。

## 重点：大型参赛作品 test10 / 队伍 12

当前 `test10_team12` 差分和 Design binary stress 使用：

- 发布索引：[ISPD2019 官方比赛页面](https://ispd.cc/contests/19/)，Final Solution Files → `ispd19_test10` → `test10.tgz`。
- 实际下载：[test10.tgz — Google Drive](https://drive.google.com/file/d/1DJfvEa3clBh-fQxO2vasjzk-tljPyxIK/view?usp=sharing)。
- Drive file ID：`1DJfvEa3clBh-fQxO2vasjzk-tljPyxIK`。
- 所需包内文件：`test10/def/12.t10.def`；官方页面将队伍 12 对应为 NTUidRoute（第二名）。

它是最终布线结果，和输入包 **`ispd19_test10.tgz` 是两个不同的文件**。输入包提供 LEF、原始 DEF 和 guide；solution 包提供提交的 routed DEF 与评分报告。测试还需要从输入包取得 `ispd19_test10.input.lef`。

下载后可用 `sha256sum` 记录文件指纹，随测试结果保存，以便确认后续运行使用同一份语料。

可在浏览器下载，也可使用 [gdown 官方工具](https://github.com/wkentaro/gdown)。通过 `uv tool run` 临时运行工具，包索引沿用国内镜像，不需要向项目 venv 安装：

```bash
workspace=/path/to/workspace
ispd19_root="$workspace/reference/ispd2019"
mkdir -p "$ispd19_root/solutions/extracted" "$ispd19_root/input"

UV_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple \
  uv tool run --from gdown gdown --fuzzy \
  'https://drive.google.com/file/d/1DJfvEa3clBh-fQxO2vasjzk-tljPyxIK/view?usp=sharing' \
  -O "$ispd19_root/test10.tgz"

gzip -t "$ispd19_root/test10.tgz"
sha256sum "$ispd19_root/test10.tgz"
# 仅提取差分需要的队伍 12；若需全部参赛作品，省略最后的 member 参数。
tar -xzf "$ispd19_root/test10.tgz" \
  -C "$ispd19_root/solutions/extracted" test10/def/12.t10.def
```

如果 Drive 报配额/访问错误，先通过上面的官方页面和浏览器链接确认下载；GitHub 镜像不会自动代理 Google Drive。仅 `curl -L` 后得到一个 HTML 确认页不能算下载成功，所以要用 `gzip -t` 核验。

下载并解压输入包以后，为已有扁平布局增加 `input` 视图（首次建立时执行，不使用 `-f` 覆盖已有目录）：

```bash
ln -s ../ispd19_test10 "$ispd19_root/input/ispd19_test10"
```

最终两份必须的文件：

```text
ECCDB_ISPD19_ROOT/
├── ispd19_test10/ispd19_test10.input.lef
├── input/ispd19_test10 -> ../ispd19_test10
└── solutions/extracted/test10/def/12.t10.def
```

如果已有解压目录 `reference/ispd2019/test10/`，`solutions/extracted/test10` 可以是指向该目录的链接。关键是配置根下的上述两个测试路径能够解析，无需复制数 GiB 的相同数据。

## ISPD2018/2019 输入包

官方链接示例：

- [ISPD18 sample](https://www.ispd.cc/contests/18/ispd18_sample.tgz)
- [ISPD18 test10](https://www.ispd.cc/contests/18/ispd18_test10.tgz)
- [ISPD19 sample3](https://ispd.cc/contests/19/benchmarks/ispd19_sample3.tgz)
- [ISPD19 test10 输入包](https://ispd.cc/contests/19/benchmarks/ispd19_test10.tgz)

路径规律来自官网链接：2018 在 `/contests/18/` 下，2019 在 `/contests/19/benchmarks/` 下。全量 wrapper 需要 2018 的 sample/sample2/sample3、test1–10，以及 2019 的 sample/sample2/sample3/sample4、test1–10。可同时保留原始 `.tgz` 和解压结果以便复现。

首次准备单个 ISPD19 case 示例：

```bash
workspace=/path/to/workspace
ispd19_root="$workspace/reference/ispd2019"
case_name=ispd19_test10
mkdir -p "$ispd19_root"
curl --fail --location --retry 3 \
  "https://ispd.cc/contests/19/benchmarks/$case_name.tgz" \
  --output "$ispd19_root/$case_name.tgz"
gzip -t "$ispd19_root/$case_name.tgz"
tar -tzf "$ispd19_root/$case_name.tgz"   # 查看包内目录层级
tar -xzf "$ispd19_root/$case_name.tgz" -C "$ispd19_root"
```

测试期望 `$ispd19_root/$case_name/$case_name.input.{lef,def}`；不要再套一层重复的 case 目录。`guide` 不是当前数据库导入差分的必需输入。sample 的 good/bad solution 也不是 test10 的最终参赛作品。

## OpenROAD / OpenDB

源码仓库：[OpenROAD](https://github.com/The-OpenROAD-Project/OpenROAD)。建议固定与测试兼容的 tag/commit，并在结果中记录版本，避免依赖浮动的 master。

新机器可先获取源码和子模块：

```bash
git clone --recursive https://github.com/The-OpenROAD-Project/OpenROAD.git \
  /path/to/workspace/OpenROAD
```

原始 `.lef/.def/.au` 语料来自此源码；OpenDB Python 模块需要额外构建，clone 本身不会生成 `_odb.so`。先按该 checkout 的官方构建说明准备依赖。如果所选版本的 `src/odb/BUILD` 提供 `odb_py` 和 `_odb.so` 目标，可在 OpenROAD 根目录构建：

```bash
bazel build //src/odb:odb_py
```

若使用不同版本/CMake 构建，按实际产物调整下面变量，不必强行使用 Bazel 路径：

```bash
export OPENROAD_SOURCE_DIR=/path/to/workspace/OpenROAD
export OPENDB_PYTHON=/usr/bin/python3
export OPENDB_PYTHON_MODULE_DIR="$OPENROAD_SOURCE_DIR/bazel-bin/src/odb"
export OPENDB_PYTHONPATH="$OPENDB_PYTHON_MODULE_DIR"
PYTHONPATH="$OPENDB_PYTHONPATH${PYTHONPATH:+:$PYTHONPATH}" \
  "$OPENDB_PYTHON" -c 'import odb; print(odb.__file__); print(odb.dbDatabase.create())'
```

Python ABI 必须与 `_odb.so` 匹配；本脚本的 `doctor diff` 会做同样的实际导入检查。EccDB 测试不会自动编译/更新你的 OpenROAD。

## IHP130

仓库：[IHP-GmbH/IHP-Open-PDK](https://github.com/IHP-GmbH/IHP-Open-PDK)。固定版本示例：`68eebafcd9b2f5e92c69d37a8d3d90eb266550f5`。

首次准备示例：

```bash
workspace=/path/to/workspace
mkdir -p "$workspace/scripts/foundry"
git clone https://github.com/IHP-GmbH/IHP-Open-PDK.git \
  "$workspace/scripts/foundry/ihp130"
git -C "$workspace/scripts/foundry/ihp130" checkout \
  68eebafcd9b2f5e92c69d37a8d3d90eb266550f5
```

克隆根必须是 `foundry/ihp130`，因为仓库自身已经含有 `ihp-sg13g2/`。测试使用 stdcell、IO、SRAM 的 LEF，完整文件清单来自 `tests/differential/LefPdkCorpus.h`，由 doctor 逐项检查。

## Sky130 与历史 scripts

上游原始 PDK：[google/skywater-pdk](https://github.com/google/skywater-pdk)。当前测试需要 HD/HS merged LEF、IO、SRAM 等经过整理的文件集合；仅 clone 原始 PDK 并不能保证出现 `sky130_fd_sc_hd_merged.lef` 等同名文件。

如果仓库历史包含 `scripts/foundry/sky130`，可选定包含该目录的提交，将测试数据导出到独立的数据目录：

```bash
workspace=/path/to/workspace
repo="$workspace/ecc-tools"
git -C "$repo" log --oneline --all -- scripts/foundry/sky130
revision=REVISION_WITH_SKY130_DATA # 替换为包含该目录的实际 tag/commit
git -C "$repo" cat-file -e "$revision^{commit}"
git -C "$repo" archive "$revision" scripts/foundry/sky130 \
  | tar -x -C "$workspace"
```

可用历史取决于仓库版本。若没有相应历史对象，应按 `LefPdkCorpus.h` 的完整清单准备兼容的 LEF；上游原始 PDK 地址不能替代 merged LEF 集合的准备步骤。

`ECCDB_TEST_DATA_ROOT` 仍设置为 `workspace`。外部数据可以与源码分开存放，无需将整个 `scripts`、`reference` 复制进源码目录。
