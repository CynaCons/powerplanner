#!/usr/bin/env python3
"""R2 session report: turn a recorded live session into one agent-readable artifact.

Input: session dir with events.jsonl + meta.json (+ optional frames/).
See docs/session-recorder-spec.md (§R1b event schema, §R2 requirements).

Usage (from repo root):
  python native/tools/session_report.py <session-dir> [--out report.md]
  python native/tools/session_report.py <session-dir> --assert <scenario.json>
  python native/tools/session_report.py --selftest
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

# --- paths -----------------------------------------------------------------

TOOLS_DIR = Path(__file__).resolve().parent
FIXTURE_DIR = TOOLS_DIR / "testdata" / "session-fixture"
PAINT_GAP_MS = 500  # anomaly threshold during active gestures
SUPPRESSION_WINDOW_MS = 250  # nativeSel child → ownSel mirror / clear window

CHILD_KINDS = frozenset(
    {
        "TASK_LABEL",
        "TASK_PROGRESS",
        "TASK_PCT",
        "MILESTONE_LABEL",
        "MARKER_LABEL",
    }
)


# --- load ------------------------------------------------------------------


def load_meta(session_dir: Path) -> Dict[str, Any]:
    path = session_dir / "meta.json"
    if not path.is_file():
        raise FileNotFoundError(f"missing meta.json in {session_dir}")
    return json.loads(path.read_text(encoding="utf-8"))


def load_events(session_dir: Path) -> List[Dict[str, Any]]:
    path = session_dir / "events.jsonl"
    if not path.is_file():
        raise FileNotFoundError(f"missing events.jsonl in {session_dir}")
    events: List[Dict[str, Any]] = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            ev = json.loads(line)
        except json.JSONDecodeError as e:
            raise ValueError(f"events.jsonl line {line_no}: {e}") from e
        if not isinstance(ev, dict) or "type" not in ev:
            raise ValueError(f"events.jsonl line {line_no}: expected object with type")
        events.append(ev)
    events.sort(key=lambda e: (e.get("t", 0), e.get("seq", 0)))
    return events


# --- helpers ---------------------------------------------------------------


def _entity_key(ent: Dict[str, Any]) -> Tuple[str, str]:
    return (str(ent.get("id", "")), str(ent.get("kind", "")))


def _fmt_t(t: Any) -> str:
    try:
        return f"{int(t)}ms"
    except (TypeError, ValueError):
        return f"{t}ms"


def _brief(obj: Any, max_len: int = 120) -> str:
    s = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
    if len(s) > max_len:
        return s[: max_len - 3] + "..."
    return s


def _is_child_kind(k: str) -> bool:
    return bool(k) and k not in ("", "CHART_ROOT")


def _slide_h(ent: Dict[str, Any]) -> Optional[float]:
    rect = ent.get("slideRect") or ent.get("screenRect")
    if isinstance(rect, (list, tuple)) and len(rect) >= 4:
        try:
            return float(rect[3])
        except (TypeError, ValueError):
            return None
    return None


def diff_entities(
    before: Sequence[Dict[str, Any]], after: Sequence[Dict[str, Any]]
) -> List[str]:
    """Return human lines for only changed/added/removed entities (by id+kind)."""
    bmap = {_entity_key(e): e for e in before if isinstance(e, dict)}
    amap = {_entity_key(e): e for e in after if isinstance(e, dict)}
    lines: List[str] = []
    for key in sorted(set(bmap) | set(amap), key=lambda k: (k[1], k[0])):
        bid, bkind = key
        bo, ao = bmap.get(key), amap.get(key)
        if bo is None and ao is not None:
            h = _slide_h(ao)
            extra = f" h={h}" if h is not None else ""
            lines.append(f"+ {bkind}/{bid}{_brief_entity_geom(ao)}{extra}")
        elif ao is None and bo is not None:
            lines.append(f"- {bkind}/{bid}{_brief_entity_geom(bo)}")
        elif bo is not None and ao is not None:
            field_diffs = _shallow_diff(bo, ao, path="")
            if field_diffs:
                lines.append(f"~ {bkind}/{bid}: " + "; ".join(field_diffs))
    return lines


def _brief_entity_geom(ent: Dict[str, Any]) -> str:
    sr = ent.get("slideRect")
    if isinstance(sr, (list, tuple)) and len(sr) >= 4:
        return f" slideRect={list(sr)}"
    return ""


def _shallow_diff(a: Any, b: Any, path: str, depth: int = 0) -> List[str]:
    if depth > 4:
        if a != b:
            return [f"{path or 'value'}: {_brief(a, 40)} → {_brief(b, 40)}"]
        return []
    if type(a) is not type(b):
        return [f"{path or 'value'}: {_brief(a, 40)} → {_brief(b, 40)}"]
    if isinstance(a, dict):
        out: List[str] = []
        for k in sorted(set(a) | set(b), key=str):
            p = f"{path}.{k}" if path else str(k)
            if k not in a:
                out.append(f"{p}: <missing> → {_brief(b[k], 40)}")
            elif k not in b:
                out.append(f"{p}: {_brief(a[k], 40)} → <missing>")
            else:
                out.extend(_shallow_diff(a[k], b[k], p, depth + 1))
        return out
    if isinstance(a, list):
        if a != b:
            return [f"{path or 'list'}: {_brief(a, 50)} → {_brief(b, 50)}"]
        return []
    if a != b:
        return [f"{path or 'value'}: {a!r} → {b!r}"]
    return []


def diff_chrome(
    before: Optional[Dict[str, Any]], after: Optional[Dict[str, Any]]
) -> List[str]:
    if not before and not after:
        return []
    before = before or {}
    after = after or {}
    return _shallow_diff(before, after, path="chrome")


# --- report ----------------------------------------------------------------


def build_report(meta: Dict[str, Any], events: List[Dict[str, Any]]) -> str:
    lines: List[str] = []
    lines.append("# Session report")
    lines.append("")
    lines.append("## Meta")
    lines.append("")
    for key in (
        "dllVersion",
        "pptxName",
        "chartId",
        "startTime",
        "screen",
        "recordingReason",
    ):
        if key in meta:
            val = meta[key]
            if isinstance(val, (dict, list)):
                lines.append(f"- **{key}**: `{json.dumps(val, ensure_ascii=False)}`")
            else:
                lines.append(f"- **{key}**: {val}")
    extra = {k: v for k, v in meta.items() if k not in {
        "dllVersion", "pptxName", "chartId", "startTime", "screen", "recordingReason"
    }}
    for k, v in sorted(extra.items()):
        if isinstance(v, (dict, list)):
            lines.append(f"- **{k}**: `{json.dumps(v, ensure_ascii=False)}`")
        else:
            lines.append(f"- **{k}**: {v}")
    lines.append(f"- **eventCount**: {len(events)}")
    lines.append("")

    lines.append("## Timeline")
    lines.append("")
    lines.extend(_timeline_lines(events))
    lines.append("")

    lines.append("## Snapshot / entity diffs")
    lines.append("")
    lines.extend(_snapshot_diff_section(events))
    lines.append("")

    lines.append("## Frames")
    lines.append("")
    frames = [e for e in events if e.get("type") == "frame"]
    if not frames:
        lines.append("_No frame events._")
    else:
        for e in frames:
            f = e.get("file", "?")
            trig = e.get("trigger", "")
            surf = e.get("surface", "")
            lines.append(
                f"- t={_fmt_t(e.get('t'))} seq={e.get('seq')} "
                f"`{f}` surface={surf} trigger={trig}"
            )
    lines.append("")

    lines.append("## Anomalies")
    lines.append("")
    anomalies = collect_anomalies(events)
    if not anomalies:
        lines.append("_None detected._")
    else:
        for a in anomalies:
            lines.append(f"- **{a['kind']}**: {a['detail']}")
    lines.append("")
    return "\n".join(lines)


def _timeline_lines(events: List[Dict[str, Any]]) -> List[str]:
    lines: List[str] = []
    i = 0
    n = len(events)
    while i < n:
        e = events[i]
        t = e.get("type")
        ts = _fmt_t(e.get("t"))
        seq = e.get("seq")

        if t == "input":
            hit = e.get("hit") or {}
            lines.append(
                f"- **{ts}** `input` seq={seq} surface={e.get('surface')} "
                f"msg={e.get('msg')} hit={hit.get('kind')}/{hit.get('id')} "
                f"zone={hit.get('zone')} pt={e.get('pt')}"
            )
            # Group immediate hit-related selection if next events are nativeSel/ownSel
            j = i + 1
            while j < n and events[j].get("type") in ("nativeSel", "ownSel") and (
                events[j].get("t", 0) - e.get("t", 0) <= SUPPRESSION_WINDOW_MS * 2
            ):
                ne = events[j]
                lines.append("  " + _format_sel_line(ne))
                j += 1
            i = j
            continue

        if t == "gesture":
            if e.get("phase") == "start":
                block, end_i = _gesture_lifecycle(events, i)
                lines.extend(block)
                i = end_i + 1
                continue
            lines.append("  " + _format_gesture_line(e, prefix="gesture"))
            i += 1
            continue

        if t == "nativeSel" or t == "ownSel":
            # _format_sel_line already includes "- **{ts}** `type` ..."
            lines.append(_format_sel_line(e))
            i += 1
            continue

        if t == "op":
            lines.append(
                f"- **{ts}** `op` seq={seq} cmd={e.get('cmd')} "
                f"dispatchMs={e.get('dispatchMs')} hr={e.get('hr')} "
                f"phases={e.get('phases')}"
            )
            i += 1
            continue

        if t == "paint":
            lines.append(
                f"- **{ts}** `paint` seq={seq} surface={e.get('surface')} "
                f"count={e.get('count')} tMs={e.get('tMs')}"
            )
            i += 1
            continue

        if t == "snapshot":
            ents = e.get("entities")
            chrome = e.get("chrome") or {}
            entity_label = "deduped" if ents is None else str(len(ents or []))
            lines.append(
                f"- **{ts}** `snapshot` seq={seq} entities={entity_label} "
                f"chrome.ownSel={chrome.get('ownSelKind')}/{chrome.get('ownSelId')} "
                f"chrome.native={chrome.get('nativeSelKind')}"
            )
            i += 1
            continue

        if t == "frame":
            lines.append(
                f"- **{ts}** `frame` seq={seq} file=`{e.get('file')}` "
                f"trigger={e.get('trigger')}"
            )
            i += 1
            continue

        if t == "doc":
            lines.append(
                f"- **{ts}** `doc` seq={seq} taskCount={e.get('taskCount')} "
                f"rowCount={e.get('rowCount')} sig={e.get('docDatesSignature')}"
            )
            i += 1
            continue

        if t == "error":
            lines.append(
                f"- **{ts}** ⚠️ `error` seq={seq} where={e.get('where')} "
                f"hr={e.get('hr')} msg={e.get('msg')}"
            )
            i += 1
            continue

        if t == "note":
            lines.append(f"- **{ts}** `note` seq={seq} {e.get('text')}")
            i += 1
            continue

        # Unknown / forward-compatible
        payload = {k: v for k, v in e.items() if k not in ("t", "seq", "type")}
        lines.append(f"- **{ts}** `{t}` seq={seq} {_brief(payload)}")
        i += 1
    return lines


def _format_sel_line(e: Dict[str, Any]) -> str:
    t = e.get("type")
    ts = _fmt_t(e.get("t"))
    if t == "nativeSel":
        return (
            f"- **{ts}** `nativeSel` kind={e.get('kind')} id={e.get('id')} "
            f"hasChildShapeRange={e.get('hasChildShapeRange')} "
            f"childKind={e.get('childKind')} childId={e.get('childId')} "
            f"resolution={e.get('resolution')}"
        )
    return (
        f"- **{ts}** `ownSel` kind={e.get('kind')} id={e.get('id')} "
        f"reason={e.get('reason')}"
    )


# Flat gesture fields emitted by Overlay RecEmitGesture* (no nested "payload").
_GESTURE_FLAT_KEYS = (
    "rowId",
    "anchor",
    "anchorDay",
    "currentDay",
    "percent",
    "deltaDays",
    "candidateStart",
    "candidateEnd",
    "start",
    "end",
    "startDay",
    "endDay",
    "delta",
    "reason",
)


def _gesture_payload(e: Dict[str, Any]) -> Any:
    """Display payload: nested \"payload\" (legacy) or known flat emitter fields."""
    nested = e.get("payload")
    if isinstance(nested, dict) and nested:
        return nested
    flat = {k: e[k] for k in _GESTURE_FLAT_KEYS if k in e}
    return flat if flat else nested


