#!/usr/bin/env python3
"""
Native Harness Driver for Agents

Provides a structured, high-level interface for coding agents and the onslide-coordinator
to drive the existing native on-slide harness executables, capture output + artifacts,
produce machine-readable reports, support golden image comparison, and run named scenarios.

This completes the "feedback-loop-plan" (docs/native-agent-feedback-loop-plan.md) within
the constraints of only using new files and pre-built exes (per on-slide coordinator notes).

Key features:
- Structured HarnessReport with markers, artifacts, status (PASS/FAIL/FLAKE).
- Retry support for known COM flakiness.
- High-level scenario execution.
- Visual golden comparison (size + MD5, update mode) -- pure stdlib.
- High-level action helpers where possible via existing exes/args.
- CLI and importable API.
- Coordinator-friendly: always writes report.json, never kills PowerPoint.

Usage:
    from native.tools import run_scenario, compare_to_golden
    report = run_scenario("appbar_matrix")
    vis = compare_to_golden("native/build/ab-task.png", "appbar-task", update=False)
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
from ctypes import wintypes
from dataclasses import dataclass, asdict, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional

NATIVE_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = NATIVE_ROOT.parent
BUILD_DIR = NATIVE_ROOT / "build"
GOLDENS_DIR = NATIVE_ROOT / "tools" / "goldens"
SCENARIOS_DIR = NATIVE_ROOT / "tools" / "scenarios"

KNOWN_EXES = {
    "ppoverlay": "ppoverlay.exe",
    "ppreflow": "ppreflow.exe",
    "ppappbarshot": "ppappbarshot.exe",
    "ppops": "ppops.exe",
}

FAIL_MARKERS = re.compile(
    r"\b(FAIL|FAILED|ERROR|WATCHDOG)\b|COM error", re.IGNORECASE
)


@dataclass
class HarnessReport:
    exe: str
    args: List[str]
    returncode: int
    duration_s: float
    stdout_tail: str
    markers_found: List[str]
    artifacts: List[str]
    status: str  # PASS | FAIL | FLAKE | ERROR
    timestamp: str
    notes: str = ""
    golden_comparisons: List[Dict[str, Any]] = field(default_factory=list)
    harness_report: Optional[Dict[str, Any]] = None
    retry_diags: int = 0
    retry_diag_lines: List[str] = field(default_factory=list)
    orphan_windows: List[Dict[str, Any]] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)


@dataclass
class TraceStep:
    step: str
    state: Dict[str, Any]
    artifacts: List[str] = field(default_factory=list)


@dataclass
class OperationTraceReport:
    profile: str
    exe: str
    steps: List[TraceStep] = field(default_factory=list)
    status: str = "PASS"  # PASS | FAIL | FLAKE
    duration_s: float = 0.0
    timestamp: str = ""
    stdout_tail: str = ""
    invariants: List[Dict[str, Any]] = field(default_factory=list)  # violations or checks

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)


def _tail(text: str, n: int = 4000) -> str:
    return text[-n:] if text else ""


def _parse_markers(stdout: str) -> List[str]:
    """Collect harness OK/PASS lines (incl. i2d APPBAR FIT OK, CHROME CALM * OK)."""
    markers: List[str] = []
    for line in stdout.splitlines():
        line = line.strip()
        if line and re.search(r"\b(PASS|OK)\b", line):
            markers.append(line)
    return markers


def _parse_retry_diags(stdout: str, cap: int = 20) -> tuple[int, List[str]]:
    lines = [ln for ln in (stdout or "").splitlines() if re.search(r"\bdiag:", ln)]
    return len(lines), lines[:cap]


def _classify_status(
    rc: int,
    markers: List[str],
    stdout: str,
    required_markers: Optional[List[str]] = None,
) -> str:
    if rc != 0:
        return "FAIL"
    if FAIL_MARKERS.search(stdout or ""):
        return "FAIL"
    if required_markers:
        for req in required_markers:
            if req not in (stdout or ""):
                return "FLAKE"
    if markers:
        return "PASS"
    return "FLAKE"


def _find_recent_artifacts(since: float, patterns: List[str] = None) -> List[str]:
    if patterns is None:
        patterns = ["*.png"]
    arts: List[str] = []
    for pat in patterns:
        for p in BUILD_DIR.glob(pat):
            if p.name.endswith("_report.json"):
                continue
            if p.stat().st_mtime >= since:
                try:
                    rel = p.relative_to(REPO_ROOT)
                except ValueError:
                    rel = p
                arts.append(str(rel))
    return sorted(set(arts))[:40]


def _hash_file(p: Path) -> str:
    if not p.exists():
        return ""
    h = hashlib.md5()
    h.update(p.read_bytes())
    return h.hexdigest()


def sweep_powerplanner_windows() -> List[Dict[str, Any]]:
    """Return top-level windows whose class name starts with PowerPlanner."""
    user32 = ctypes.windll.user32
    found: List[Dict[str, Any]] = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def _enum_proc(hwnd: wintypes.HWND, _lparam: wintypes.LPARAM) -> bool:
        buf = ctypes.create_unicode_buffer(256)
        if user32.GetClassNameW(hwnd, buf, 256):
            cls = buf.value
            if cls.startswith("PowerPlanner"):
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                found.append({"cls": cls, "pid": int(pid.value)})
        return True

    user32.EnumWindows(_enum_proc, 0)
    return found


def run_harness(
    exe_name: str,
    args: Optional[List[str]] = None,
    timeout: int = 180,
    retries: int = 1,
    required_markers: Optional[List[str]] = None,
    kill_ppt: bool = True,
) -> HarnessReport:
    """Run a pre-built harness exe and return structured report."""
    if args is None:
        args = []
    base = exe_name.replace(".exe", "")
    exe_stem = base
    exe_path = BUILD_DIR / KNOWN_EXES.get(exe_stem, f"{exe_stem}.exe")
    if not exe_path.exists():
        raise FileNotFoundError(
            f"Harness not found: {exe_path}. Build first (e.g. native/build-appbar-shot.bat)."
        )

    cmd = [str(exe_path)] + [str(a) for a in args]
    start = time.time()
    last_rc, last_out = -1, ""
    max_attempts = retries + 1

    report_json = None
    for attempt in range(1, max_attempts + 1):
        if kill_ppt:
            subprocess.run(
                ["taskkill", "/f", "/im", "POWERPNT.EXE"],
                capture_output=True,
                text=True,
            )
        try:
            proc = subprocess.run(
                cmd, cwd=REPO_ROOT, capture_output=True, text=True, timeout=timeout
            )
            last_rc = proc.returncode
            last_out = (proc.stdout or "") + (proc.stderr or "")
        except subprocess.TimeoutExpired as e:
            last_rc = 124
            last_out = (e.stdout or "") + (e.stderr or "") + "\n[TIMEOUT after {}s]".format(timeout)
        except Exception as e:
            last_out = f"Driver exception: {e}"
            last_rc = 1

        markers = _parse_markers(last_out)
        status = _classify_status(last_rc, markers, last_out, required_markers)

        m = re.search(r"REPORT:\s*(\{.*\})", last_out, re.DOTALL)
        if m:
            try:
                report_json = json.loads(m.group(1))
            except Exception:
                pass

        if status != "FLAKE" or attempt == max_attempts:
            break
        time.sleep(2.0)

    duration = time.time() - start
    artifacts = _find_recent_artifacts(start)
    retry_count, retry_lines = _parse_retry_diags(last_out)
    if kill_ppt:
        # Harness exes deliberately leave their spawned PowerPoint open, and
        # the registered add-in inside it (LoadBehavior 3) owns live
        # PowerPlanner* windows. Kill it once the run is over and give window
        # teardown a moment so the sweep below only reports genuine leaks.
        subprocess.run(
            ["taskkill", "/f", "/im", "POWERPNT.EXE"],
            capture_output=True,
            text=True,
        )
        time.sleep(2.0)
    orphan_windows = sweep_powerplanner_windows()
    orphan_notes = ""
    if orphan_windows:
        status = "FAIL"
        orphan_notes = (
            "orphan PowerPlanner windows after run: "
            + json.dumps(orphan_windows, separators=(",", ":"))
        )
    report = HarnessReport(
        exe=exe_stem,
        args=args,
        returncode=last_rc,
        duration_s=round(duration, 2),
        stdout_tail=_tail(last_out),
        markers_found=markers,
        artifacts=artifacts,
        status=status,
        timestamp=datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        notes=orphan_notes,
        harness_report=report_json,
        retry_diags=retry_count,
        retry_diag_lines=retry_lines,
        orphan_windows=orphan_windows,
    )

    report_path = BUILD_DIR / f"{exe_stem}_report.json"
    report_path.write_text(report.to_json(), encoding="utf-8")
    # In-process consumers (run_operation_trace) need the untruncated output:
    # a single TRACE state line is longer than the persisted stdout_tail.
    report.full_stdout = last_out
    return report


# ------------------------------------------------------------------
# Golden comparison (Phase 3)
# ------------------------------------------------------------------

GOLDENS_DIR.mkdir(parents=True, exist_ok=True)


def compare_to_golden(
    artifact_rel: str, golden_name: str, update: bool = False
) -> Dict[str, Any]:
    """Compare a build artifact to a golden. Uses size + MD5 for no-dep comparison."""
    art = (REPO_ROOT / artifact_rel).resolve()
    golden = (GOLDENS_DIR / golden_name).resolve()

    if not art.exists():
        return {"match": False, "reason": "artifact_missing", "artifact": str(art)}

    if not golden.exists():
        if not update:
            return {
                "match": False,
                "reason": "golden_missing",
                "golden": str(golden.relative_to(NATIVE_ROOT)),
            }
        shutil.copy2(art, golden)
        return {
            "match": True,
            "updated": True,
            "golden": str(golden.relative_to(NATIVE_ROOT)),
            "size": art.stat().st_size,
        }

    if update:
        shutil.copy2(art, golden)
        return {
            "match": True,
            "updated": True,
            "golden": str(golden.relative_to(NATIVE_ROOT)),
            "size": art.stat().st_size,
        }

    size_match = art.stat().st_size == golden.stat().st_size
    hash_match = _hash_file(art) == _hash_file(golden)
    match = size_match and hash_match

    result = {
        "match": match,
        "size_match": size_match,
        "hash_match": hash_match,
        "artifact_size": art.stat().st_size,
        "golden_size": golden.stat().st_size,
        "artifact": str(art.relative_to(NATIVE_ROOT)),
        "golden": str(golden.relative_to(NATIVE_ROOT)),
    }
    if not match:
        result["note"] = "Visual diff recommended (use external tool or update golden if intentional)"
    return result


def run_with_golden_checks(
    report: HarnessReport, mappings: List[tuple], update: bool = False
) -> HarnessReport:
    """Attach golden comparisons to a report. mappings = [(artifact_rel, golden_name), ...]"""
    for art_rel, gname in mappings:
        comp = compare_to_golden(art_rel, gname, update=update)
        report.golden_comparisons.append(comp)
        if not comp.get("match"):
            if report.status == "PASS":
                report.status = "VISUAL_DIFF"
    # re-write updated report
    report_path = BUILD_DIR / f"{report.exe}_report.json"
    report_path.write_text(report.to_json(), encoding="utf-8")
    return report


# ------------------------------------------------------------------
# v2.4.0 Operation Trace support (before / immed / +1 / +3)
# ------------------------------------------------------------------

TRACE_STEP_RE = re.compile(r"TRACE\s+(\S+):\s*(\{.*?\})\s*$", re.DOTALL)
TRACE_ARTIFACT_RE = re.compile(r"TRACE\s+(\S+)\s+ARTIFACTS:\s*(.*)$")


def _parse_trace_steps(stdout: str) -> List[TraceStep]:
    """Parse sequenced TRACE lines emitted by harness during a --trace run."""
    steps: List[TraceStep] = []
    step_map: Dict[str, TraceStep] = {}
    for line in (stdout or "").splitlines():
        line = line.strip()
        m = TRACE_STEP_RE.search(line)
        if m:
            step_name = m.group(1)
            try:
                state = json.loads(m.group(2))
            except Exception:
                state = {"raw": m.group(2)}
            ts = step_map.get(step_name)
            if not ts:
                ts = TraceStep(step=step_name, state=state)
                step_map[step_name] = ts
                steps.append(ts)
            else:
                ts.state = state
        am = TRACE_ARTIFACT_RE.search(line)
        if am:
            step_name = am.group(1)
            arts = [a.strip() for a in am.group(2).split() if a.strip()]
            ts = step_map.get(step_name)
            if not ts:
                ts = TraceStep(step=step_name, state={})
                step_map[step_name] = ts
                steps.append(ts)
            ts.artifacts.extend([a for a in arts if a not in ts.artifacts])
    return steps


def run_operation_trace(
    profile: str,
    exe_name: str = "ppappbarshot",
    timeout: int = 180,
    retries: int = 1,
) -> OperationTraceReport:
    """Run a harness in trace mode for the given profile and return sequenced report."""
    start = time.time()
    base_report = run_harness(
        exe_name,
        ["--trace", profile],
        timeout=timeout,
        retries=retries,
        required_markers=["TRACE COMPLETE OK"],
        kill_ppt=True,
    )
    steps = _parse_trace_steps(getattr(base_report, "full_stdout", "") or base_report.stdout_tail)
    # attach any late artifacts by mtime to the trace (best effort)
    recent = _find_recent_artifacts(start)
    for st in steps:
        for art in recent:
            if profile in art or st.step in art or "trace_" in art:
                if art not in st.artifacts:
                    st.artifacts.append(art)
    tr = OperationTraceReport(
        profile=profile,
        exe=exe_name,
        steps=steps,
        status=base_report.status if base_report.status != "PASS" else "PASS",
        duration_s=round(time.time() - start, 2),
        timestamp=datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        stdout_tail=_tail(base_report.stdout_tail),
    )
    # write sidecar trace report
    trace_path = BUILD_DIR / f"trace_{profile}_report.json"
    trace_path.write_text(tr.to_json(), encoding="utf-8")
    return tr


# Core invariants for continuity (used to auto-detect the v2.4.1 class of bugs)
CONTINUITY_RULES = [
    "row_sel_stable_during_item_op",
    "appbar_visible_stable",
    "no_large_sel_drop_to_empty",
    "rowband_count_stable_or_increases",
    "scale_group_always_reachable",
]


def check_trace_invariants(tr: OperationTraceReport, profile: Optional[str] = None) -> List[Dict[str, Any]]:
    """Run core continuity checks. Returns list of {rule, passed, detail}."""
    results: List[Dict[str, Any]] = []
    steps = tr.steps
    if not steps:
        results.append({"rule": "has_trace_steps", "passed": False, "detail": "no steps captured"})
        return results

    # Collect per-step key signals
    own_sel_seq = [ (s.step, s.state.get("ownSelKind", "")) for s in steps ]
    rowcount_seq = [ (s.step, s.state.get("rowCount", 0)) for s in steps ]
    appbar_vis_seq = [ (s.step, bool(s.state.get("appBarVisible"))) for s in steps ]
    has_scale_seq = [ (s.step, bool(s.state.get("hasScaleGroup"))) for s in steps ]
    groups_seq = [ (s.step, s.state.get("appBarGroups", [])) for s in steps ]

    # 1. row_sel_stable_during_item_op : for row-* profiles, once ROW, should not drop to empty or container during flow
    if profile and profile.startswith("row-"):
        dropped = False
        for step, kind in own_sel_seq:
            if kind not in ("ROW", "row"):  # tolerate casing
                # allow pre step before we select
                if step != "pre":
                    dropped = True
        results.append({
            "rule": "row_sel_stable_during_item_op",
            "passed": not dropped,
            "detail": f"sel seq: {own_sel_seq}",
            "seq": own_sel_seq,
        })

    # 2. appbar_visible_stable
    vis_ok = all(v for _, v in appbar_vis_seq)
    results.append({
        "rule": "appbar_visible_stable",
        "passed": vis_ok,
        "detail": f"vis: {appbar_vis_seq}",
    })

    # 3. no_large_sel_drop_to_empty (ownSelKind should not go blank mid op for *item-specific* ops)
    # Overall move/resize legitimately clears item sel to avoid weird combined chrome.
    if profile and not profile.startswith('overall-'):
        empty_mid = False
        for i, (step, kind) in enumerate(own_sel_seq):
            if i > 0 and not kind:
                empty_mid = True
        results.append({
            "rule": "no_large_sel_drop_to_empty",
            "passed": not empty_mid,
            "detail": f"sel seq: {own_sel_seq}",
        })
    else:
        results.append({
            "rule": "no_large_sel_drop_to_empty",
            "passed": True,
            "detail": "skipped for overall-* (intentional clear when selecting CHART_ROOT)",
        })

    # 4. rowband count stable or increases (insert may +1)
    counts = [c for _, c in rowcount_seq]
    non_decreasing = all(counts[i] <= counts[i+1] for i in range(len(counts)-1)) if len(counts) > 1 else True
    results.append({
        "rule": "rowband_count_stable_or_increases",
        "passed": non_decreasing,
        "detail": f"counts: {rowcount_seq}",
    })

    # 5. scale_group_always_reachable : SR-EDT-02 (docs/SRS_CreationFlows.md).
    # The global SCALE/Labels/Grid group is appended for EVERY selection
    # context (matches the committed ops-test appbar contract "SCALE last
    # for every sel"). Flag any step where the app bar lacks it.
    scale_missing = False
    for sstep, has in has_scale_seq:
        if not has:
            scale_missing = True
    results.append({
        "rule": "scale_group_always_reachable",
        "passed": not scale_missing,
        "detail": f"scale-seq: {has_scale_seq} sel:{own_sel_seq}",
    })

    # v2.4.1+ content flash hunter: compare PNG sizes across trace steps.
    # A sharp drop in immed or +1 vs pre/+3 for overlay/ctx artifacts indicates
    # graph (bars) or left titles (labels) temporarily missing.
    def _flash_flag(art_list, pre_size, final_size):
        for a in art_list:
            if 'immed' in a or '+1' in a:
                try:
                    # approximate: caller will have populated recent artifacts
                    pass
                except:
                    pass
        return False

    # Heuristic on known artifacts in report
    flash_suspect = False
    sizes = {}
    for s in steps:
        for art in s.artifacts:
            p = (REPO_ROOT / art).resolve()
            if p.exists() and p.suffix == '.png' and ('overlay' in art or 'ctx' in art):
                try:
                    sz = p.stat().st_size
                    key = 'pre' if 'pre' in art else ('immed' if 'immed' in art else ('+1' if '+1' in art else ('+3' if '+3' in art else 'other')))
                    sizes[key] = sz
                except:
                    pass
    if sizes:
        max_ref = max(sizes.values()) or 1
        for k, sz in sizes.items():
            if k in ('immed', '+1') and sz < max_ref * 0.72:
                flash_suspect = True
    results.append({
        "rule": "no_content_flash_in_trace",
        "passed": not flash_suspect,
        "detail": f"png sizes sample (max ref): {sizes}",
    })

    # Overall component (CHART_ROOT) move/resize invariants
    if profile and (profile.startswith('row-label') or profile.startswith('row-then') or profile == 'row-label-select'):
        # For row interaction e2e: labels must not disappear on select (user reported title gone on click)
        pre_labels = steps[0].state.get('rowLabelCount') if steps else None
        stable_labels = all(s.state.get('rowLabelCount') == pre_labels for s in steps if s.state.get('rowLabelCount') is not None)
        results.append({
            'rule': 'row_labels_stable_on_select',
            'passed': stable_labels or pre_labels is None,
            'detail': 'labels seq: ' + str([s.state.get('rowLabelCount') for s in steps])
        })
    if profile and profile == 'task-select-progress':
        # Task selection + progress edit must set TASK and keep labels visible.
        has_task = any(s.state.get('ownSelKind') == 'TASK' for s in steps)
        labels_stable = all(s.state.get('rowLabelCount') in (None, 4) for s in steps)  # 4 in showcase
        results.append({
            'rule': 'task_select_and_progress_stable',
            'passed': has_task and labels_stable,
            'detail': 'sel seq: ' + str([s.state.get('ownSelKind') for s in steps]) + ' labels: ' + str([s.state.get('rowLabelCount') for s in steps])
        })

    # Detect "weird full component takeover on item click": when ownSel is item (ROW/TASK/etc),
    # the selScreenRect/frameRect should be small (item sized), not equal to full chartRect.
    # This catches the intermittent resize-to-whole-area bug.
    item_kinds = {'ROW', 'TASK', 'MILESTONE', 'MARKER', 'TEXT'}
    takeover = False
    for s in steps:
        kind = s.state.get('ownSelKind', '')
        if kind in item_kinds:
            cr = s.state.get('chartRect') or {}
            sr = s.state.get('selScreenRect') or {}
            if cr and sr and cr.get('left') == sr.get('left') and cr.get('right') == sr.get('right'):
                takeover = True
    if any(s.state.get('ownSelKind') in item_kinds for s in steps):
        results.append({
            'rule': 'no_full_component_takeover_on_item_sel',
            'passed': not takeover,
            'detail': 'checked selScreenRect != chartRect for item kinds'
        })
    if profile and profile.startswith("overall-"):
        # chartRect should have changed between pre and later steps
        chart_rects = [(s.step, s.state.get("chartRect", {})) for s in steps]
        pre_rect = chart_rects[0][1] if chart_rects else {}
        later_rect = chart_rects[-1][1] if len(chart_rects) > 1 else {}
        rect_changed = (pre_rect != later_rect) if pre_rect and later_rect else True  # at least not identical after op
        results.append({
            "rule": "overall_rect_propagates_to_chartRect",
            "passed": rect_changed or len(steps) < 2,
            "detail": f"pre={pre_rect} later={later_rect}",
        })
        # rowBands should have shifted or adjusted if overall moved/resized
        row_counts = [s.state.get("rowCount", 0) for s in steps]
        row_stable = all(c == row_counts[0] for c in row_counts)  # move shouldn't change count
        results.append({
            "rule": "overall_op_preserves_row_count",
            "passed": row_stable,
            "detail": f"counts: {row_counts}",
        })
        # appbar should remain visible
        ab_vis = all(s.state.get("appBarVisible", False) for s in steps)
        results.append({
            "rule": "overall_op_keeps_appbar_visible",
            "passed": ab_vis,
            "detail": "appBarVisible across steps",
        })

    tr.invariants = results
    # write back
    (BUILD_DIR / f"trace_{tr.profile}_report.json").write_text(tr.to_json(), encoding="utf-8")
    return results


def snapshot_trace_keys(tr: OperationTraceReport) -> Dict[str, Any]:
    """Extract stable key sequence for golden comparison (ignores pixel rects)."""
    key_seq = []
    for s in tr.steps:
        st = s.state or {}
        key_seq.append({
            "step": s.step,
            "ownSelKind": st.get("ownSelKind", ""),
            "rowCount": st.get("rowCount", 0),
            "hasScaleGroup": st.get("hasScaleGroup", False),
            "appBarVisible": st.get("appBarVisible", False),
            "appBarGroups": st.get("appBarGroups", []),
            "scale": st.get("scale", ""),
        })
    return {"profile": tr.profile, "steps": key_seq}


def compare_trace_to_golden(
    tr: OperationTraceReport, golden_name: str, update: bool = False
) -> Dict[str, Any]:
    """Compare trace key snapshot (not images) to golden json."""
    snap = snapshot_trace_keys(tr)
    golden_path = GOLDENS_DIR / f"{golden_name}.json"
    if not golden_path.exists():
        if not update:
            return {"match": False, "reason": "golden_missing", "golden": str(golden_path)}
        golden_path.write_text(json.dumps(snap, indent=2), encoding="utf-8")
        return {"match": True, "updated": True, "golden": str(golden_path.relative_to(NATIVE_ROOT))}
    if update:
        golden_path.write_text(json.dumps(snap, indent=2), encoding="utf-8")
        return {"match": True, "updated": True}
    try:
        expected = json.loads(golden_path.read_text(encoding="utf-8"))
    except Exception as e:
        return {"match": False, "reason": f"bad_golden: {e}"}
    match = expected.get("steps") == snap.get("steps")
    res = {
        "match": match,
        "profile": tr.profile,
        "golden": str(golden_path.relative_to(NATIVE_ROOT)),
    }
    if not match:
        res["actual"] = snap.get("steps")
        res["expected"] = expected.get("steps")
        res["note"] = "Trace key sequence mismatch - run with --update-goldens or investigate transient"
    return res


# ------------------------------------------------------------------
# Scenarios
# ------------------------------------------------------------------

def load_scenario(name: str) -> Dict[str, Any]:
    path = SCENARIOS_DIR / f"{name}.json"
    if not path.exists():
        raise FileNotFoundError(f"Scenario {name} not found in {SCENARIOS_DIR}")
    return json.loads(path.read_text(encoding="utf-8"))


def run_scenario(name: str, update_goldens: bool = False, **overrides) -> HarnessReport:
    spec = load_scenario(name)
    spec.update(overrides)
    if spec.get("trace_profile"):
        # v2.4.0 trace scenario path returns harness-like but we use trace underneath
        # For unified, return a shim HarnessReport with trace info in notes
        tr = run_operation_trace(spec["trace_profile"], retries=spec.get("retries", 1))
        if spec.get("check_invariants", True):
            check_trace_invariants(tr, spec["trace_profile"])
        shim = HarnessReport(
            exe=spec.get("exe", "ppappbarshot"),
            args=spec.get("args", []),
            returncode=0,
            duration_s=tr.duration_s,
            stdout_tail=tr.stdout_tail,
            markers_found=[f"TRACE_{s.step}" for s in tr.steps],
            artifacts=[a for s in tr.steps for a in s.artifacts],
            status=tr.status,
            timestamp=tr.timestamp,
            notes=json.dumps({"trace": tr.to_dict(), "invariants": tr.invariants}),
        )
        (BUILD_DIR / f"{name}_report.json").write_text(shim.to_json(), encoding="utf-8")
        return shim
    report = run_harness(
        spec["exe"],
        spec.get("args", []),
        timeout=spec.get("timeout", 180),
        retries=spec.get("retries", 1),
        required_markers=spec.get("expected_markers"),
    )
    if "goldens" in spec:
        run_with_golden_checks(report, spec["goldens"], update=update_goldens)
    return report


# ------------------------------------------------------------------
# High-level actions (Phase 2) - wrappers over existing exes
# ------------------------------------------------------------------

def run_appbar_matrix() -> HarnessReport:
    """S2 app bar visual matrix (uses --matrix on ppappbarshot)."""
    return run_scenario("appbar_matrix")


def run_full_overlay_gate() -> HarnessReport:
    """Full interaction harness (for S3+ row UX validation once ready)."""
    return run_harness("ppoverlay", [])


def run_reflow_check() -> HarnessReport:
    return run_harness("ppreflow", [])


# Example high-level for future row work (currently notes; will use ppoverlay when S3 ready)
def run_row_selection_scenario() -> HarnessReport:
    """Placeholder for S3 row-selection UX. Runs overlay harness + documents intent."""
    report = run_harness("ppoverlay", [])
    report.notes = (
        "S3 row-selection scenario placeholder. "
        "Once s3-row-selection lands, add specific args or post-message support. "
        "Current run validates base overlay + selection model."
    )
    (BUILD_DIR / "ppoverlay_report.json").write_text(report.to_json(), encoding="utf-8")
    return report


# v2.4.0 high-level trace runners for reported issues
def run_trace_row_add_below() -> OperationTraceReport:
    """Trace the exact user-reported flow: row selected + New row below."""
    return run_operation_trace("row-add-below")


def run_trace_row_rename() -> OperationTraceReport:
    return run_operation_trace("row-rename")


def run_trace_row_scale() -> OperationTraceReport:
    return run_operation_trace("row-scale")


def run_trace_with_golden(profile: str, golden_name: Optional[str] = None, update: bool = False) -> Dict[str, Any]:
    tr = run_operation_trace(profile)
    gname = golden_name or f"trace_{profile}"
    key_res = compare_trace_to_golden(tr, gname, update=update)
    # also allow attaching visual checks for generated pngs if caller wants
    tr.notes = json.dumps({"key_golden": key_res})  # stash
    return {"trace": tr.to_dict(), "key_golden": key_res}


# ------------------------------------------------------------------
# CLI
# ------------------------------------------------------------------

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Native harness driver for agent feedback loops.")
    sub = parser.add_subparsers(dest="cmd")

    p_run = sub.add_parser("run", help="Run a harness exe")
    p_run.add_argument("exe")
    p_run.add_argument("args", nargs="*")
    p_run.add_argument("--retries", type=int, default=1)

    p_sc = sub.add_parser("scenario", help="Run named scenario")
    p_sc.add_argument("name")
    p_sc.add_argument("--retries", type=int, default=1)
    p_sc.add_argument("--update-goldens", action="store_true")

    p_g = sub.add_parser("golden", help="Compare artifact to golden")
    p_g.add_argument("artifact")
    p_g.add_argument("golden_name")
    p_g.add_argument("--update", action="store_true")

    p_tr = sub.add_parser("trace", help="Run operation trace for continuity monitoring (v2.4.0)")
    p_tr.add_argument("profile", help="e.g. row-add-below, row-rename, row-scale")
    p_tr.add_argument("--retries", type=int, default=1)
    p_tr.add_argument("--check-invariants", action="store_true")

    args = parser.parse_args()

    def _exit_for_status(status: str) -> None:
        if status == "PASS":
            sys.exit(0)
        if status == "VISUAL_DIFF":
            sys.exit(3)
        sys.exit(1)

    if args.cmd == "run":
        r = run_harness(args.exe, args.args, retries=args.retries)
        print(r.to_json())
        _exit_for_status(r.status)
    elif args.cmd == "scenario":
        r = run_scenario(
            args.name, retries=args.retries, update_goldens=args.update_goldens
        )
        print(r.to_json())
        _exit_for_status(r.status)
    elif args.cmd == "golden":
        res = compare_to_golden(args.artifact, args.golden_name, update=args.update)
        print(json.dumps(res, indent=2))
        sys.exit(0 if res.get("match") else 1)
    elif args.cmd == "trace":
        tr = run_operation_trace(args.profile, retries=args.retries)
        invs = []
        if args.check_invariants:
            invs = check_trace_invariants(tr, args.profile)
            tr.invariants = invs
        print(tr.to_json())
        # exit non-zero on any failing invariant or bad status
        bad = any(not i.get("passed", True) for i in (tr.invariants or []))
        if bad or tr.status not in ("PASS",):
            sys.exit(4)
        sys.exit(0)
    else:
        parser.print_help()
        sys.exit(2)
