#!/usr/bin/env bash
# SPDX-License-Identifier: MulanPSL-2.0
# See Test_Runner.zh-CN.md. Requires Bash 4+, Python 3, CMake and Ninja.
set -euo pipefail
if [[ ${BASH_SOURCE[0]} != "$0" ]]; then
  echo '请执行此脚本；加载环境请用 source <(bash run_tests.sh env)。' >&2
  return 2
fi
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
default_repo=$(cd -- "$script_dir/../../../.." && pwd -P)
die() { echo "错误：$*" >&2; exit 2; }
usage() {
  cat <<'EOF'
用法：run_tests.sh [--config FILE] COMMAND [GROUP] [OPTIONS]
  doctor [GROUP]       检查工具、数据目录及 OpenDB Python 的真实导入
  env                 输出可 source 的环境变量
  sources             查看数据源 GitHub/官网/Google Drive 与下载说明
  configure standalone|tools
  build GROUP         配置并构建对应目标
  test GROUP          检查依赖、配置、构建、运行，输出日志和 JUnit
  benchmark           分进程串行运行 iDB/EccDB/二进制性能测量

GROUP：unit | io | binary | diff | irt | idrc | route-smoke | route | all
  irt：Wrap.* + SelfCheck.* + Writeback.*（已有失败也会如实报告）
  route-smoke：ISPD18 sample；route：IspdUnder100k/*，逐 case 新进程
  all：unit io binary diff irt idrc route-smoke，失败后继续其他组并汇总

选项：
  --config FILE       Bash 配置文件（仅加载你信任的文件，建议放仓库外）
  --no-build          复用现有构建；配置/源码变化后请重新构建
  --filter PATTERN    GTest 过滤器；route 下仍逐 case 启动
  --large             启用 large DEF 和 Design binary；diff 包含 test10_team12
  --strict            任何 SKIP 也返回非零
  --jobs N            构建并行度，默认 nproc；测试进程仍串行
  --output DIR        本次输出的父目录；每次创建唯一子目录
  --case NAME         benchmark 用例，可重复，默认 ispd19_sample3
  --repeat N          benchmark 重复次数，默认 1
  -h, --help

示例：
  ./run_tests.sh doctor
  ./run_tests.sh test diff
  ./run_tests.sh test irt --filter 'Wrap.*:SelfCheck.*'
  ./run_tests.sh test route --filter 'IspdUnder100k/*/Ispd19Sample3'
  ./run_tests.sh benchmark --case ispd19_test8 --case ispd19_test10 --repeat 3
EOF
}
# Source config before defaults; command-line options are applied afterwards.
args=("$@")
for ((i=0; i<${#args[@]}; i++)); do
  if [[ ${args[i]} == --config ]]; then
    ((i+1<${#args[@]})) || die '--config 缺少文件'
    config=${args[i+1]}; [[ -f $config ]] || die "配置不存在：$config"
    # shellcheck disable=SC1090
    source "$config"
    i=$((i+1))
  fi
done
: "${ECCDB_SRC:=$default_repo}"
ECCDB_SRC=$(cd -- "$ECCDB_SRC" && pwd -P)
: "${ECCDB_WORKSPACE:=$(dirname -- "$ECCDB_SRC")}"
: "${ECCDB_TEST_DATA_ROOT:=$ECCDB_WORKSPACE}"
: "${ECCDB_ISPD18_ROOT:=$ECCDB_WORKSPACE/reference/ispd2018}"
: "${ECCDB_ISPD19_ROOT:=$ECCDB_WORKSPACE/reference/ispd2019}"
: "${OPENROAD_SOURCE_DIR:=$ECCDB_WORKSPACE/OpenROAD}"
: "${OPENDB_PYTHON:=python3}"
: "${OPENDB_PYTHON_MODULE_DIR:=${OPENDB_PYTHONPATH:-$OPENROAD_SOURCE_DIR/bazel-bin/src/odb}}"
: "${OPENDB_PYTHONPATH:=$OPENDB_PYTHON_MODULE_DIR}"
: "${ECCDB_DIFF_BUILD:=$ECCDB_SRC/build/eccdb-differential}"
: "${ECCDB_TOOLS_BUILD:=$ECCDB_SRC/build/adapter-differential}"
: "${ECCDB_TOOLS_BIN:=$ECCDB_SRC/bin}"
: "${ECCDB_BUILD_JOBS:=$(nproc)}"
: "${ECCDB_NET_COMPARE_THREADS:=128}"
: "${ECCDB_RT_THREAD_NUMBER:=128}"
: "${OMP_NUM_THREADS:=$(nproc)}"
: "${ECCDB_KEEP_TEMP:=1}"
: "${ECCDB_RESULTS_ROOT:=/tmp/eccdb-test-results}"
: "${ECCDB_IRT_WRAP_LEF:=$ECCDB_ISPD19_ROOT/ispd19_sample3/ispd19_sample3.input.lef}"
: "${ECCDB_IRT_WRAP_DEF:=$ECCDB_ISPD19_ROOT/ispd19_sample3/ispd19_sample3.input.def}"
: "${UV_INDEX_URL:=https://pypi.tuna.tsinghua.edu.cn/simple}"
: "${PIP_INDEX_URL:=$UV_INDEX_URL}"
# The C++ tests use getenv()!=nullptr: exporting 0 would ENABLE stress tests.
for flag in ECCDB_RUN_LARGE_DEF_TESTS ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS; do
  case ${!flag:-} in ''|0|false|OFF) unset "$flag";; 1|true|ON) export "$flag=1";; *) die "$flag 必须为 0 或 1";; esac
done
command_name= group= filter= no_build=0 strict=0 repeats=1
cases=()
while (($#)); do
  case $1 in
    -h|--help) usage; exit 0;;
    --config) (($#>=2)) || die '--config 缺少值'; shift 2;;
    --no-build) no_build=1; shift;;
    --strict) strict=1; shift;;
    --large) export ECCDB_RUN_LARGE_DEF_TESTS=1 ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS=1; shift;;
    --filter|--jobs|--output|--case|--repeat)
      (($#>=2)) || die "$1 缺少值"
      case $1 in
        --filter) filter=$2;; --jobs) ECCDB_BUILD_JOBS=$2;; --output) ECCDB_RESULTS_ROOT=$2;;
        --case) cases+=("$2");; --repeat) repeats=$2;;
      esac
      shift 2;;
    -*) die "未知选项：$1";;
    *) if [[ -z $command_name ]]; then command_name=$1; elif [[ -z $group ]]; then group=$1; else die "多余参数：$1"; fi; shift;;
  esac