def _gesture_same_instance(start: Dict[str, Any], e: Dict[str, Any]) -> bool:
    """Match start→update/terminal by gesture instance id \"g\", else (kind,id)."""
    sg = start.get("g")
    eg = e.get("g")
    if sg is not None and eg is not None:
        return sg == eg
    # Legacy recordings without \"g\": match kind + id (empty id only pairs empty).
    if start.get("kind") != e.get("kind"):
        return False
    gid = start.get("id")
    oid = e.get("id")
    if gid == oid:
        return True
    return gid in (None, "") and oid in (None, "")


def _format_gesture_line(e: Dict[str, Any], prefix: str = "gesture") -> str:
    return (
        f"`{prefix}` phase={e.get('phase')} g={e.get('g')} kind={e.get('kind')} "
        f"id={e.get('id')} result={e.get('result')} hr={e.get('hr')} "
        f"payload={_brief(_gesture_payload(e), 80)}"
    )


def _gesture_lifecycle(
    events: List[Dict[str, Any]], start_i: int
) -> Tuple[List[str], int]:
    """Group start..commit/cancel (or open-ended) into timeline block."""
    start = events[start_i]
    kind = start.get("kind")
    gid = start.get("id")
    ginst = start.get("g")
    t0 = start.get("t", 0)
    lines = [
        f"- **{_fmt_t(t0)}** `gesture` **lifecycle** start g={ginst} kind={kind} "
        f"id={gid} payload={_brief(_gesture_payload(start), 80)}"
    ]
    end_i = start_i
    terminal: Optional[Dict[str, Any]] = None
    for j in range(start_i + 1, len(events)):
        e = events[j]
        if e.get("type") != "gesture":
            # allow interleaving paint/input/snapshot inside lifecycle scan
            if e.get("type") in ("paint", "input", "snapshot", "frame", "doc"):
                continue
            if e.get("type") in ("nativeSel", "ownSel", "op", "error", "note"):
                # still search forward for commit of same gesture
                continue
            continue
        phase = e.get("phase")
        if phase == "start":
            # New instance — stop looking for a terminal. Leave end_i at the last
            # event of THIS gesture so the main timeline still emits interleaved
            # ownSel/op/error/etc. and the next start is processed separately.
            break
        if not _gesture_same_instance(start, e):
            continue
        if phase == "update":
            lines.append(
                f"  - {_fmt_t(e.get('t'))} update {_brief(_gesture_payload(e), 80)}"
            )
            end_i = j
        elif phase in ("commit", "cancel"):
            terminal = e
            end_i = j
            break

    if terminal:
        dur = (terminal.get("t", 0) or 0) - (t0 or 0)
        lines.append(
            f"  - {_fmt_t(terminal.get('t'))} **{terminal.get('phase')}** "
            f"g={terminal.get('g')} id={terminal.get('id')} "
            f"duration={dur}ms result={terminal.get('result')} hr={terminal.get('hr')}"
        )
    else:
        lines.append(
            f"  - ⚠️ **open gesture** — no commit/cancel after start "
            f"(g={ginst} kind={kind} id={gid})"
        )
    return lines, max(end_i, start_i)


