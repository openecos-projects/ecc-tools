# Copy outside the repository and use: run_tests.sh --config /path/to/config.sh ...
# This is a Bash file: only source configuration that you trust.
# Download/source URLs: Data_Sources.zh-CN.md (run_tests.sh sources).
# Example layout: workspace/{ecc-tools,reference,scripts,OpenROAD}.
ECCDB_WORKSPACE=/path/to/workspace # replace with your data/source parent directory
ECCDB_SRC="$ECCDB_WORKSPACE/ecc-tools"
ECCDB_TEST_DATA_ROOT="$ECCDB_WORKSPACE" # contains scripts/foundry; NOT reference itself
ECCDB_ISPD18_ROOT="$ECCDB_WORKSPACE/reference/ispd2018"
ECCDB_ISPD19_ROOT="$ECCDB_WORKSPACE/reference/ispd2019"
OPENROAD_SOURCE_DIR="$ECCDB_WORKSPACE/OpenROAD"
OPENDB_PYTHON=python3 # resolved to an absolute executable path by the runner
OPENDB_PYTHON_MODULE_DIR="$OPENROAD_SOURCE_DIR/bazel-bin/src/odb"
OPENDB_PYTHONPATH="$OPENDB_PYTHON_MODULE_DIR"

ECCDB_DIFF_BUILD="$ECCDB_SRC/build/eccdb-differential"
ECCDB_TOOLS_BUILD="$ECCDB_SRC/build/adapter-differential"
ECCDB_TOOLS_BIN="$ECCDB_SRC/bin" # root CMake currently forces bin/ by default
ECCDB_BUILD_JOBS=$(nproc)
ECCDB_NET_COMPARE_THREADS=128
ECCDB_RT_THREAD_NUMBER=128
OMP_NUM_THREADS=$(nproc)
ECCDB_KEEP_TEMP=1
ECCDB_RESULTS_ROOT=/tmp/eccdb-test-results

# The runner converts 0 into unset; raw C++ tests treat even '=0' as enabled!
ECCDB_RUN_LARGE_DEF_TESTS=0
ECCDB_RUN_LARGE_DESIGN_BINARY_TESTS=0
ECCDB_IRT_WRAP_LEF="$ECCDB_ISPD19_ROOT/ispd19_sample3/ispd19_sample3.input.lef"
ECCDB_IRT_WRAP_DEF="$ECCDB_ISPD19_ROOT/ispd19_sample3/ispd19_sample3.input.def"

UV_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple
PIP_INDEX_URL="$UV_INDEX_URL"
# New build directories prefer gcc-13/g++-13 if installed. Existing compilers
# and toolchain cache entries are preserved. For another compiler, use a NEW
# build directory and set CC/CXX before invoking the runner.
