"""Safe discovery and management for PowerPlanner recorder sessions."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterable


REPO_ROOT = Path(__file__).resolve().parents[2]
PROJECT_RECORDS_ROOT = REPO_ROOT / "native" / "records"
LEGACY_RECORDS_ROOT = Path(tempfile.gettempdir()) / "powerplanner-sessions"
SESSION_ID_RE = re.compile(r"^\d{8}-\d{6}(?:-\d{3}-\d+)?$")


def records_root() -> Path:
    override = __import__("os").environ.get("POWERPLANNER_RECORDS_DIR")
    return Path(override).expanduser().resolve() if override else PROJECT_RECORDS_ROOT


def approved_roots(
    project_root: Path | None = None, legacy_root: Path | None = None
) -> list[tuple[str, Path]]:
    return [
        ("project", (project_root or records_root()).resolve()),
        ("legacy", (legacy_root or LEGACY_RECORDS_ROOT).resolve()),
    ]


def _contained(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}


def _event_stats(path: Path) -> tuple[int, int, dict[str, int]]:
    count = 0
    duration_ms = 0
    types: dict[str, int] = {}
    try:
        with path.open("r", encoding="utf-8") as stream:
            for line in stream:
                if not line.strip():
                    continue
                event = json.loads(line)
                count += 1
                duration_ms = max(duration_ms, int(event.get("t", 0)))
                event_type = str(event.get("type", "unknown"))
                types[event_type] = types.get(event_type, 0) + 1
    except (OSError, ValueError, TypeError):
        pass
    return count, duration_ms, types


def discover_sessions(
    include_legacy: bool = True,
    project_root: Path | None = None,
    legacy_root: Path | None = None,
) -> list[dict[str, Any]]:
    roots = approved_roots(project_root, legacy_root)
    if not include_legacy:
        roots = roots[:1]
    sessions: list[dict[str, Any]] = []
    for source, root in roots:
        if not root.is_dir():
            continue
        for path in root.iterdir():
            if (
                not path.is_dir()
                or not SESSION_ID_RE.fullmatch(path.name)
                or not (path / "events.jsonl").is_file()
                or not (path / "meta.json").is_file()
            ):
                continue
            meta = _read_json(path / "meta.json")
            event_count, duration_ms, event_types = _event_stats(path / "events.jsonl")
            frame_count = sum(1 for item in (path / "frames").glob("*.png"))
            sessions.append(
                {
                    "id": path.name,
                    "source": source,
                    "path": str(path.resolve()),
                    "active": (path / ".active").exists(),
                    "startTime": meta.get("startTime", ""),
                    "pptxName": meta.get("pptxName", ""),
                    "chartId": meta.get("chartId", ""),
                    "dllVersion": meta.get("dllVersion", ""),
                    "eventCount": event_count,
                    "durationMs": duration_ms,
                    "eventTypes": event_types,
                    "frameCount": frame_count,
                    "hasReport": (path / "report.md").is_file(),
                }
            )
    sessions.sort(key=lambda item: (item["startTime"], item["id"]), reverse=True)
    return sessions


def find_session(
    session_id: str,
    include_legacy: bool = True,
    project_root: Path | None = None,
    legacy_root: Path | None = None,
) -> dict[str, Any]:
    if not SESSION_ID_RE.fullmatch(session_id):
        raise ValueError("session_id must be an exact recorder session id")
    matches = [
        item
        for item in discover_sessions(include_legacy, project_root, legacy_root)
        if item["id"] == session_id
    ]
    if not matches:
        raise FileNotFoundError(f"recording not found: {session_id}")
    return matches[0]


def read_events(
    session_id: str,
    *,
    after_seq: int = 0,
    limit: int = 200,
    event_types: Iterable[str] | None = None,
    project_root: Path | None = None,
    legacy_root: Path | None = None,
) -> dict[str, Any]:
    session = find_session(session_id, True, project_root, legacy_root)
    wanted = set(event_types or [])
    events: list[dict[str, Any]] = []
    with (Path(session["path"]) / "events.jsonl").open("r", encoding="utf-8") as stream:
        for line in stream:
            if not line.strip():
                continue
            event = json.loads(line)
            if int(event.get("seq", 0)) <= after_seq:
                continue
            if wanted and event.get("type") not in wanted:
                continue
            events.append(event)
            if len(events) >= max(1, min(limit, 2000)):
                break
    return {"session": session, "events": events}


def _run_session_report_cli(session_path: Path, report_path: Path) -> None:
    """Invoke session_report.py as a subprocess (CLI fallback).

    Must not inherit the parent's stdin. FastMCP stdio owns stdin as a pipe
    that anyio continuously reads; a child that inherits that handle deadlocks
    on Windows until the subprocess timeout (observed: generate_recording_report
    hung 60s while the same argv finished in <1s outside MCP).
    """
    script = Path(__file__).with_name("session_report.py")
    # sys.executable: same interpreter as the MCP/host process (never a
    # hardcoded path that may be a Store stub or different env).
    # capture_output=True → stdout+stderr PIPEd and drained via communicate().
    # stdin=DEVNULL → detach from MCP protocol pipe (the hang root cause).
    result = subprocess.run(
        [sys.executable, str(script), str(session_path), "--out", str(report_path)],
        text=True,
        capture_output=True,
        stdin=subprocess.DEVNULL,
        timeout=60,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "session_report failed")


def generate_report(
    session_id: str,
    project_root: Path | None = None,
    legacy_root: Path | None = None,
    *,
    use_subprocess: bool = False,
) -> dict[str, Any]:
    session = find_session(session_id, True, project_root, legacy_root)
    session_path = Path(session["path"])
    report_path = session_path / "report.md"
    if use_subprocess:
        _run_session_report_cli(session_path, report_path)
    else:
        # In-process: no stdio inheritance, no pipe buffers, no 60s backstop needed
        # for the normal path. session_report is pure stdlib next to this module.
        try:
            import session_report as report_mod
        except ImportError:
            _run_session_report_cli(session_path, report_path)
        else:
            report_mod.write_report(session_path, report_path)
    if not report_path.is_file():
        raise RuntimeError(f"report was not written: {report_path}")
    return {"session": session_id, "reportPath": str(report_path), "generated": True}


def read_report(
    session_id: str,
    *,
    generate_if_missing: bool = True,
    project_root: Path | None = None,
    legacy_root: Path | None = None,
) -> dict[str, Any]:
    session = find_session(session_id, True, project_root, legacy_root)
    report_path = Path(session["path"]) / "report.md"
    generated = False
    if not report_path.is_file() and generate_if_missing:
        generate_report(session_id, project_root, legacy_root)
        generated = True
    if not report_path.is_file():
        raise FileNotFoundError(f"report not found for {session_id}")
    return {
        "session": session,
        "reportPath": str(report_path),
        "generated": generated,
        "markdown": report_path.read_text(encoding="utf-8"),
    }


def delete_session(
    session_id: str,
    confirmation: str,
    *,
    project_root: Path | None = None,
    legacy_root: Path | None = None,
) -> dict[str, Any]:
    if confirmation != session_id:
        raise ValueError("confirmation must exactly match session_id")
    session = find_session(session_id, True, project_root, legacy_root)
    if session["active"]:
        raise RuntimeError("cannot delete an active recording")
    target = Path(session["path"]).resolve()
    roots = dict(approved_roots(project_root, legacy_root))
    root = roots[session["source"]]
    if target == root or not _contained(target, root):
        raise RuntimeError("refusing deletion outside the approved records root")
    shutil.rmtree(target)
    return {"deleted": True, "id": session_id, "path": str(target)}


def _print(value: Any) -> None:
    print(json.dumps(value, indent=2, ensure_ascii=True))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    list_parser = sub.add_parser("list")
    list_parser.add_argument("--no-legacy", action="store_true")
    show_parser = sub.add_parser("show")
    show_parser.add_argument("session_id")
    events_parser = sub.add_parser("events")
    events_parser.add_argument("session_id")
    events_parser.add_argument("--after-seq", type=int, default=0)
    events_parser.add_argument("--limit", type=int, default=200)
    events_parser.add_argument("--type", action="append", dest="types")
    report_parser = sub.add_parser("report")
    report_parser.add_argument("session_id")
    delete_parser = sub.add_parser("delete")
    delete_parser.add_argument("session_id")
    delete_parser.add_argument("--confirm", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "list":
            _print(discover_sessions(not args.no_legacy))
        elif args.command == "show":
            _print(find_session(args.session_id))
        elif args.command == "events":
            _print(read_events(args.session_id, after_seq=args.after_seq, limit=args.limit, event_types=args.types))
        elif args.command == "report":
            _print(read_report(args.session_id))
        elif args.command == "delete":
            _print(delete_session(args.session_id, args.confirm))
        return 0
    except (FileNotFoundError, RuntimeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