def _snapshot_diff_section(events: List[Dict[str, Any]]) -> List[str]:
    snaps = [e for e in events if e.get("type") == "snapshot"]
    if not snaps:
        return ["_No snapshot events._"]
    lines: List[str] = []
    lines.append(f"_{len(snaps)} snapshot(s). Diffs between consecutive dumps:_")
    lines.append("")
    prev = snaps[0]
    prev_entities = prev.get("entities") if isinstance(prev.get("entities"), list) else []
    lines.append(
        f"### Snapshot seq={prev.get('seq')} t={_fmt_t(prev.get('t'))} (baseline)"
    )
    lines.append(f"- entities: {len(prev_entities)}")
    chrome = prev.get("chrome") or {}
    if chrome:
        lines.append(f"- chrome: `{_brief(chrome, 200)}`")
    lines.append("")

    for snap in snaps[1:]:
        lines.append(
            f"### Diff → seq={snap.get('seq')} t={_fmt_t(snap.get('t'))}"
        )
        cdiff = diff_chrome(prev.get("chrome"), snap.get("chrome"))
        if cdiff:
            lines.append("**Chrome changes:**")
            for d in cdiff:
                lines.append(f"- {d}")
        else:
            lines.append("_Chrome unchanged._")
        current_entities = snap.get("entities")
        if current_entities is None:
            current_entities = prev_entities
        ediff = diff_entities(prev_entities, current_entities)
        if ediff:
            lines.append("**Entity changes (id+kind):**")
            for d in ediff:
                # highlight 1px-high entities
                if "h=1" in d or "h=1.0" in d or ", 1]" in d or ",1]" in d:
                    lines.append(f"- ⚠️ {d}  ← **1px-high entity (hairline preview signature)**")
                else:
                    lines.append(f"- {d}")
        else:
            lines.append("_Entities unchanged._")
        lines.append("")
        prev = snap
        prev_entities = current_entities
    return lines


