"""SSH-free host job/collector checks; no installed app or native events."""
import copy
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock

import pair_test as pair

CODEC = os.environ.get("PAIR_CODEC")


class SplitConfigTests(unittest.TestCase):
    def test_split_endpoints_exclude_wildcard_public_and_ipv6(self):
        for address in ("127.0.0.1", "10.1.2.3", "172.16.1.2", "192.168.1.20"):
            self.assertEqual(pair.split_address(address), address)
        for address in ("0.0.0.0", "8.8.8.8", "224.0.0.1", "255.255.255.255", "::1", "localhost", None):
            with self.assertRaises(pair.Failure):
                pair.split_address(address)

    def test_private_json_is_bounded_and_not_overwritten(self):
        with tempfile.TemporaryDirectory(prefix="keystitch-split-json-") as root:
            path = Path(root) / "request.json"
            pair.write_private_json(path, {"fixture": True})
            with self.assertRaises(FileExistsError):
                pair.write_private_json(path, {"fixture": False})
            with self.assertRaises(pair.Failure):
                pair.read_json_bounded(path, 2)
            self.assertEqual(pair.read_json_bounded(path), {"fixture": True})
            if os.name != "nt":
                self.assertEqual(path.stat().st_mode & 0o777, 0o600)


@unittest.skipUnless(CODEC, "set PAIR_CODEC to the built production codec executable")
class SplitProcessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="keystitch-split-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.counter = 0
        self.hosts, self.paths = [], []
        for host_id in ("listener-test", "connector-test"):
            info = pair.inspect_host(CODEC, host_id)
            host = dict(host_id=host_id, transport="local", python=sys.executable,
                        agent=str(Path(pair.__file__).resolve()), codec=info["codec"],
                        listen="127.0.0.1", connect_address="127.0.0.1",
                        expected={key: info[key] for key in pair.EXPECTED_FIELDS})
            path = self.root / (host_id + ".json")
            pair.write_private_json(path, host)
            self.hosts.append(host)
            self.paths.append(path)

    def command(self, *args):
        return [sys.executable, str(Path(pair.__file__).resolve()), *map(str, args)]

    def run_direction(self, direction, modify_request=None):
        left, right = self.root / ("left-" + str(direction)), self.root / ("right-" + str(direction))
        command = self.command("split-listen", "--host-manifest", self.paths[0],
                               "--peer-host-manifest", self.paths[1], "--run-id", "split-test",
                               "--direction", direction, "--timeout", 10, "--output", left)
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as listener:
            lines = queue.Queue()
            reader = threading.Thread(target=lambda: lines.put(listener.stdout.readline()), daemon=True)
            reader.start()
            try:
                ready_line = lines.get(timeout=12)
                reader.join(timeout=1)
                ready = json.loads(ready_line)
                self.assertEqual(ready.get("stage"), "ready", ready_line)
                request = pair.read_json_bounded(ready["request"])
                self.assertNotIn(request["token"], ready_line)
                request_path = Path(ready["request"])
                if modify_request:
                    modify_request(request)
                    request_path = self.root / "modified-request.json"
                    pair.write_private_json(request_path, request)
                connector = subprocess.run(self.command("split-connect", "--host-manifest", self.paths[1],
                                                        "--request", request_path, "--output", right),
                                           capture_output=True, text=True, timeout=20)
                stdout, stderr = listener.communicate(timeout=20)
                if modify_request:
                    return connector, listener.returncode, [left / "host-result.json", right / "host-result.json"]
                self.assertEqual(connector.returncode, 0, connector.stdout + connector.stderr)
                self.assertEqual(listener.returncode, 0, stdout + stderr)
                for path in (left, right):
                    report = pair.read_json_bounded(path / "host-result.json", pair.MAX_REPORT)
                    self.assertNotIn("token", report)
                    self.assertEqual(report["cleanup"], "completed")
                    self.assertEqual(report["exit_code"], 0)
                return [left / "host-result.json", right / "host-result.json"]
            finally:
                if listener.poll() is None:
                    listener.terminate()
                    listener.communicate(timeout=15)
                reader.join(timeout=2)

    def collect(self, paths, local=True, run_id="split-test"):
        self.counter += 1
        output = self.root / ("collected-" + str(self.counter))
        args = mock.Mock(report=list(map(str, paths)), output=str(output), run_id=run_id, local_smoke=local)
        with mock.patch("builtins.print"):
            code = pair.collect_split(args)
        return code, pair.read_json_bounded(output / "result.json", 4 * pair.MAX_REPORT)

    def test_split_both_fixture_directions_with_one_listener_host(self):
        reports = self.run_direction(0) + self.run_direction(1)
        code, result = self.collect(reports)
        self.assertEqual(code, 0, result)
        self.assertTrue(result["evidence"]["real_subprocess_tcp_codec"])
        self.assertFalse(result["evidence"]["windows_mac_pair"])
        self.assertFalse(result["evidence"]["native_input"])
        self.assertFalse(result["evidence"]["ime"])
        code, result = self.collect(reports, local=False)
        self.assertEqual(code, 1)
        self.assertEqual(result["failure_stage"], "preflight")
        self.assertFalse(result["evidence"]["windows_mac_pair"])
        # Collection never equates acknowledgements/partial or stale jobs to a pass.
        for paths, run_id in ((reports[:2], "split-test"), (reports[:3] + reports[:1], "split-test"),
                              (reports, "other-run")):
            code, result = self.collect(paths, run_id=run_id)
            self.assertEqual(code, 1, result)
            self.assertFalse(result["evidence"]["real_subprocess_tcp_codec"])
        base = [pair.read_json_bounded(path, pair.MAX_REPORT) for path in reports]
        for field, value in (("request_sha256", "a" * 64), ("exit_code", 7), ("cleanup", "unconfirmed"),
                             ("config_sha256", "a" * 64)):
            changed = copy.deepcopy(base)
            changed[1][field] = value
            altered = self.root / (field + ".json")
            pair.write_private_json(altered, changed[1])
            code, result = self.collect([reports[0], altered, *reports[2:]])
            self.assertEqual(code, 1, (field, result))
        for stage in ("handshake", "preflight", "received", "reset"):
            changed = copy.deepcopy(base[1])
            changed["records"].remove(next(item for item in changed["records"] if item["stage"] == stage))
            altered = self.root / (stage + ".json")
            pair.write_private_json(altered, changed)
            code, result = self.collect([reports[0], altered, *reports[2:]])
            self.assertEqual(code, 1, (stage, result))

    def test_split_request_cannot_change_connector_pins_or_add_commands(self):
        request = dict(schema_version=1, boundary="production-codec-record-only", pair_run_id="split-test",
                       direction=0, scenario=pair.read_json_bounded(pair.HERE / "smoke.json"),
                       hosts=self.hosts, connect="127.0.0.1", port=1, token="a" * 64, timeout=10)
        request["config_sha256"] = pair.hashlib.sha256(pair.canonical(request["scenario"])).hexdigest()
        pair.validate_split_request(request)
        for key, value in (("command", "anything"), ("timeout", 121), ("connect", "0.0.0.0"),
                           ("port", 0), ("token", "short"), ("config_sha256", "a" * 64)):
            changed = copy.deepcopy(request)
            changed[key] = value
            with self.assertRaises(pair.Failure):
                pair.validate_split_request(changed)
        changed = copy.deepcopy(request)
        changed["hosts"][1]["expected"]["agent_sha256"] = "a" * 64
        path = self.root / "wrong-host.json"
        pair.write_private_json(path, changed)
        done = subprocess.run(self.command("split-connect", "--host-manifest", self.paths[1],
                                           "--request", path, "--output", self.root / "must-not-start"),
                              capture_output=True, text=True, timeout=10)
        self.assertEqual(done.returncode, 1)
        self.assertFalse((self.root / "must-not-start").exists())

    def test_split_wrong_token_fails_both_hosts_with_cleanup(self):
        connector, listener_code, paths = self.run_direction(0, lambda request: request.update(token="b" * 64))
        self.assertEqual(connector.returncode, 1)
        self.assertEqual(listener_code, 1)
        for path in paths:
            report = pair.read_json_bounded(path, pair.MAX_REPORT)
            self.assertEqual(report["status"], "failed")
            self.assertEqual(report["cleanup"], "completed")
            self.assertIsNotNone(report["exit_code"])
        self.assertEqual(pair.read_json_bounded(paths[0], pair.MAX_REPORT)["failure_stage"], "handshake")

    def test_malformed_split_request_never_starts_a_job(self):
        for index, value in enumerate((None, [], {"hosts": []}, {"hosts": "invalid"})):
            path = self.root / ("bad-request-" + str(index) + ".json")
            output = self.root / ("not-started-" + str(index))
            pair.write_private_json(path, value)
            done = subprocess.run(self.command("split-connect", "--host-manifest", self.paths[1],
                                               "--request", path, "--output", output),
                                  capture_output=True, text=True, timeout=10)
            self.assertEqual(done.returncode, 1)
            self.assertEqual(json.loads(done.stdout)["status"], "failed")
            self.assertFalse(output.exists())

    def test_split_listener_timeout_writes_failure_and_releases_port(self):
        output = self.root / "timeout"
        done = subprocess.run(self.command("split-listen", "--host-manifest", self.paths[0],
                                           "--peer-host-manifest", self.paths[1], "--run-id", "timeout-test",
                                           "--direction", 1, "--timeout", 0.5, "--output", output),
                              capture_output=True, text=True, timeout=15)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        request = pair.read_json_bounded(output / "request.json")
        report = pair.read_json_bounded(output / "host-result.json", pair.MAX_REPORT)
        self.assertEqual(report["failure_stage"], "timeout")
        self.assertEqual(report["cleanup"], "completed")
        self.assertIsNotNone(report["exit_code"])
        with self.assertRaises(OSError):
            pair.socket.create_connection(("127.0.0.1", request["port"]), timeout=0.5)


if __name__ == "__main__":
    unittest.main()