done
[[ -n $command_name ]] || { usage; exit 0; }
for number in ECCDB_BUILD_JOBS ECCDB_NET_COMPARE_THREADS ECCDB_RT_THREAD_NUMBER OMP_NUM_THREADS; do
  [[ ${!number} =~ ^[1-9][0-9]*$ ]] || die "$number 必须是正整数"
done
[[ $repeats =~ ^[1-9][0-9]*$ ]] || die '--repeat 必须是正整数'
((${#cases[@]})) || cases=(ispd19_sample3)
for name in "${cases[@]}"; do [[ $name =~ ^ispd19_(sample|test)[0-9]*$ ]] || die "无效 benchmark case：$name"; done
for path_var in ECCDB_WORKSPACE ECCDB_TEST_DATA_ROOT ECCDB_ISPD18_ROOT ECCDB_ISPD19_ROOT \
  OPENROAD_SOURCE_DIR OPENDB_PYTHON_MODULE_DIR OPENDB_PYTHONPATH ECCDB_DIFF_BUILD ECCDB_TOOLS_BUILD ECCDB_TOOLS_BIN \
  ECCDB_RESULTS_ROOT ECCDB_IRT_WRAP_LEF ECCDB_IRT_WRAP_DEF; do
  printf -v "$path_var" '%s' "$(realpath -ms -- "${!path_var}")"
done
if [[ $OPENDB_PYTHON != */* ]] && command -v "$OPENDB_PYTHON" >/dev/null; then
  OPENDB_PYTHON=$(command -v "$OPENDB_PYTHON")
fi
if [[ $OPENDB_PYTHON == */* ]]; then OPENDB_PYTHON=$(realpath -ms -- "$OPENDB_PYTHON"); fi
export ECCDB_SRC ECCDB_WORKSPACE ECCDB_TEST_DATA_ROOT ECCDB_ISPD18_ROOT ECCDB_ISPD19_ROOT
export OPENROAD_SOURCE_DIR OPENDB_PYTHON OPENDB_PYTHONPATH OPENDB_PYTHON_MODULE_DIR
export ECCDB_DIFF_BUILD ECCDB_TOOLS_BUILD ECCDB_TOOLS_BIN ECCDB_BUILD_JOBS
export ECCDB_NET_COMPARE_THREADS ECCDB_RT_THREAD_NUMBER OMP_NUM_THREADS ECCDB_KEEP_TEMP
export ECCDB_IRT_WRAP_LEF ECCDB_IRT_WRAP_DEF ECCDB_RESULTS_ROOT UV_INDEX_URL PIP_INDEX_URL
print_env() {
  local name
  for name in ECCDB_SRC ECCDB_WORKSPACE ECCDB_TEST_DATA_ROOT ECCDB_ISPD18_ROOT ECCDB_ISPD19_ROOT \
    OPENROAD_SOURCE_DIR OPENDB_PYTHON OPENDB_PYTHONPATH OPENDB_PYTHON_MODULE_DIR ECCDB_DIFF_BUILD \
    ECCDB_TOOLS_BUILD ECCDB_TOOLS_BIN ECCDB_BUILD_JOBS ECCDB_NET_COMPARE_THREADS ECCDB_RT_THREAD_NUMBER \
    OMP_NUM_THREADS ECCDB_KEEP_TEMP ECCDB_IRT_WRAP_LEF ECCDB_IRT_WRAP_DEF ECCDB_RESULTS_ROOT UV_INDEX_URL PIP_INDEX_URL; do
    printf 'export %s=%q\n' "$name" "${!name}"
  done
  for name in ECCDB_RUN_LARGE_DEF_TESTS ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS; do
    if [[ -v $name ]]; then printf 'export %s=1\n' "$name"; else printf 'unset %s\n' "$name"; fi
  done
}
check_data() {
  python3 - "$1" "${cases[@]}" <<'PY'
import os,sys,re,subprocess,shutil
from pathlib import Path
group=sys.argv[1];e=os.environ; errors=[]
def need(path):
    p=Path(path)
    if not p.is_file() or p.stat().st_size==0: errors.append(str(p))
for tool in ['cmake','ninja','python3']:
    if not shutil.which(tool): errors.append('command: '+tool)
repo=Path(e['ECCDB_SRC']);data=Path(e['ECCDB_TEST_DATA_ROOT']);road=Path(e['OPENROAD_SOURCE_DIR'])
need(repo/'src/third_party/entt/entt/entt.hpp')
if group in ['io','binary','diff','all']:
    header=(repo/'src/database/eccdb/tests/differential/LefPdkCorpus.h').read_text()
    bases={'lef_root':data/'scripts/foundry/sky130/lef'}
    ihp=data/'scripts/foundry/ihp130/ihp-sg13g2/libs.ref'
    bases.update(stdcell=ihp/'sg13g2_stdcell/lef',io=ihp/'sg13g2_io/lef',sram=ihp/'sg13g2_sram/lef')
    for key,leaf in re.findall(r'\b(lef_root|stdcell|io|sram) / "([^"]+)"',header): need(bases[key]/leaf)
if group in ['diff','all']:
    for rel in ['src/odb/src/lef/TEST/complete.5.8.lef','src/odb/src/lef/TEST/complete.5.8.lef.au',
                'src/odb/src/def/TEST/complete.5.8.def','src/odb/src/def/TEST/complete.5.8.def.au',
                'src/odb/test/data/gscl45nm.lef','src/gpl/test/nangate45.lef',
                'test/Nangate45/Nangate45_tech.lef','test/Nangate45/Nangate45_stdcell.lef']:
        need(road/rel)
    need(e['OPENDB_PYTHON'])
    need(Path(e['OPENDB_PYTHONPATH'])/'odb.py');need(Path(e['OPENDB_PYTHONPATH'])/'_odb.so')
    env=e.copy();env['PYTHONPATH']=e['OPENDB_PYTHONPATH']+os.pathsep+e.get('PYTHONPATH','')
    try:
        r=subprocess.run([e['OPENDB_PYTHON'],'-c','import odb; db=odb.dbDatabase.create(); assert db is not None; print(odb.__file__)'],env=env,text=True,capture_output=True,timeout=30)
        if r.returncode: errors.append('OpenDB Python import: '+r.stderr.strip())
        else: print('[OK] OpenDB Python: '+r.stdout.strip())
    except (OSError,subprocess.TimeoutExpired) as ex: errors.append('OpenDB Python: '+str(ex))
if group in ['diff','irt','idrc','route','all']:
    for year,total_samples in [(18,3),(19,4)]:
        if group=='diff' and year==19: continue # only test10 solution is used by the OpenDB ISPD19 suite
        root=Path(e[f'ECCDB_ISPD{year}_ROOT'])
        for name in [f'ispd{year}_sample'+('' if i==1 else str(i)) for i in range(1,total_samples+1)]+[f'ispd{year}_test{i}' for i in range(1,11)]:
            for ext in ['lef','def']:need(root/name/f'{name}.input.{ext}')
if group in ['irt','all']:
    need(e['ECCDB_IRT_WRAP_LEF']);need(e['ECCDB_IRT_WRAP_DEF'])
if group=='route-smoke':
    root=Path(e['ECCDB_ISPD18_ROOT'])/'ispd18_sample'
    for ext in ['lef','def']:need(root/f'ispd18_sample.input.{ext}')
if group=='benchmark':
    for name in sys.argv[2:]:
        for ext in ['lef','def']:need(Path(e['ECCDB_ISPD19_ROOT'])/name/f'{name}.input.{ext}')
if ((group in ['diff','all'] and 'ECCDB_RUN_LARGE_DEF_TESTS' in e) or
    (group in ['binary','all'] and 'ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS' in e)):
    root=Path(e['ECCDB_ISPD19_ROOT'])
    need(root/'input/ispd19_test10/ispd19_test10.input.lef');need(root/'solutions/extracted/test10/def/12.t10.def')
if group in ['diff','all'] and 'ECCDB_RUN_LARGE_DEF_TESTS' in e:
    need(road/'src/gpl/test/large01.def');need(road/'src/drt/test/aes_nangate45_preroute.def')
if errors:
    print('\n'.join('[MISSING] '+s for s in errors),file=sys.stderr);sys.exit(1)
print('[OK] '+group+' 所需目录/核心文件检查通过；完整语料由测试继续核验。')
PY
}
case $command_name in
  env) print_env; exit;;
  sources) cat "$script_dir/Data_Sources.zh-CN.md"; exit;;
  doctor)
    group=${group:-all}
    case $group in unit|io|binary|diff|irt|idrc|route-smoke|route|all|benchmark) :;; *) die "未知检查组：$group";; esac
    print_env; check_data "$group"; exit;;
  configure) [[ $group == standalone || $group == tools ]] || die 'configure 需要 standalone 或 tools';;
  build|test) case $group in unit|io|binary|diff|irt|idrc|route-smoke|route|all) :;; *) die '请指定有效 GROUP，见 --help';; esac;;
  benchmark) [[ -z $group ]] || die 'benchmark 用 --case 指定用例';group=benchmark;;
  *) die "未知命令：$command_name";;