# --- anomalies -------------------------------------------------------------


def collect_anomalies(events: List[Dict[str, Any]]) -> List[Dict[str, str]]:
    out: List[Dict[str, str]] = []
    out.extend(_anom_open_gestures(events))
    out.extend(_anom_nativesel_child_no_mirror(events))
    out.extend(_anom_errors(events))
    out.extend(_anom_paint_gaps(events))
    out.extend(_anom_hairline_preview(events))
    return out


def _anom_open_gestures(events: List[Dict[str, Any]]) -> List[Dict[str, str]]:
    starts: List[Dict[str, Any]] = []
    closed: set = set()  # indices of start events that got terminal
    for i, e in enumerate(events):
        if e.get("type") != "gesture":
            continue
        phase = e.get("phase")
        if phase == "start":
            starts.append({"i": i, "ev": e})
        elif phase in ("commit", "cancel"):
            # match last unmatched start of same gesture instance (g, else kind/id)
            for s in reversed(starts):
                if s["i"] in closed:
                    continue
                if _gesture_same_instance(s["ev"], e):
                    closed.add(s["i"])
                    break
    out = []
    for s in starts:
        if s["i"] not in closed:
            se = s["ev"]
            out.append(
                {
                    "kind": "gesture_start_without_commit_or_cancel",
                    "detail": (
                        f"t={_fmt_t(se.get('t'))} seq={se.get('seq')} "
                        f"g={se.get('g')} kind={se.get('kind')} id={se.get('id')} "
                        f"(failure signature: silent create/drag — no commit)"
                    ),
                }
            )
    return out


def _anom_nativesel_child_no_mirror(events: List[Dict[str, Any]]) -> List[Dict[str, str]]:
    out = []
    for i, e in enumerate(events):
        if e.get("type") != "nativeSel":
            continue
        has_child = bool(e.get("hasChildShapeRange"))
        child_kind = e.get("childKind") or ""
        kind = e.get("kind") or ""
        is_child = has_child or child_kind in CHILD_KINDS or (
            kind in CHILD_KINDS
        ) or (_is_child_kind(kind) and kind != "CHART_ROOT" and has_child)
        # Broaden: any nativeSel with hasChildShapeRange true OR child kind selected
        if not (has_child or child_kind in CHILD_KINDS or kind in CHILD_KINDS):
            continue
        t0 = e.get("t", 0) or 0
        mirrored = False
        suppressed = False
        for j in range(i + 1, len(events)):
            ne = events[j]
            if (ne.get("t", 0) or 0) - t0 > SUPPRESSION_WINDOW_MS:
                break
            if ne.get("type") == "ownSel":
                # unit mirror: TASK (or parent unit) selection
                if ne.get("kind") in ("TASK", "MILESTONE", "MARKER", "ROW", "CHART_ROOT"):
                    mirrored = True
            if ne.get("type") == "nativeSel":
                if not ne.get("hasChildShapeRange") and not _is_child_kind(
                    str(ne.get("kind") or "")
                ):
                    suppressed = True
        if not mirrored and not suppressed:
            out.append(
                {
                    "kind": "nativeSel_child_without_suppression_or_ownSel_mirror",
                    "detail": (
                        f"t={_fmt_t(e.get('t'))} seq={e.get('seq')} "
                        f"kind={kind} id={e.get('id')} "
                        f"hasChildShapeRange={has_child} childKind={child_kind} "
                        f"childId={e.get('childId')} resolution={e.get('resolution')} "
                        f"(failure signature: task-bar click selected label child)"
                    ),
                }
            )
    return out


