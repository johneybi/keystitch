import copy
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

import pair_test as pair

CODEC = os.environ.get("PAIR_CODEC")
SCENARIO = json.loads((pair.HERE / "smoke.json").read_text(encoding="utf-8"))


class StateAndConfigTests(unittest.TestCase):
    def test_text_identity_is_line_ending_independent_but_file_pin_is_exact(self):
        with tempfile.TemporaryDirectory(prefix="keystitch-text-fingerprint-") as root:
            lf = Path(root) / "lf.py"
            crlf = Path(root) / "crlf.py"
            changed = Path(root) / "changed.py"
            lf.write_bytes(b"print('fixture')\n")
            crlf.write_bytes(b"print('fixture')\r\n")
            changed.write_bytes(b"print('different')\n")
            self.assertEqual(pair.text_digest(lf), pair.text_digest(crlf))
            self.assertNotEqual(pair.digest(lf), pair.digest(crlf))
            self.assertNotEqual(pair.text_digest(lf), pair.text_digest(changed))

    def test_valid_scenario(self):
        pair.validate_scenario(SCENARIO)

    def test_invalid_scenario_and_button(self):
        for field, value in (("button", 0), ("key", -1), ("mask", True)):
            case = copy.deepcopy(SCENARIO)
            case["phases"][0]["events"][0][field] = value
            with self.assertRaises(pair.Failure):
                pair.validate_scenario(case)
        case = copy.deepcopy(SCENARIO)
        case["phases"][0]["events"][0]["text"] = "personal input is not a fixture field"
        with self.assertRaises(pair.Failure):
            pair.validate_scenario(case)
        for invalid in (None, [], {"schema_version": 1, "boundary": "production-codec-record-only", "scenario_id": None}):
            with self.assertRaises(pair.Failure):
                pair.validate_scenario(invalid)

    def test_fixture_up_and_reset_contract(self):
        state = pair.SinkState()
        with self.assertRaises(pair.Failure):
            state.apply({"event": "mouse_up", "button": 4})
        state.apply({"event": "mouse_down", "button": 4})
        state.apply({"event": "key_down", "button": 42})
        self.assertTrue(state.held())
        state.reset()
        self.assertEqual(state.snapshot(), {"keys": [], "buttons": []})

    def test_frame_fragmentation_and_oversize(self):
        # Use TCP rather than socketpair, which varies on Windows versions.
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            with socket.create_connection(listener.getsockname()) as writer:
                reader, _ = listener.accept()
                with reader:
                    reader.settimeout(0.2)
                    for byte in b"\x00\x00\x00\x04CNOP":
                        writer.sendall(bytes([byte]))
                    got = pair.receive_frame(reader, threading.Event(), time.monotonic() + 1, "delivery")
                    self.assertEqual(got, b"CNOP")
                    writer.sendall(b"\x00\x00\x20\x00")
                    with self.assertRaises(pair.Failure):
                        pair.receive_frame(reader, threading.Event(), time.monotonic() + 1, "delivery")

    def test_ssh_paths_are_quoted_not_interpolated(self):
        host = dict(transport="ssh", python="/some dir/python", agent="/some dir/pair_test.py",
                    codec="/some dir/codec", host_id="mac", ssh_target="test@host")
        self.assertIn("'/some dir/python'", pair.host_command(host, "inspect")[-1])
        host.update(ssh_shell="powershell", python="C:\\a'b\\python.exe")
        command = pair.host_command(host, "inspect")[-1]
        script = pair.base64.b64decode(command.split()[-1]).decode("utf-16le")
        self.assertIn("C:\\a''b\\python.exe", script)
        for target in ("-oProxyCommand=evil", "-V", "test@host;evil"):
            host["ssh_target"] = target
            with self.assertRaises(pair.Failure):
                pair.host_command(host, "inspect")

    def test_invalid_host_fingerprints_and_addresses_are_rejected(self):
        host = dict(host_id="first", transport="local", python="python", agent="agent.py", codec="codec",
                    expected=dict(os="Darwin", source_commit="a" * 40, source_digest="a" * 64,
                                  source_fingerprint=pair.SOURCE_FINGERPRINT,
                                  codec_sha256="a" * 64, agent_sha256="a" * 64, agent_file_sha256="a" * 64))
        other = dict(host, host_id="second")
        pair.validate_hosts([host, other])
        for field, value in (("codec_sha256", "missing"), ("agent_file_sha256", "missing"),
                             ("source_fingerprint", "old-policy"), ("source_commit", []),
                             ("source_dirty", "false"), ("protocol", [1, True])):
            case = copy.deepcopy(host)
            case["expected"][field] = value
            with self.assertRaises(pair.Failure):
                pair.validate_hosts([case, other])
        for field in ("listen", "connect_address"):
            case = dict(host, **{field: "not-an-IP"})
            with self.assertRaises(pair.Failure):
                pair.validate_hosts([case, other])
        with self.assertRaises(pair.Failure):
            pair.validate_hosts([host, host])

    def test_missing_result_trace_is_not_pass(self):
        fake = mock.Mock(records=[{"stage": "handshake", "run_id": "run", "role": "sink"}])
        with self.assertRaises(pair.Failure):
            pair.validate_records([("sink", fake)], SCENARIO, "run")


