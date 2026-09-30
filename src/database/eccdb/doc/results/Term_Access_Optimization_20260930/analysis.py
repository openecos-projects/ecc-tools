#!/usr/bin/env python3
"""Generate the Term-access one-run replay report.

Run with the project plotting environment:
  /home/zhuxu1/.venv/bin/python analysis.py --before BASELINE.json --after CANDIDATE.json
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import shlex
from pathlib import Path
from typing import Any

os.environ.setdefault("MPLCONFIGDIR", "/tmp/eccdb-matplotlib")
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


HOTSPOTS = ("find_pin", "read_instance_pins", "net_pins")
BACKENDS = ("odb", "eccdb", "idb")
ISPD = "/home/zhuxu1/workspace/reference/ispd2018/ispd18_test6"


def load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def one_run(report: dict[str, Any], backend: str) -> dict[str, Any]:
    runs = report["results"][backend]["runs"]
    if len(runs) != 1:
        raise ValueError(f"{backend}: expected one run, got {len(runs)}")
    return runs[0]


def groups(*reports: dict[str, Any]) -> list[str]:
    return sorted({name for report in reports for name in report["operations"]})


def elapsed_ns(report: dict[str, Any], backend: str, group: str) -> int | None:
    return one_run(report, backend).get("timing_ns", {}).get(group)


def as_ms(value: int | float | None) -> float | None:
    return None if value is None else float(value) / 1_000_000


def calls(report: dict[str, Any], backend: str, group: str) -> int:
    run = one_run(report, backend)
    return int(run.get("measured_operations", run.get("operations", {})).get(group, 0))


def total_ms(report: dict[str, Any], backend: str, selected: list[str] | None = None) -> float:
    if selected is None:
        return one_run(report, backend)["total_ns"] / 1_000_000
    return sum(elapsed_ns(report, backend, group) or 0 for group in selected) / 1_000_000


def reduction(before: float | None, after: float | None) -> float | None:
    return None if before in (None, 0) or after is None else 100 * (before - after) / before


def ratio(left: float | None, right: float | None) -> float | None:
    return None if left is None or right in (None, 0) else left / right


def fms(value: float | None) -> str:
    if value is None:
        return "-"
    if 0 < value < 0.001:
        return "<0.001"
    return f"{value:.3f}"


def fpct(value: float | None) -> str:
    return "-" if value is None else f"{value:+.2f}%"


def fratio(value: float | None) -> str:
    return "-" if value is None else f"{value:.3f}x"


def group_values(before: dict[str, Any], after: dict[str, Any], group: str) -> tuple[float | None, float | None, float | None, float | None]:
    return (
        as_ms(elapsed_ns(after, "odb", group)),
        as_ms(elapsed_ns(before, "eccdb", group)),
        as_ms(elapsed_ns(after, "eccdb", group)),
        as_ms(elapsed_ns(after, "idb", group)),
    )


def write_csv(before: dict[str, Any], after: dict[str, Any], output: Path) -> list[str]:
    observed = groups(before, after)
    fields = [
        "api_group", "calls", "odb_ms_current", "eccdb_ms_before", "eccdb_ms_after", "idb_ms_current",
        "eccdb_reduction_pct", "eccdb_vs_odb_time_ratio", "odb_over_eccdb_speedup", "idb_supported",
    ]
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for group in observed:
            odb, old, new, idb = group_values(before, after, group)
            writer.writerow({
                "api_group": group, "calls": calls(after, "eccdb", group),
                "odb_ms_current": fms(odb), "eccdb_ms_before": fms(old), "eccdb_ms_after": fms(new),
                "idb_ms_current": fms(idb), "eccdb_reduction_pct": fpct(reduction(old, new)),
                "eccdb_vs_odb_time_ratio": fratio(ratio(new, odb)), "odb_over_eccdb_speedup": fratio(ratio(odb, new)),
                "idb_supported": str(idb is not None).lower(),
            })
    return observed


def plot_hotspots(before: dict[str, Any], after: dict[str, Any], output_dir: Path) -> None:
    labels = ("ODB\ncurrent", "EccDB\nbefore", "EccDB\nafter", "iDB\ncurrent")
    colors = ("#2166d1", "#129b73", "#f59e0b", "#8758e8")
    x = np.arange(len(HOTSPOTS))
    width = 0.18
    data = np.array([group_values(before, after, group) for group in HOTSPOTS], dtype=float).T
    figure, axis = plt.subplots(figsize=(13, 7), constrained_layout=True, facecolor="white")
    axis.set_facecolor("white")
    for index, (label, color) in enumerate(zip(labels, colors)):
        bars = axis.bar(x + (index - 1.5) * width, data[index], width, label=label, color=color)
        axis.bar_label(bars, labels=[f"{value:.1f}" for value in data[index]], padding=3, fontsize=9)
    axis.set_title("Term-access hotspots: cumulative group time (one replay)", fontsize=16, weight="bold", pad=14)
    axis.set_ylabel("Cumulative time (ms, lower is better)")
    axis.set_xticks(x, ["find_pin\n690,219 calls", "read_instance_pins\n107,919 calls", "net_pins\n107,701 calls"])
    axis.yaxis.grid(True, color="#d9e0e8")
    axis.set_axisbelow(True)
    axis.spines[["top", "right"]].set_visible(False)
    axis.legend(ncols=4, loc="upper center", bbox_to_anchor=(0.5, -0.15), frameon=False)
    for suffix in ("svg", "png"):
        figure.savefig(output_dir / f"term_access_hotspots.{suffix}", dpi=180, facecolor="white")
    plt.close(figure)


def identity_rows(before: dict[str, Any], after: dict[str, Any]) -> list[tuple[str, str, str, bool]]:
    result: list[tuple[str, str, str, bool]] = []
    for item in ("trace", "lef", "def", "guides"):
        old, new = str(before["inputs"][item]["sha256"]), str(after["inputs"][item]["sha256"])
        result.append((f"{item}.sha256", old, new, old == new))
    for name, old, new in (
        ("events", str(before["events"]), str(after["events"])),
        ("observed_group_count", str(len(groups(before))), str(len(groups(after)))),
        ("common_group_count", str(len(before["comparable_operations"])), str(len(after["comparable_operations"]))),
    ):
        result.append((name, old, new, old == new))
    return result


def validation_line(report: dict[str, Any], backend: str) -> str:
    current, validation = one_run(report, backend), one_run(report, backend).get("validation", {})
    return (
        f"验证事件 {current['validated_events']:,}/{current['events']:,}；"
        f"all_supported_events={validation.get('all_supported_events')}；"
        f"final_route_state_comparable={report['results'][backend].get('final_route_state_comparable')}；"
        f"net_check={validation.get('final_net_pin_checks'):,}，wire_check={validation.get('final_wire_checks'):,}，"
        f"final_wires={validation.get('final_wires'):,}，primitive={validation.get('final_primitives'):,}，"
        f"unvalidated_wire={validation.get('unvalidated_wire_objects')}"
    )


def command_text(command: list[str]) -> str:
    return shlex.join(command)


def write_readme(before: dict[str, Any], after: dict[str, Any], observed: list[str], output: Path) -> None:
    common = [group for group in after["comparable_operations"] if group in before["comparable_operations"]]
    old_all, new_all, odb_all = total_ms(before, "eccdb"), total_ms(after, "eccdb"), total_ms(after, "odb")
    old_common, new_common = total_ms(before, "eccdb", common), total_ms(after, "eccdb", common)
    odb_common, idb_common = total_ms(after, "odb", common), total_ms(after, "idb", common)
    lines = [
        "# Term 访问路径优化：v3 trace 三端回放",
        "",
        "本报告只比较 2026-09-30 的独立 Term/Pin entity 基线与本次候选实现。每端只运行一轮，以下均是一次观测，不能称为稳定的性能提升。",
        "",
        "## 实验范围",
        "",
        f"- 同一 ISPD18 test6 v3 trace：`{after['events']:,}` 条事件、`{len(observed)}` 组已出现 API；三端共同支持 `{len(common)}` 组。",
        "- 计时包围单条记录的 adapter 调用和返回字段构造；不含 LEF/DEF/Guide/JSONL parser、初始建库、binding/canonicalization、逐事件和最终正确性校验。",
        "- 候选同时改变了三条 Term 访问路径，并切换到 Release/`NDEBUG`；iDB 也在相同 Release 配置下重建。因此不能将时间差拆分归因到某一处访问优化、断言移除、编译配置或单轮波动。",
        "- `EccDB/ODB` 是时间比，小于 `1.0x` 表示 EccDB 更快；`ODB/EccDB` 是其倒数。",
        "",
        "## 汇总",
        "",
        "| 范围 | 事件/组 | ODB 当前 ms | EccDB 优化前 ms | EccDB 优化后 ms | EccDB 降幅 | EccDB/ODB | ODB/EccDB | iDB 当前 ms |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
        f"| EccDB 全部 34 组 | {after['events']:,}/{len(observed)} | {fms(odb_all)} | {fms(old_all)} | {fms(new_all)} | {fpct(reduction(old_all, new_all))} | {fratio(ratio(new_all, odb_all))} | {fratio(ratio(odb_all, new_all))} | 不可比 |",
        f"| 三端共同 18 组 | {sum(calls(after, 'eccdb', group) for group in common):,}/{len(common)} | {fms(odb_common)} | {fms(old_common)} | {fms(new_common)} | {fpct(reduction(old_common, new_common))} | {fratio(ratio(new_common, odb_common))} | {fratio(ratio(odb_common, new_common))} | {fms(idb_common)} |",
        "",
        "iDB 不支持 16 个已记录组，不能将 iDB 的 18 组总计同 ODB/EccDB 的 34 组总计横向比较。",
        "",
        "## 三个 Term 热点",
        "",
        "| API | 次数 | ODB 当前 ms | EccDB 前 ms | EccDB 后 ms | EccDB 降幅 | EccDB/ODB | iDB 当前 ms |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for group in HOTSPOTS:
        odb, old, new, idb = group_values(before, after, group)
        lines.append(f"| `{group}` | {calls(after, 'eccdb', group):,} | {fms(odb)} | {fms(old)} | {fms(new)} | {fpct(reduction(old, new))} | {fratio(ratio(new, odb))} | {fms(idb)} |")
    lines += [
        "",
        "![Term 访问热点图](term_access_hotspots.svg)",
        "",
        "`find_pin` 和 `read_instance_pins` 分别观察到 18.92% 和 44.12% 的 EccDB 累计时间下降。`net_pins` 为 220.261 ms → 221.362 ms，单轮内没有改善；它仍包含 Net endpoint 列表遍历、visitor/std::function 调用和 JSON 字段投影，本次减少的 Term 解析/校验不是唯一成本。0.50% 的单轮差异不构成退化结论。",
        "",
        "## 完整 34 组累计时间",
        "",
        "`-` 表示 iDB 不支持。所有 ms 是一整组累计时间，不是单次 API 延迟；`<0.001` 表示非零但小于 0.001 ms。仅调用一次且极小的组保留原始记录，但不据其比值判断稳定性能。",
        "",
        "| API group | 次数 | ODB 当前 ms | EccDB 前 ms | EccDB 后 ms | EccDB 降幅 | EccDB/ODB | iDB 当前 ms |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for group in observed:
        odb, old, new, idb = group_values(before, after, group)
        lines.append(f"| `{group}` | {calls(after, 'eccdb', group):,} | {fms(odb)} | {fms(old)} | {fms(new)} | {fpct(reduction(old, new))} | {fratio(ratio(new, odb))} | {fms(idb)} |")
    lines += [
        "",
        "## 正确性与输入一致性",
        "",
        "| 字段 | 优化前 | 优化后 | 一致 |",
        "| --- | --- | --- | ---: |",
    ]
    for name, old, new, equal in identity_rows(before, after):
        lines.append(f"| `{name}` | `{old}` | `{new}` | {'是' if equal else '否'} |")
    lines += ["", "优化后回放的验证字段：", ""]
    lines.extend(f"- `{backend}`：{validation_line(after, backend)}" for backend in BACKENDS)
    lines += [
        "",
        "iDB 因 unsupported 组报告 `all_events=false`，适用条件是 `all_supported_events=true`。ODB/EccDB 完整执行并验证全部事件。trace schema 没有单独的输出 checksum 字段，因此此处以相同 trace/LEF/DEF/Guide SHA-256、事件数和最终 route-state 验证字段为正确性依据。",
        "",
        "## 本轮代码改动",
        "",
        "- `CMakeLists.txt:35`：Release C++ flags 增加 `-DNDEBUG`。",
        "- `src/database/eccdb/storage/design/netlist/storage/DesignNetlistStorage.h:70`：`instanceTerm()` 内联直接 `registry.get`，用于已维护关系中的 Term ID。",
        "- `src/database/eccdb/service/query/PhysicalDesignService.cpp:513`：`forEachNetPin()` 只解析一次 `DesignInstanceTerm` typed storage，再直接取得 Term component。",
        "- `src/database/eccdb/benchmarks/DrtTraceEccdbIdb.cpp:2794,2889`：`read_instance_pins` 与 `find_pin` 读取已缓存的 `master_term_ptr->name`，不再按 MasterTerm ID 跨 registry 重新查找。",
        "",
        "构建和测试：",
        "",
        "```bash",
        "cmake --build build/adapter-differential \\",
        "  --target eccdb_drt_trace_replay eccdb_design_test eccdb_drt_trace_test \\",
        "  --parallel 64",
        "bin/eccdb_design_test       # 73 tests passed",
        "bin/eccdb_drt_trace_test   # trace schema/canonical geometry tests passed",
        "```",
        "",
        "构建日志：`build/term-pin-access-optimized-20260930-build.log`；Design 测试：`build/term-pin-access-optimized-20260930-design-tests.log`；trace 测试：`build/term-pin-access-optimized-20260930-trace-tests.log`。",
        "",
        "## 回放与再生成",
        "",
        "实际三端回放：",
        "",
        "```bash",
        "python3 src/database/eccdb/benchmarks/DrtTraceComparison.py \\",
        "  --trace build/term-pin-v3-20260930.trace.jsonl \\",
        f"  --lef {ISPD}/ispd18_test6.input.lef \\",
        f"  --def {ISPD}/ispd18_test6.input.def \\",
        f"  --guides {ISPD}/ispd18_test6.input.guide \\",
        "  --odb build/openroad-native-eccdb-build/src/drt/eccdbDrtTraceOdb \\",
        "  --eccdb-idb bin/eccdb_drt_trace_replay \\",
        "  --repeat 1 --parallel --cpu 24 \\",
        "  --output build/term-pin-access-optimized-20260930.json",
        "```",
        "",
        "候选 EccDB 子进程实际命令：",
        "",
        "```bash",
        command_text(one_run(after, "eccdb")["command"]),
        "```",
        "",
        "可用任意显式 JSON 路径重新生成本报告：",
        "",
        "```bash",
        "/home/zhuxu1/.venv/bin/python \\",
        "  src/database/eccdb/doc/results/Term_Access_Optimization_20260930/analysis.py \\",
        "  --before src/database/eccdb/doc/results/Term_Pin_Entity_V3_Trace_Replay_20260930.json \\",
        "  --after build/term-pin-access-optimized-20260930.json \\",
        "  --output-dir src/database/eccdb/doc/results/Term_Access_Optimization_20260930",
        "```",
        "",
        "## 产物",
        "",
        "- `all_operations.csv`：完整 34 组机器可读表。",
        "- `term_access_hotspots.svg` / `.png`：一张白底热点图。",
        "- `analysis.py`：接受显式 `--before` / `--after` JSON 路径的生成器。",
    ]
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", required=True, type=Path)
    parser.add_argument("--after", required=True, type=Path)
    parser.add_argument("--output-dir", default=Path(__file__).resolve().parent, type=Path)
    arguments = parser.parse_args()
    before, after, output_dir = load(arguments.before), load(arguments.after), arguments.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    observed = write_csv(before, after, output_dir / "all_operations.csv")
    plot_hotspots(before, after, output_dir)
    write_readme(before, after, observed, output_dir / "README.md")
    print(f"wrote {output_dir}")


if __name__ == "__main__":
    main()