def _anom_errors(events: List[Dict[str, Any]]) -> List[Dict[str, str]]:
    out = []
    for e in events:
        if e.get("type") == "error":
            out.append(
                {
                    "kind": "error_event",
                    "detail": (
                        f"t={_fmt_t(e.get('t'))} seq={e.get('seq')} "
                        f"where={e.get('where')} hr={e.get('hr')} msg={e.get('msg')}"
                    ),
                }
            )
    return out


def _active_gesture_intervals(
    events: List[Dict[str, Any]],
) -> List[Tuple[float, float]]:
    """Return [start_t, end_t] for each gesture (end = commit/cancel t or +inf)."""
    intervals: List[Tuple[float, float]] = []
    open_starts: List[Dict[str, Any]] = []
    for e in events:
        if e.get("type") != "gesture":
            continue
        phase = e.get("phase")
        t = float(e.get("t", 0) or 0)
        if phase == "start":
            open_starts.append(e)
        elif phase in ("commit", "cancel"):
            for k in range(len(open_starts) - 1, -1, -1):
                s = open_starts[k]
                if _gesture_same_instance(s, e):
                    intervals.append((float(s.get("t", 0) or 0), t))
                    open_starts.pop(k)
                    break
    # still open
    for s in open_starts:
        intervals.append((float(s.get("t", 0) or 0), float("inf")))
    return intervals


def _t_in_intervals(t: float, intervals: Sequence[Tuple[float, float]]) -> bool:
    for a, b in intervals:
        if a <= t <= b:
            return True
    return False


def _anom_paint_gaps(events: List[Dict[str, Any]]) -> List[Dict[str, str]]:
    intervals = _active_gesture_intervals(events)
    if not intervals:
        return []
    paints = [e for e in events if e.get("type") == "paint"]
    out = []
    for a, b in zip(paints, paints[1:]):
        t0 = float(a.get("t", 0) or 0)
        t1 = float(b.get("t", 0) or 0)
        gap = t1 - t0
        if gap <= PAINT_GAP_MS:
            continue
        # gap counts if either endpoint or the span overlaps an active gesture
        mid = (t0 + t1) / 2
        if (
            _t_in_intervals(t0, intervals)
            or _t_in_intervals(t1, intervals)
            or _t_in_intervals(mid, intervals)
        ):
            out.append(
                {
                    "kind": "paint_gap_during_gesture",
                    "detail": (
                        f"gap={gap:.0f}ms between paint seq={a.get('seq')}@"
                        f"{_fmt_t(t0)} and seq={b.get('seq')}@{_fmt_t(t1)} "
                        f"(threshold {PAINT_GAP_MS}ms)"
                    ),
                }
            )
    return out


def _anom_hairline_preview(events: List[Dict[str, Any]]) -> List[Dict[str, str]]:
    """Surface 1px-high TASK (or similar) entities in snapshots — failure #3."""
    out = []
    for e in events:
        if e.get("type") != "snapshot":
            continue
        for ent in e.get("entities") or []:
            if not isinstance(ent, dict):
                continue
            h = _slide_h(ent)
            if h is None:
                continue
            if h <= 1.5 and str(ent.get("kind", "")) in (
                "TASK",
                "TASK_PROGRESS",
                "CREATE_PREVIEW",
            ):
                out.append(
                    {
                        "kind": "hairline_preview_entity",
                        "detail": (
                            f"t={_fmt_t(e.get('t'))} seq={e.get('seq')} "
                            f"entity {ent.get('kind')}/{ent.get('id')} "
                            f"height={h}px slideRect={ent.get('slideRect')} "
                            f"(failure signature: 1px create preview)"
                        ),
                    }
                )
    return out


# --- assert / invariants ---------------------------------------------------


def evaluate_invariants(
    events: List[Dict[str, Any]], rule_names: Sequence[str]
) -> List[Dict[str, Any]]:
    results = []
    for name in rule_names:
        fn = INVARIANT_RULES.get(name)
        if fn is None:
            results.append(
                {
                    "rule": name,
                    "passed": False,
                    "detail": f"unknown rule {name!r} (not implemented for session assert)",
                }
            )
        else:
            results.append(fn(events))
    return results


