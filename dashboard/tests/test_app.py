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


class RegisteredClientsTest(unittest.TestCase):
    def test_parses_model_from_server_log(self):
        contents = '''
Client #0:
    name: "endpoint-01"
    version: "1.1"
    binding: "UDP"
    lifetime: 7200 sec
    objects: /3/0, /4/0,
Client #0 model: "BC95GVBAR02A02_LG_BETA260611"
'''

        clients = app.parse_registered_clients(contents)

        self.assertEqual("BC95GVBAR02A02_LG_BETA260611", clients[0]["model"])

    @patch("app.socket.socket")
    @patch("app.tempfile.mkstemp")
    def test_control_list_accepts_model_field(self, mkstemp, socket_factory):
        mkstemp.return_value = (10, "/tmp/wakaama-dashboard-test")
        client_socket = socket_factory.return_value
        client_socket.recv.return_value = b"0\tendpoint-01\t1.1\t3\t7200\tBC95GVBAR02A02\n"

        with patch("app.os.close"), patch("app.os.unlink"):
            clients = app.request_control_list()

        self.assertEqual("BC95GVBAR02A02", clients[0]["model"])
        self.assertEqual("UDP", clients[0]["binding"])

    @patch("app.socket.socket")
    @patch("app.tempfile.mkstemp")
    def test_control_list_keeps_legacy_five_field_response(self, mkstemp, socket_factory):
        mkstemp.return_value = (10, "/tmp/wakaama-dashboard-test")
        client_socket = socket_factory.return_value
        client_socket.recv.return_value = b"0\tendpoint-01\t1.1\t3\t7200\n"

        with patch("app.os.close"), patch("app.os.unlink"):
            clients = app.request_control_list()

        self.assertEqual("", clients[0]["model"])

    def test_binding_labels_ignore_unknown_bit_when_transport_is_present(self):
        self.assertEqual("Not specified", app.binding_label("1"))
        self.assertEqual("UDP", app.binding_label("3"))
        self.assertEqual("UDP, Queue", app.binding_label("35"))
        self.assertEqual("custom", app.binding_label("custom"))


if __name__ == "__main__":
    unittest.main()