esac
[[ -z $filter || $command_name == test ]] || die '--filter 仅用于 test'
mkdir -p -- "$ECCDB_RESULTS_ROOT"
result_dir=$(mktemp -d "$ECCDB_RESULTS_ROOT/${command_name}-${group}.XXXXXX")
# Tool logs and failed fixtures stay under this invocation's output directory.
mkdir -p "$result_dir/tmp"
export TMPDIR="$result_dir/tmp"
print_env > "$result_dir/environment.sh"
git -C "$ECCDB_SRC" rev-parse HEAD > "$result_dir/commit.txt"
git -C "$ECCDB_SRC" diff > "$result_dir/worktree.patch"
git -C "$OPENROAD_SOURCE_DIR" rev-parse HEAD > "$result_dir/openroad-commit.txt" 2>/dev/null || true
printf '结果目录：%s\n' "$result_dir"
printf 'cd -- %q\n' "$result_dir" > "$result_dir/commands.sh"
step=0
run_logged() {
  local label=$1; shift
  step=$((step+1))
  printf '%q ' "$@" >> "$result_dir/commands.sh"; printf '\n' >> "$result_dir/commands.sh"
  echo "[$step] $label"
  (cd -- "$result_dir" && "$@") 2>&1 | tee "$result_dir/$step-$label.log"
}
configure_build() {
  local kind=$1 build source
  local opts=()
  if [[ $kind == standalone ]]; then
    build=$ECCDB_DIFF_BUILD;source=$ECCDB_SRC/src/database/eccdb
    opts+=(-DECCDB_STANDALONE_LEF_DEF=ON -DECCDB_STANDALONE_LEGACY_IDB=ON)
  else
    build=$ECCDB_TOOLS_BUILD;source=$ECCDB_SRC
    opts+=(-DECCDB_BUILD_BENCHMARKS=ON)
  fi
  if [[ ! -f $build/CMakeCache.txt ]]; then
    opts+=(-G Ninja)
    if [[ -n ${CC:-} ]]; then opts+=("-DCMAKE_C_COMPILER=$CC");
    elif command -v gcc-13 >/dev/null; then opts+=(-DCMAKE_C_COMPILER=gcc-13); fi
    if [[ -n ${CXX:-} ]]; then opts+=("-DCMAKE_CXX_COMPILER=$CXX");
    elif command -v g++-13 >/dev/null; then opts+=(-DCMAKE_CXX_COMPILER=g++-13); fi
  fi
  run_logged "configure-$kind" cmake -S "$source" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release -DECCDB_BUILD_TESTS=ON \
    "-DECCDB_TEST_DATA_ROOT=$ECCDB_TEST_DATA_ROOT" "-DOPENROAD_SOURCE_DIR=$OPENROAD_SOURCE_DIR" \
    "-DOPENDB_PYTHON_MODULE_DIR=$OPENDB_PYTHON_MODULE_DIR" "${opts[@]}"
}
unit_targets=(eccdb_geometry_pool_test eccdb_design_test eccdb_library_site_cell_master_test eccdb_cut_layer_test eccdb_routing_layer_test eccdb_tech_database_test eccdb_non_routing_layer_test)
io_targets=(eccdb_lef_tech_importer_test eccdb_lef_library_importer_test eccdb_lef_tech_exporter_test eccdb_lef_library_exporter_test eccdb_def_design_roundtrip_test)
binary_targets=(eccdb_binary_database_archive_test eccdb_design_binary_database_archive_test)
diff_targets=(eccdb_lef_idb_conversion_test eccdb_direct_lef_differential_test eccdb_lef_full_corpus_semantic_test eccdb_def_full_corpus_semantic_test eccdb_opendb_design_differential_test)
select_group() {
  kind=standalone;selection='*'
  case $1 in
    unit) targets=("${unit_targets[@]}");;
    io) targets=("${io_targets[@]}");;
    binary) targets=("${binary_targets[@]}");;
    diff) targets=("${diff_targets[@]}");;
    irt) kind=tools;targets=(irt_adapter_differential_test);selection='Wrap.*:SelfCheck.*:Writeback.*';;
    idrc) kind=tools;targets=(idrc_adapter_differential_test);selection='Wrap.*:Shapes.*:SelfCheck.*';;
    route-smoke) kind=tools;targets=(irt_adapter_differential_test);selection='IspdUnder100k/*/Ispd18Sample';;
    route) kind=tools;targets=(irt_adapter_differential_test);selection='IspdUnder100k/*';;
    benchmark) kind=tools;targets=(eccdb_benchmark irt_input_benchmark eccdb_binary_archive_benchmark);;
  esac
  [[ -z $filter ]] || selection=$filter
  if [[ $1 == diff && ! -v ECCDB_RUN_LARGE_DEF_TESTS ]]; then
    # This ISPD19 case is NOT gated by getenv() in the existing C++ code.
    if [[ $selection == *-* ]]; then selection+=':*test10_team12*'; else selection+='-*test10_team12*'; fi
    echo '[范围] 默认排除超大 test10_team12；使用 --large 纳入。'
  fi
  if [[ $kind == tools ]]; then build=$ECCDB_TOOLS_BUILD; else build=$ECCDB_DIFF_BUILD; fi
}
prepared_standalone=0 prepared_tools=0
prepare_group() {
  if ((no_build)); then
    [[ -f $build/CMakeCache.txt ]] || die "没有构建配置：$build"
    if [[ $kind == standalone ]]; then
      local actual
      actual=$(sed -n 's/^ECCDB_TEST_DATA_ROOT:PATH=//p' "$build/CMakeCache.txt")
      [[ $actual == "$ECCDB_TEST_DATA_ROOT" ]] || die "已编译数据根目录为 $actual；去掉 --no-build 重新配置"
    fi
    return
  fi
  if [[ $kind == standalone && $prepared_standalone == 0 ]]; then configure_build standalone; prepared_standalone=1; fi
  if [[ $kind == tools && $prepared_tools == 0 ]]; then configure_build tools; prepared_tools=1; fi
  run_logged "build-$active_group" cmake --build "$build" --parallel "$ECCDB_BUILD_JOBS" --target "${targets[@]}"
}
find_binary() {
  local target=$1 found
  if [[ $kind == tools ]]; then
    [[ -x $ECCDB_TOOLS_BIN/$target ]] || die "找不到 $ECCDB_TOOLS_BIN/$target；自定义输出目录请设置 ECCDB_TOOLS_BIN"
    printf '%s\n' "$ECCDB_TOOLS_BIN/$target"
  else
    found=$(find "$build/tests" -type f -name "$target" -executable -print -quit)
    [[ -n $found ]] || die "找不到已构建目标：$target"
    printf '%s\n' "$found"
  fi
}
if [[ $command_name == configure ]]; then configure_build "$group"; exit; fi
failures=0 executed=0
run_test_group() {
  local target binary listfile name index=0
  for target in "${targets[@]}"; do
    binary=$(find_binary "$target")
    listfile=$result_dir/$active_group-$target.list
    "$binary" --gtest_list_tests "--gtest_filter=$selection" > "$listfile"
    # Parse GTest's own filtering, including parameterized suites; no shell eval.
    python3 - "$listfile" > "$listfile.names" <<'PY'
import sys
suite=''
for line in open(sys.argv[1]):
    clean=line.split('#',1)[0].strip()
    if not clean:continue
    if not line[0].isspace() and clean.endswith('.'):suite=clean
    elif line.startswith('  ') and suite:
        name=suite+clean
        if not any(part.startswith('DISABLED_') for part in name.replace('.','/').split('/')):print(name)
PY
    [[ -s $listfile.names ]] || { echo "[未匹配] $target: $selection"; continue; }
    if [[ $active_group == route || $active_group == route-smoke ]]; then
      while IFS= read -r name; do
        index=$((index+1));executed=$((executed+1))
        if run_logged "$active_group-$index" "$binary" "--gtest_filter=$name" "--gtest_output=xml:$result_dir/$active_group-$index.xml"; then :; else failures=$((failures+1)); fi
      done < "$listfile.names"
    else
      executed=$((executed+1))
      if run_logged "$active_group-$target" "$binary" "--gtest_filter=$selection" "--gtest_output=xml:$result_dir/$active_group-$target.xml"; then :; else failures=$((failures+1)); fi
    fi
  done
}
run_benchmark() {
  local repeat name source target binary prefix
  local sources=()
  for ((repeat=1;repeat<=repeats;repeat++)); do
    sources=(idb entt); ((repeat%2)) || sources=(entt idb)
    for name in "${cases[@]}"; do
      local lef=$ECCDB_ISPD19_ROOT/$name/$name.input.lef def=$ECCDB_ISPD19_ROOT/$name/$name.input.def
      for target in eccdb_benchmark irt_input_benchmark; do
        binary=$(find_binary "$target")
        for source in "${sources[@]}"; do
          prefix=$name-r$repeat-$target-$source
          local extra=(); [[ $target != eccdb_benchmark ]] || extra=(--writes 256)
          if run_logged "$prefix" "$binary" --lef "$lef" --def "$def" --source "$source" --output "$result_dir/$prefix.jsonl" "${extra[@]}"; then :; else failures=$((failures+1)); fi
        done
      done
      prefix=$name-r$repeat-binary-archive;binary=$(find_binary eccdb_binary_archive_benchmark)
      if run_logged "$prefix" "$binary" --lef "$lef" --def "$def" --archive-dir "$result_dir/$prefix-archives" --output "$result_dir/$prefix.jsonl"; then :; else failures=$((failures+1)); fi
    done
  done
}
groups=("$group")
[[ $group != all ]] || groups=(unit io binary diff irt idrc route-smoke)
for active_group in "${groups[@]}"; do
  select_group "$active_group"
  if [[ $command_name != build ]]; then check_data "$active_group"; fi
  prepare_group
  case $command_name in test) run_test_group;; benchmark) run_benchmark;; esac