def _rule_task_label_selects_task_unit(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    """After native child TASK_LABEL selection, ownSel must mirror unit TASK and
    native child must be suppressed — same spirit as harness task_label_selects_task_unit.
    """
    label_natives = [
        (i, e)
        for i, e in enumerate(events)
        if e.get("type") == "nativeSel"
        and (
            e.get("childKind") == "TASK_LABEL"
            or e.get("kind") == "TASK_LABEL"
            or (e.get("hasChildShapeRange") and e.get("childKind") == "TASK_LABEL")
        )
    ]
    if not label_natives:
        # Also consider input hit on TASK_LABEL
        label_inputs = [
            (i, e)
            for i, e in enumerate(events)
            if e.get("type") == "input"
            and (e.get("hit") or {}).get("kind") == "TASK_LABEL"
        ]
        if not label_inputs:
            return {
                "rule": "task_label_selects_task_unit",
                "passed": False,
                "detail": "no TASK_LABEL nativeSel/input in session; required evidence missing",
            }
        # evaluate via ownSel after label input
        bad = []
        for i, e in label_inputs:
            want_id = (e.get("hit") or {}).get("id")
            ok = _own_sel_task_unit_after(events, i, want_id)
            if not ok:
                bad.append(f"seq={e.get('seq')} id={want_id}")
        return {
            "rule": "task_label_selects_task_unit",
            "passed": not bad,
            "detail": (
                "all label inputs mirrored to ownSel TASK"
                if not bad
                else f"label input(s) without TASK unit ownSel: {bad}"
            ),
        }

    bad = []
    for i, e in label_natives:
        want_id = e.get("childId") or e.get("id")
        mirrored = _own_sel_task_unit_after(events, i, want_id)
        suppressed = _native_child_suppressed_after(events, i)
        if not (mirrored and suppressed):
            bad.append(
                f"seq={e.get('seq')} kind={e.get('kind')} child={e.get('childKind')} "
                f"mirrored={mirrored} suppressed={suppressed}"
            )
    return {
        "rule": "task_label_selects_task_unit",
        "passed": not bad,
        "detail": (
            "label nativeSel mirrored to ownSel TASK and child suppressed"
            if not bad
            else (
                f"TASK_LABEL nativeSel without unit mirror/suppression: {bad} "
                f"(want ownSel TASK + native ''/CHART_ROOT)"
            )
        ),
    }


def _own_sel_task_unit_after(
    events: List[Dict[str, Any]], start_i: int, want_id: Optional[str]
) -> bool:
    t0 = events[start_i].get("t", 0) or 0
    for j in range(start_i + 1, len(events)):
        e = events[j]
        if (e.get("t", 0) or 0) - t0 > SUPPRESSION_WINDOW_MS:
            break
        if e.get("type") == "ownSel" and e.get("kind") == "TASK":
            if want_id in (None, "", e.get("id")):
                return True
    return False


def _native_child_suppressed_after(events: List[Dict[str, Any]], start_i: int) -> bool:
    t0 = events[start_i].get("t", 0) or 0
    for j in range(start_i + 1, len(events)):
        e = events[j]
        if (e.get("t", 0) or 0) - t0 > SUPPRESSION_WINDOW_MS:
            break
        if e.get("type") == "nativeSel":
            kind = str(e.get("kind") or "")
            if not e.get("hasChildShapeRange") and not _is_child_kind(kind):
                return True
            if kind in ("", "CHART_ROOT") and not e.get("hasChildShapeRange"):
                return True
    return False


def _rule_task_body_selects_same_unit(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    """Body click (hit kind TASK) must yield ownSel TASK same id (harness-style)."""
    body_inputs = [
        (i, e)
        for i, e in enumerate(events)
        if e.get("type") == "input"
        and (e.get("hit") or {}).get("kind") == "TASK"
        and (e.get("msg") or "") in ("WM_LBUTTONDOWN", "WM_LBUTTONUP", "WM_LBUTTONDBLCLK")
    ]
    # Prefer DOWN only to avoid double-count
    downs = [
        (i, e)
        for i, e in body_inputs
        if (e.get("msg") or "") == "WM_LBUTTONDOWN"
    ]
    samples = downs or body_inputs
    if not samples:
        return {
            "rule": "task_body_selects_same_unit",
            "passed": False,
            "detail": "no TASK body input in session; required evidence missing",
        }
    bad = []
    for i, e in samples:
        want_id = (e.get("hit") or {}).get("id")
        if not _own_sel_task_unit_after(events, i, want_id):
            # also fail if following nativeSel is a child (label) without unit
            bad.append(f"seq={e.get('seq')} hitId={want_id}")
    # Special-case fixture signature: body click followed by TASK_LABEL nativeSel
    for i, e in samples:
        t0 = e.get("t", 0) or 0
        for j in range(i + 1, len(events)):
            ne = events[j]
            if (ne.get("t", 0) or 0) - t0 > SUPPRESSION_WINDOW_MS * 2:
                break
            if ne.get("type") == "nativeSel" and (
                ne.get("childKind") == "TASK_LABEL" or ne.get("kind") == "TASK_LABEL"
            ):
                if not _own_sel_task_unit_after(events, j, ne.get("childId") or ne.get("id")):
                    if f"seq={e.get('seq')} hitId={(e.get('hit') or {}).get('id')}" not in bad:
                        bad.append(
                            f"seq={e.get('seq')} body→nativeSel child "
                            f"{ne.get('childKind')}/{ne.get('childId')}"
                        )
    return {
        "rule": "task_body_selects_same_unit",
        "passed": not bad,
        "detail": (
            "all TASK body clicks selected ownSel TASK same id"
            if not bad
            else f"body click(s) without same-unit ownSel: {bad}"
        ),
    }


def _rule_gesture_commits_or_cancels(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    open_anom = _anom_open_gestures(events)
    return {
        "rule": "gesture_commits_or_cancels",
        "passed": len(open_anom) == 0,
        "detail": (
            "all gesture starts have commit or cancel"
            if not open_anom
            else "; ".join(a["detail"] for a in open_anom)
        ),
    }


def _rule_no_silent_errors(events: List[Dict[str, Any]]) -> Dict[str, Any]:
    """Fail if the session recorded error events (explicit failures surfaced).

    Note: silent failures without error events are caught by other rules
    (open gestures, nativeSel child). This rule flags any `error` type event.
    """
    errs = [e for e in events if e.get("type") == "error"]
    if not errs:
        return {
            "rule": "no_silent_errors",
            "passed": True,
            "detail": "no error events in session",
        }
    details = [
        f"seq={e.get('seq')} where={e.get('where')} msg={e.get('msg')}" for e in errs
    ]
    return {
        "rule": "no_silent_errors",
        "passed": False,
        "detail": f"{len(errs)} error event(s): " + "; ".join(details),
    }


INVARIANT_RULES = {
    "task_label_selects_task_unit": _rule_task_label_selects_task_unit,
    "task_body_selects_same_unit": _rule_task_body_selects_same_unit,
    "gesture_commits_or_cancels": _rule_gesture_commits_or_cancels,
    "no_silent_errors": _rule_no_silent_errors,
}


# --- CLI -------------------------------------------------------------------


def write_report(session_dir: Path, out_path: Optional[Path] = None) -> Path:
    meta = load_meta(session_dir)
    events = load_events(session_dir)
    md = build_report(meta, events)
    dest = out_path if out_path is not None else session_dir / "report.md"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(md, encoding="utf-8")
    return dest


def run_assert(session_dir: Path, scenario_path: Path) -> Tuple[int, List[Dict[str, Any]]]:
    scenario = json.loads(scenario_path.read_text(encoding="utf-8"))
    rules = scenario.get("invariants") or scenario.get("rules") or []
    if not rules:
        print(
            json.dumps(
                {
                    "status": "ERROR",
                    "detail": "scenario has no invariants/rules list",
                    "results": [],
                },
                indent=2,
            )
        )
        return 1, []
    events = load_events(session_dir)
    results = evaluate_invariants(events, rules)
    all_pass = all(r.get("passed") for r in results)
    summary = {
        "status": "PASS" if all_pass else "FAIL",
        "session": str(session_dir),
        "scenario": scenario.get("name", scenario_path.name),
        "results": results,
    }
    print(json.dumps(summary, indent=2))
    return (0 if all_pass else 1), results


def run_selftest() -> int:
    """Exercise fixture: report surfaces three 2026-07-18 failures; assert outcomes."""
    if not FIXTURE_DIR.is_dir():
        print(f"SELFTEST FAIL: fixture missing at {FIXTURE_DIR}", file=sys.stderr)
        return 1
    errors: List[str] = []

    # 1) Build report
    try:
        report_path = write_report(FIXTURE_DIR, FIXTURE_DIR / "report.md")
        report = report_path.read_text(encoding="utf-8")
    except Exception as e:
        print(f"SELFTEST FAIL: report build: {e}", file=sys.stderr)
        return 1

    # Event types from spec table must all appear in timeline
    required_types = [
        "input",
        "nativeSel",
        "ownSel",
        "gesture",
        "op",
        "paint",
        "snapshot",
        "frame",
        "doc",
        "error",
        "note",
    ]
    for t in required_types:
        if f"`{t}`" not in report:
            errors.append(f"report missing event type coverage for `{t}`")

    # Failure signature (a): nativeSel child TASK_LABEL without mirror
    if "nativeSel_child_without_suppression_or_ownSel_mirror" not in report:
        errors.append("anomaly section missing nativeSel child-without-mirror (failure #1)")
    if "TASK_LABEL" not in report or "hasChildShapeRange" not in report:
        errors.append("report does not surface nativeSel hasChildShapeRange/TASK_LABEL")

    # Failure signature (b): gesture start Create without commit (silent create)
    if "gesture_start_without_commit_or_cancel" not in report:
        errors.append("anomaly section missing open create gesture (failure #2)")
    if "open gesture" not in report:
        errors.append("timeline missing open create gesture lifecycle")
    if "kind=Create" not in report and "kind=create" not in report:
        errors.append("timeline/anomalies missing Create gesture kind")

    # Failure signature (c): 1px-high preview entity
    if "hairline_preview_entity" not in report and "1px-high" not in report:
        errors.append("report missing 1px-high / hairline preview entity (failure #3)")
    if "preview-create" not in report and "h=1" not in report:
        # entity diff should show the added preview
        if "1px" not in report.lower() and "height=1" not in report:
            errors.append("entity diffs do not show 1px preview-create entity")

    # Error events highlighted
    if "error_event" not in report and "⚠️ `error`" not in report:
        errors.append("error events not highlighted in report")

    # Frames referenced
    if "frames/" not in report:
        errors.append("frame references missing")

    # Deduped snapshots carry forward the prior entity body; null must never
    # render as a full-scene removal.
    dedupe_probe = _snapshot_diff_section([
        {"type": "snapshot", "seq": 1, "t": 0, "chrome": {},
         "entities": [{"id": "t1", "kind": "TASK", "slideRect": [0, 0, 10, 4]}]},
        {"type": "snapshot", "seq": 2, "t": 10, "chrome": {}, "entities": None},
    ])
    dedupe_text = "\n".join(dedupe_probe)
    if "- TASK/t1" in dedupe_text or "Entities unchanged" not in dedupe_text:
        errors.append("deduped snapshot incorrectly treated as an empty entity list")

    # 2) Assert mode expected outcomes (fixture is failure-oriented → all four FAIL)
    scenario = FIXTURE_DIR / "assert-scenario.json"
    events = load_events(FIXTURE_DIR)
    rules = json.loads(scenario.read_text(encoding="utf-8"))["invariants"]
    results = evaluate_invariants(events, rules)
    by_rule = {r["rule"]: r for r in results}

    expected_fail = [
        "task_label_selects_task_unit",
        "task_body_selects_same_unit",
        "gesture_commits_or_cancels",
        "no_silent_errors",
    ]
    for rule in expected_fail:
        r = by_rule.get(rule)
        if r is None:
            errors.append(f"assert missing result for {rule}")
        elif r.get("passed"):
            errors.append(
                f"assert expected FAIL for {rule} on failure fixture, got PASS: {r.get('detail')}"
            )

    # 3) Lifecycle matching on \"g\": healthy Create (g=3) + WindowEdge (g=4)
    # must close; only the genuine silent Create (g=1) remains open.
    open_anoms = [
        a
        for a in collect_anomalies(events)
        if a.get("kind") == "gesture_start_without_commit_or_cancel"
    ]
    create_opens = [a for a in open_anoms if "kind=Create" in a.get("detail", "")]
    if len(create_opens) != 1:
        errors.append(
            f"expected exactly 1 open Create (silent), got {len(create_opens)}: "
            f"{[a.get('detail') for a in create_opens]}"
        )
    if not any("g=1" in a.get("detail", "") for a in create_opens):
        errors.append(
            "silent Create (g=1) not flagged as open; open details: "
            f"{[a.get('detail') for a in create_opens]}"
        )
    if any("g=3" in a.get("detail", "") for a in open_anoms):
        errors.append(
            "healthy create g=3 incorrectly flagged as open gesture "
            "(start id=\"\" vs commit id=newTaskHealthy must match via g)"
        )
    if any("g=4" in a.get("detail", "") or "WindowEdge" in a.get("detail", "")
           for a in open_anoms):
        errors.append(
            "window-edge drag incorrectly flagged as open gesture "
            "(start kind WindowEdgeL must match commit WindowEdge via g)"
        )
    # Healthy create commit + window edge must appear closed in the timeline.
    if "newTaskHealthy" not in report:
        errors.append("timeline missing healthy create commit id=newTaskHealthy")
    if "WindowEdgeL" not in report:
        errors.append("timeline missing WindowEdgeL start")
    if "**commit**" not in report:
        errors.append("timeline missing gesture commit phases")

    if errors:
        print("SELFTEST FAIL:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        print("\n--- report excerpt (Anomalies) ---", file=sys.stderr)
        # print anomalies section
        if "## Anomalies" in report:
            print(report.split("## Anomalies", 1)[1][:2000], file=sys.stderr)
        print("\n--- assert results ---", file=sys.stderr)
        print(json.dumps(results, indent=2), file=sys.stderr)
        return 1

    print("SELFTEST PASS")
    print(f"  report: {report_path}")
    print(f"  anomalies surfaced: failure #1 nativeSel child, #2 open create, #3 hairline")
    print(f"  healthy Create + WindowEdge closed via g= (not false open)")
    print(f"  assert rules all FAIL as expected on fixture:")
    for r in results:
        print(f"    {r['rule']}: passed={r['passed']}")
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(
        description="PowerPlanner session recorder report (R2)"
    )
    p.add_argument(
        "session_dir",
        nargs="?",
        default=None,
        help="Path to session directory (events.jsonl + meta.json)",
    )
    p.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Write markdown report here (default: <session-dir>/report.md)",
    )
    p.add_argument(
        "--assert",
        dest="assert_scenario",
        type=Path,
        default=None,
        help="Evaluate invariant names from scenario JSON; exit 0/1",
    )
    p.add_argument(
        "--selftest",
        action="store_true",
        help="Run built-in checks against testdata/session-fixture",
    )
    args = p.parse_args(argv)

    if args.selftest:
        return run_selftest()

    if not args.session_dir:
        p.error("session_dir is required unless --selftest")

    session_dir = Path(args.session_dir)
    if not session_dir.is_dir():
        print(f"ERROR: not a directory: {session_dir}", file=sys.stderr)
        return 1

    if args.assert_scenario:
        code, _ = run_assert(session_dir, Path(args.assert_scenario))
        # Still write report for agent convenience unless --out suppressed? always write
        try:
            dest = write_report(session_dir, args.out)
            print(f"report written: {dest}", file=sys.stderr)
        except Exception as e:
            print(f"warning: could not write report: {e}", file=sys.stderr)
        return code

    dest = write_report(session_dir, args.out)
    print(dest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
