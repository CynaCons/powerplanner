"""PowerPlanner recorder MCP server (stdio)."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

_TOOLS = Path(__file__).resolve().parent
if str(_TOOLS) not in sys.path:
    sys.path.insert(0, str(_TOOLS))

from mcp.server.fastmcp import FastMCP

import session_manager as manager


mcp = FastMCP("powerplanner-recorder")


@mcp.tool()
def list_recordings(include_legacy: bool = True) -> list[dict[str, Any]]:
    """List project recordings and, optionally, legacy temporary recordings."""
    return manager.discover_sessions(include_legacy=include_legacy)


@mcp.tool()
def get_recording(session_id: str) -> dict[str, Any]:
    """Get metadata and counts for one exact recording id."""
    return manager.find_session(session_id)


@mcp.tool()
def get_recording_events(
    session_id: str,
    after_seq: int = 0,
    limit: int = 200,
    event_types: list[str] | None = None,
) -> dict[str, Any]:
    """Read a bounded page of structured events from a recording."""
    return manager.read_events(
        session_id,
        after_seq=after_seq,
        limit=limit,
        event_types=event_types,
    )


@mcp.tool()
def get_recording_report(
    session_id: str, generate_if_missing: bool = True
) -> dict[str, Any]:
    """Read the Markdown analysis report, optionally generating it first."""
    return manager.read_report(session_id, generate_if_missing=generate_if_missing)


@mcp.tool()
def generate_recording_report(session_id: str) -> dict[str, Any]:
    """Generate or refresh report.md for one recording."""
    return manager.generate_report(session_id)


@mcp.tool()
def delete_recording(session_id: str, confirmation: str) -> dict[str, Any]:
    """Delete a contained, inactive recording; confirmation must equal its id."""
    return manager.delete_session(session_id, confirmation)


if __name__ == "__main__":
    mcp.run(transport="stdio")
