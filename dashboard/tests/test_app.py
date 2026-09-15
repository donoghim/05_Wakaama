import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import app


def completed(stdout="", returncode=0):
    return SimpleNamespace(stdout=stdout, returncode=returncode)


class JournalLogTest(unittest.TestCase):
    @patch("app.subprocess.run")
    def test_reads_latest_entries_since_activation_in_chronological_order(self, run):
        newest = json.dumps({"MESSAGE": "newest"})
        oldest = json.dumps({"MESSAGE": "oldest"})
        run.side_effect = [
            completed("Tue 2026-09-15 16:31:20 KST\n"),
            completed("88835f146fd54c64866057c0205407fe\n"),
            completed("{}\n{}\n".format(newest, oldest)),
        ]

        log = app.read_journal_log("wakaama-server.service")

        self.assertEqual("oldest\nnewest", log["contents"])
        journal_command = run.call_args_list[2].args[0]
        self.assertEqual(
            ["journalctl", "--user", "-u", "wakaama-server.service",
             "--since", "Tue 2026-09-15 16:31:20 KST",
             "_SYSTEMD_INVOCATION_ID=88835f146fd54c64866057c0205407fe",
             "--reverse", "-n", "2000", "--no-pager", "-o", "json"],
            journal_command,
        )

    @patch("app.subprocess.run")
    def test_handles_binary_and_malformed_journal_entries(self, run):
        binary = json.dumps({"MESSAGE": [65, 129, 66]})
        run.side_effect = [completed("activation\n"), completed("invocation\n"),
                           completed("not-json\n{}\n".format(binary))]

        log = app.read_journal_log("wakaama-server.service")

        self.assertEqual("A\ufffdB", log["contents"])

    @patch("app.subprocess.run")
    def test_falls_back_to_latest_entries_when_activation_is_unavailable(self, run):
        run.side_effect = [completed(""), completed(""),
                           completed(json.dumps({"MESSAGE": "latest"}) + "\n")]

        log = app.read_journal_log("wakaama-server.service")

        journal_command = run.call_args_list[2].args[0]
        self.assertNotIn("--since", journal_command)
        self.assertEqual("latest", log["contents"])

    @patch("app.subprocess.run")
    def test_returns_none_when_journalctl_fails(self, run):
        run.side_effect = [completed("activation\n"), completed("invocation\n"), completed(returncode=1)]

        self.assertIsNone(app.read_journal_log("wakaama-server.service"))


if __name__ == "__main__":
    unittest.main()