@unittest.skipUnless(CODEC, "set PAIR_CODEC to the built production codec executable")
class RealCodecAndProcessTests(unittest.TestCase):
    def setUp(self):
        self.codec = pair.Codec(CODEC)
        self.temporary = tempfile.TemporaryDirectory(prefix="keystitch-pair-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def run_controller(self, fault=None, hosts=None):
        output = self.root / ("result-" + str(len(list(self.root.iterdir()))))
        args = [sys.executable, str(pair.HERE / "pair_test.py"), "run", "--output", str(output), "--timeout", "3"]
        if fault:
            args += ["--fault", fault]
        if hosts:
            path = self.root / "hosts.json"
            path.write_text(json.dumps(hosts), encoding="utf-8")
            args += ["--hosts", str(path)]
        else:
            args += ["--codec", str(CODEC)]
        done = subprocess.run(args, capture_output=True, text=True, timeout=25)
        self.assertTrue((output / "result.json").exists(), done.stderr)
        return done, json.loads((output / "result.json").read_text(encoding="utf-8"))

    def test_wire_bytes_and_roundtrip_use_production_codec(self):
        event = {"event": "mouse_down", "button": 4}
        payload = self.codec.encode(event)
        self.assertEqual(payload, b"DMDN\x04")
        self.assertEqual(self.codec.decode(payload), event)
        for phase in SCENARIO["phases"]:
            for event in phase["events"]:
                self.assertEqual(self.codec.decode(self.codec.encode(event)), event)
        hello = self.codec.encode({"event": "hello"})
        self.assertEqual(hello, b"Barrier\x00\x01\x00\x06")
        self.assertEqual(self.codec.decode(hello)["protocol"], [1, 6])

    def test_truncated_oversized_and_trailing_wire_payloads_fail(self):
        for wire in (b"DKDN", b"DMDN", b"DMUP\x04extra", b"CNOPextra", b"ZZZZ",
                     b"Barrier\x00\x01\x00\x06\xff\xff\xff\xff"):
            with self.assertRaises(pair.Failure):
                self.codec.decode(wire)
        with self.assertRaises(pair.Failure):
            self.codec.call("decode", "ff" * 1025)

    def test_real_subprocess_pair_both_directions(self):
        done, result = self.run_controller()
        self.assertEqual(done.returncode, 0, done.stderr + json.dumps(result))
        self.assertEqual(result["status"], "passed")
        self.assertEqual(len(result["directions"]), 2)
        self.assertTrue(result["evidence"]["real_subprocess_tcp_codec"])
        self.assertFalse(result["evidence"]["windows_mac_pair"])
        self.assertFalse(result["evidence"]["native_input"])
        self.assertFalse(result["evidence"]["production_reconnect_state"])
        for direction in result["directions"]:
            self.assertEqual(direction["sender_exit"], 0)
            self.assertEqual(direction["sink_exit"], 0)
            self.assertEqual(len([e for e in direction["sink_log"] if e["stage"] == "received"]), 16)
            reset = [e for e in direction["sink_log"] if e["stage"] == "reset"][1]
            self.assertEqual(reset["before"], {"keys": [42], "buttons": [4]})
            self.assertEqual(reset["after"], {"keys": [], "buttons": []})

    def test_faults_never_return_success(self):
        for fault in pair.FAULTS:
            with self.subTest(fault=fault):
                done, result = self.run_controller(fault=fault)
                self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
                self.assertEqual(result["status"], "failed")
                self.assertTrue(result.get("failure_stage"))
                expected_stage = {"peer_exit": "process", "handshake_timeout": "timeout",
                                  "leak_state": "state", "bad_result": "artifacts"}.get(fault, "delivery")
                self.assertEqual(result["failure_stage"], expected_stage, json.dumps(result))
                for direction in result["directions"]:
                    for role in ("sender", "sink"):
                        if role + "_log" in direction:
                            self.assertIsNotNone(direction.get(role + "_exit"), "owned process was left running")

    def test_preflight_wrong_hash_and_missing_host_fail(self):
        info = pair.inspect_host(CODEC, "first")
        host = dict(host_id="first", transport="local", python=sys.executable,
                    agent=str(pair.HERE / "pair_test.py"), codec=str(CODEC), expected=info)
        other = copy.deepcopy(host)
        other["host_id"] = "second"
        other["expected"]["host_id"] = "second"
        # Host identity is verified separately, not an expected artifact field.
        for current in (host, other):
            current["expected"] = {key: value for key, value in current["expected"].items()
                                   if key in pair.EXPECTED_FIELDS}
        other["expected"]["codec_sha256"] = "0" * 64
        done, result = self.run_controller(hosts={"schema_version": 1, "hosts": [host, other]})
        self.assertEqual(done.returncode, 1)
        self.assertEqual(result["failure_stage"], "preflight")
        self.assertEqual(result["directions"], [])
        done, result = self.run_controller(hosts={"schema_version": 1, "hosts": [host]})
        self.assertEqual(done.returncode, 1)
        self.assertEqual(result["failure_stage"], "configuration")

    def test_real_pair_mode_rejects_two_processes_on_same_os(self):
        hosts = []
        for host_id in ("first", "second"):
            info = pair.inspect_host(CODEC, host_id)
            hosts.append(dict(host_id=host_id, transport="local", python=sys.executable,
                              agent=str(pair.HERE / "pair_test.py"), codec=str(CODEC),
                              expected={key: info[key] for key in pair.EXPECTED_FIELDS}))
        done, result = self.run_controller(hosts={"schema_version": 1, "hosts": hosts})
        self.assertEqual(done.returncode, 1)
        self.assertEqual(result["failure_stage"], "preflight")
        self.assertFalse(result["evidence"]["windows_mac_pair"])
        self.assertEqual(result["directions"], [])

    def test_cancel_after_ready_exits_owned_agent(self):
        info = pair.inspect_host(CODEC, "cancel-test")
        host = dict(host_id="cancel-test", transport="local", python=sys.executable,
                    agent=str(pair.HERE / "pair_test.py"), codec=str(CODEC))
        job = dict(run_id="cancel-test", host_id="cancel-test", role="sender", expected=info,
                   scenario=SCENARIO, config_sha256=pair.hashlib.sha256(pair.canonical(SCENARIO)).hexdigest(),
                   token="a" * 64, timeout=10, listen="127.0.0.1")
        process = pair.HostProcess(host, job)
        self.addCleanup(process.cleanup)
        process.wait_stage("ready", time.monotonic() + 5)
        process.cleanup()
        self.assertEqual(process.proc.returncode, 1)
        self.assertTrue(any(e.get("failure_stage") == "cancelled" for e in process.records))

    def test_invalid_agent_request_exits_without_shutdown_abort(self):
        for request in (b"null\n", b"[]\n", b"not-json\n"):
            done = subprocess.run([sys.executable, str(pair.HERE / "pair_test.py"), "agent",
                                   "--codec", str(CODEC), "--host-id", "bad-job"],
                                  input=request, capture_output=True, timeout=5)
            self.assertEqual(done.returncode, 1, done.stderr)
            self.assertEqual(json.loads(done.stdout)["status"], "failed")
            self.assertNotIn(b"Fatal Python error", done.stderr)

    def test_manifest_is_generated_from_actual_artifacts_and_never_overwritten(self):
        output = self.root / "host.json"
        args = [sys.executable, str(pair.HERE / "pair_test.py"), "manifest", "--codec", str(CODEC),
                "--host-id", "fixture-mac", "--output", str(output)]
        done = subprocess.run(args, capture_output=True, timeout=5)
        self.assertEqual(done.returncode, 0, done.stderr)
        host = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(host["expected"]["codec_sha256"], pair.digest(CODEC))
        self.assertEqual(host["expected"]["agent_sha256"], pair.text_digest(pair.__file__))
        self.assertEqual(host["expected"]["agent_file_sha256"], pair.digest(pair.__file__))
        self.assertEqual(host["transport"], "local")
        original = output.read_bytes()
        done = subprocess.run(args, capture_output=True, timeout=5)
        self.assertEqual(done.returncode, 1)
        self.assertEqual(output.read_bytes(), original)

    def test_reformatted_agent_keeps_code_identity_but_fails_exact_host_pin(self):
        copied = self.root / "pair_test.py"
        original = Path(pair.__file__).read_bytes()
        changed = (original.replace(b"\r\n", b"\n") if b"\r\n" in original
                   else original.replace(b"\n", b"\r\n"))
        copied.write_bytes(changed)
        self.assertEqual(pair.text_digest(copied), pair.text_digest(pair.__file__))
        self.assertNotEqual(pair.digest(copied), pair.digest(pair.__file__))
        info = pair.inspect_host(CODEC, "file-pin-test")
        host = dict(host_id="file-pin-test", transport="local", python=sys.executable,
                    agent=str(copied), codec=str(CODEC),
                    expected={key: info[key] for key in pair.EXPECTED_FIELDS})
        with self.assertRaisesRegex(pair.Failure, "agent_file_sha256"):
            pair.preflight(host, 5)

    def test_record_state_tampering_is_rejected(self):
        done, result = self.run_controller()
        self.assertEqual(done.returncode, 0, done.stderr)
        records = copy.deepcopy(result["directions"][0]["sink_log"])
        next(item for item in records if item["stage"] == "state_before")["state"]["keys"] = [42]
        fake = mock.Mock(records=records)
        with self.assertRaises(pair.Failure):
            pair.validate_records([("sink", fake)], SCENARIO, result["run_id"] + "-0")

    def test_log_write_failure_cannot_leave_passed_result(self):
        args = mock.Mock(output=str(self.root / "write-failure"), scenario=str(pair.HERE / "smoke.json"),
                         hosts=None, host_manifest=None, codec=str(CODEC), timeout=3, fault=None)
        original = Path.write_text
        def fail_log(path, *values, **options):
            if path.suffix == ".jsonl":
                raise OSError("injected artifact write failure")
            return original(path, *values, **options)
        with mock.patch.object(Path, "write_text", fail_log):
            self.assertEqual(pair.controller(args), 1)
        result = json.loads((Path(args.output) / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(result["failure_stage"], "artifacts")
        self.assertEqual(result["status"], "failed")
        self.assertFalse(result["evidence"]["real_subprocess_tcp_codec"])


if __name__ == "__main__":
    unittest.main()
