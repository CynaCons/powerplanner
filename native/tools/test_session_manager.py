from __future__ import annotations

import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import session_manager as manager


def make_session(root: Path, session_id: str, active: bool = False) -> Path:
    path = root / session_id
    (path / "frames").mkdir(parents=True)
    (path / "meta.json").write_text(
        json.dumps({"startTime": "2026-07-18T14:25:22Z", "pptxName": "Test.pptx"}),
        encoding="utf-8",
    )
    (path / "events.jsonl").write_text(
        '{"t":0,"seq":1,"type":"doc"}\n'
        '{"t":150,"seq":2,"type":"snapshot"}\n',
        encoding="utf-8",
    )
    if active:
        (path / ".active").write_text("active\n", encoding="ascii")
    return path


class SessionManagerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        base = Path(self.temp.name)
        self.project = base / "project"
        self.legacy = base / "legacy"
        self.project.mkdir()
        self.legacy.mkdir()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_discovers_project_and_legacy(self) -> None:
        make_session(self.project, "20260718-162522-519-14476")
        make_session(self.legacy, "20260718-154259")
        sessions = manager.discover_sessions(True, self.project, self.legacy)
        self.assertEqual({s["source"] for s in sessions}, {"project", "legacy"})
        self.assertEqual(sum(s["eventCount"] for s in sessions), 4)

    def test_records_root_honors_environment_override(self) -> None:
        override = Path(self.temp.name) / "override"
        with patch.dict(os.environ, {"POWERPLANNER_RECORDS_DIR": str(override)}):
            self.assertEqual(manager.records_root(), override.resolve())

    def test_cli_json_is_windows_console_safe(self) -> None:
        output = io.StringIO()
        with patch("sys.stdout", output):
            manager._print({"text": "input\u2192selection"})
        self.assertIn("\\u2192", output.getvalue())

    def test_delete_requires_exact_confirmation(self) -> None:
        session_id = "20260718-162522-519-14476"
        path = make_session(self.project, session_id)
        with self.assertRaises(ValueError):
            manager.delete_session(session_id, "wrong", project_root=self.project, legacy_root=self.legacy)
        self.assertTrue(path.exists())

    def test_delete_rejects_active_session(self) -> None:
        session_id = "20260718-162522-519-14476"
        path = make_session(self.project, session_id, active=True)
        with self.assertRaises(RuntimeError):
            manager.delete_session(session_id, session_id, project_root=self.project, legacy_root=self.legacy)
        self.assertTrue(path.exists())

    def test_delete_removes_only_discovered_contained_session(self) -> None:
        session_id = "20260718-162522-519-14476"
        path = make_session(self.project, session_id)
        result = manager.delete_session(
            session_id, session_id, project_root=self.project, legacy_root=self.legacy
        )
        self.assertTrue(result["deleted"])
        self.assertFalse(path.exists())
        with self.assertRaises(ValueError):
            manager.find_session("../outside", project_root=self.project, legacy_root=self.legacy)

    def test_generate_report_subprocess_detaches_stdin(self) -> None:
        """MCP hang regression: child must not inherit the host stdin pipe."""
        session_id = "20260718-162522-519-14476"
        path = make_session(self.project, session_id)
        # Minimal fixture for session_report.write_report path via CLI
        (path / "meta.json").write_text(
            json.dumps(
                {
                    "startTime": "2026-07-18T14:25:22Z",
                    "pptxName": "Test.pptx",
                    "chartId": "c1",
                    "dllVersion": "test",
                }
            ),
            encoding="utf-8",
        )

        captured: dict = {}

        def fake_run(cmd, **kwargs):
            captured["cmd"] = cmd
            captured["kwargs"] = kwargs

            class R:
                returncode = 0
                stdout = str(path / "report.md")
                stderr = ""

            (path / "report.md").write_text("# ok\n", encoding="utf-8")
            return R()

        with patch("session_manager.subprocess.run", side_effect=fake_run):
            result = manager.generate_report(
                session_id,
                project_root=self.project,
                legacy_root=self.legacy,
                use_subprocess=True,
            )
        self.assertTrue(result["generated"])
        self.assertIs(captured["kwargs"].get("stdin"), manager.subprocess.DEVNULL)
        self.assertTrue(captured["kwargs"].get("capture_output"))
        self.assertEqual(captured["cmd"][0], sys.executable)

    def test_generate_report_inprocess_writes_file(self) -> None:
        session_id = "20260718-162522-519-14476"
        path = make_session(self.project, session_id)
        (path / "meta.json").write_text(
            json.dumps(
                {
                    "startTime": "2026-07-18T14:25:22Z",
                    "pptxName": "Test.pptx",
                    "chartId": "c1",
                    "dllVersion": "test",
                    "recordingReason": "unit-test",
                }
            ),
            encoding="utf-8",
        )
        (path / "events.jsonl").write_text(
            '{"t":0,"seq":1,"type":"doc"}\n'
            '{"t":10,"seq":2,"type":"snapshot","entities":[]}\n',
            encoding="utf-8",
        )
        result = manager.generate_report(
            session_id, project_root=self.project, legacy_root=self.legacy
        )
        self.assertTrue(result["generated"])
        report = path / "report.md"
        self.assertTrue(report.is_file())
        self.assertGreater(report.stat().st_size, 20)


if __name__ == "__main__":
    unittest.main()