done
if [[ $command_name == test ]]; then
  ((executed>0)) || die '过滤器未匹配到任何测试，不能视为通过'
  if python3 - "$result_dir" "$strict" "$failures" <<'PY'
import json,sys,xml.etree.ElementTree as ET
from pathlib import Path
root=Path(sys.argv[1]);counts=dict(passed=0,failed=0,skipped=0)
for path in sorted(root.glob('*.xml')):
    tree=ET.parse(path)
    for test in tree.iter('testcase'):
        if test.find('failure') is not None or test.find('error') is not None:key='failed'
        elif test.find('skipped') is not None or test.get('status')=='notrun':key='skipped'
        else:key='passed'
        counts[key]+=1
counts['failed_commands']=int(sys.argv[3])
(root/'summary.json').write_text(json.dumps(counts,indent=2))
print('JUnit 汇总：',counts)
sys.exit(bool(counts['failed'] or (sys.argv[2]=='1' and counts['skipped']) or not sum(counts[k] for k in ['passed','failed','skipped'])))
PY
  then :; else failures=$((failures+1)); fi
fi
if [[ $command_name == benchmark ]]; then
  if python3 - "$result_dir" <<'PY'
import json,sys,csv,statistics
from pathlib import Path
from collections import defaultdict
root=Path(sys.argv[1]);groups=defaultdict(list);bad=0
for path in root.glob('*.jsonl'):
    for row in map(json.loads,path.read_text().splitlines()):
        bad+=row.get('ok') is False
        if row['operation'] in ['def_write','lef_tech_write','lef_library_write']:bad+=row.get('output_bytes',0)==0
        source=row.get('source','entt');key=(row['case'],source,row['operation'])
        if 'elapsed_ns' in row:groups[key].append(row)
with (root/'summary.csv').open('w') as f:
    writer=csv.writer(f);writer.writerow(['case','source','operation','samples','median_seconds','min_seconds','max_seconds','median_rss_mib'])
    for key,rows in sorted(groups.items()):
        seconds=[r['elapsed_ns']/1e9 for r in rows];rss=[r['rss_after_kib']/1024 for r in rows]
        writer.writerow([*key,len(rows),statistics.median(seconds),min(seconds),max(seconds),statistics.median(rss)])
print('Benchmark 汇总：',root/'summary.csv','失败/空写出记录：',bad)
sys.exit(bool(bad or not groups))
PY
  then :; else failures=$((failures+1)); fi
fi
printf '结束：失败命令/汇总检查=%s，结果目录=%s\n' "$failures" "$result_dir"
((failures==0))
