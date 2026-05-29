#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

ECC_BINARY="${ECC_BINARY:-${REPO_ROOT}/bin/ecc_bin}"
WORKSPACE="${WORKSPACE_HOME:-gcd}"
STEPS=(create_workspace Floorplan fixFanout place CTS legalization route drc filler RCX sta harden)

usage() {
  cat <<EOF
Usage:
  $(basename "$0") [WORKSPACE]
  $(basename "$0") [all|STEP] [WORKSPACE]

Examples:
  $(basename "$0")
  $(basename "$0") gcd
  $(basename "$0") all gcd
  $(basename "$0") CTS gcd
  $(basename "$0") route /path/to/gcd

Environment:
  ECC_BINARY      ECC executable to run. Defaults to ${REPO_ROOT}/bin/ecc_bin.
  WORKSPACE_HOME Default workspace when no WORKSPACE argument is provided.

Steps:
  ${STEPS[*]}
EOF
}

is_supported_step() {
  local candidate="$1"
  local step

  for step in "${STEPS[@]}"; do
    [[ "${candidate}" == "${step}" ]] && return 0
  done

  return 1
}

run_step() {
  local step="$1"
  local step_script="${SCRIPT_DIR}/steps/${step}.tcl"
  local runner
  local status

  if [[ "${step}" == "RCX" ]]; then
    step_script="${SCRIPT_DIR}/steps/rcx.tcl"
  fi

  if [[ ! -f "${step_script}" ]]; then
    echo "step script not found: ${step_script}" >&2
    return 1
  fi

  runner="$(mktemp "${TMPDIR:-/tmp}/ecc-${step}.XXXXXX.tcl")"
  printf 'source {%s}\n' "${step_script}" > "${runner}"

  if "${ECC_BINARY}" -script "${runner}" "${WORKSPACE}"; then
    status=0
  else
    status=$?
  fi

  rm -f "${runner}"
  return "${status}"
}

run_flow() {
  local runner
  local status

  runner="$(mktemp "${TMPDIR:-/tmp}/ecc-rtl2gds.XXXXXX.tcl")"
  cat > "${runner}" <<EOF
set RTL2GDS 1
set RESTORE_DATA 1
set RTL2GDS_FLOW 1

source {${SCRIPT_DIR}/steps/create_workspace.tcl}
source {${SCRIPT_DIR}/steps/Floorplan.tcl}
source {${SCRIPT_DIR}/steps/fixFanout.tcl}
source {${SCRIPT_DIR}/steps/place.tcl}
source {${SCRIPT_DIR}/steps/CTS.tcl}
source {${SCRIPT_DIR}/steps/legalization.tcl}
source {${SCRIPT_DIR}/steps/route.tcl}
source {${SCRIPT_DIR}/steps/drc.tcl}
source {${SCRIPT_DIR}/steps/filler.tcl}
source {${SCRIPT_DIR}/steps/rcx.tcl}
source {${SCRIPT_DIR}/steps/sta.tcl}
source {${SCRIPT_DIR}/steps/harden.tcl}

set RTL2GDS_FLOW 0
step_maybe_flow_exit
EOF

  if "${ECC_BINARY}" -script "${runner}" "${WORKSPACE}"; then
    status=0
  else
    status=$?
  fi

  rm -f "${runner}"
  return "${status}"
}

STEP="all"
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -ge 1 ]]; then
  if [[ "$1" == "all" ]] || is_supported_step "$1"; then
    STEP="$1"
    shift
  else
    WORKSPACE="$1"
    shift
    if [[ $# -ne 0 ]]; then
      echo "too many arguments" >&2
      usage >&2
      exit 2
    fi
  fi
fi

if [[ $# -ge 1 ]]; then
  WORKSPACE="$1"
  shift
fi

if [[ $# -ne 0 ]]; then
  echo "too many arguments" >&2
  usage >&2
  exit 2
fi

if [[ ! -x "${ECC_BINARY}" ]]; then
  echo "ecc binary is not executable: ${ECC_BINARY}" >&2
  echo "Set ECC_BINARY or build ${REPO_ROOT}/bin/ecc_bin first." >&2
  exit 1
fi

cd "${SCRIPT_DIR}"

case "${STEP}" in
  all)
    run_flow
    ;;
  *)
    if is_supported_step "${STEP}"; then
      run_step "${STEP}"
    else
      echo "unsupported step: ${STEP}" >&2
      usage >&2
      exit 2
    fi
    ;;
esac
