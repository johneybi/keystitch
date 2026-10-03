#!/usr/bin/env python3
"""Portable controller and stdin-controlled host agent. No native input APIs.

The C++ executable supplies the *production* wire codec. TCP, acknowledgement,
strict fixture state, cancellation and orchestration are test-harness code.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import platform
import queue
import re
import secrets
import shlex
import socket
import struct
import subprocess
import sys
import threading
import time
import uuid

HERE = Path(__file__).resolve().parent
MAX_FRAME = 4096
MAX_LINE = 65536
SAFE_ID = re.compile(r"^[A-Za-z0-9_.-]{1,80}$")
FAULTS = ("missing_up", "duplicate_up", "wrong_key", "out_of_order",
          "peer_exit", "handshake_timeout", "leak_state", "bad_result")
SOURCE_FINGERPRINT = "sha256-lf-v1"
EXPECTED_FIELDS = {"os", "arch", "source_commit", "source_digest", "source_fingerprint",
                   "codec_sha256", "agent_sha256", "agent_file_sha256", "source_dirty", "protocol"}


class Failure(Exception):
    def __init__(self, stage, message):
        super().__init__(message)
        self.stage = stage


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def digest(path):
    hasher = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def text_digest(path):
    # Code identity across Git's LF/CRLF checkouts, not an exact file pin.
    return hashlib.sha256(Path(path).read_bytes().replace(b"\r\n", b"\n")).hexdigest()


class Codec:
    def __init__(self, path):
        self.path = str(Path(path).resolve(strict=True))

    def call(self, *args):
        try:
            done = subprocess.run([self.path, *map(str, args)], capture_output=True,
                                  text=True, timeout=5, check=False)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise Failure("codec", str(error)) from error
        if done.returncode:
            raise Failure("codec", done.stderr.strip()[:1000] or "codec failed")
        try:
            return json.loads(done.stdout)
        except ValueError as error:
            raise Failure("codec", "codec returned invalid JSON") from error

    def encode(self, event):
        name = event["event"]
        args = [name]
        if name in ("key_down", "key_up"):
            args += [event["key"], event["mask"], event["button"]]
        elif name in ("mouse_down", "mouse_up"):
            args += [event["button"]]
        elif name == "hello_back":
            args += [event["name"]]
        return bytes.fromhex(self.call("encode", *args)["hex"])

    def decode(self, data):
        return self.call("decode", data.hex())


def inspect_host(codec_path, host_id):
    codec = Codec(codec_path)
    info = codec.call("describe")
    if (info.get("boundary") != "production_ProtocolUtil" or info.get("native_input") is not False
            or info.get("source_fingerprint") != SOURCE_FINGERPRINT):
        raise Failure("preflight", "expected record-only production codec")
    return dict(info, schema_version=1, host_id=host_id, os=platform.system(),
                arch=platform.machine(), python=platform.python_version(),
                codec=codec.path, codec_sha256=digest(codec.path),
                agent_sha256=text_digest(__file__), agent_file_sha256=digest(__file__))


def validate_scenario(scenario):
    if not isinstance(scenario, dict):
        raise Failure("configuration", "scenario must be an object")
    if scenario.get("schema_version") != 1 or scenario.get("boundary") != "production-codec-record-only":
        raise Failure("configuration", "unsupported scenario schema/boundary")
    if not isinstance(scenario.get("scenario_id"), str) or not SAFE_ID.fullmatch(scenario["scenario_id"]):
        raise Failure("configuration", "invalid scenario ID")
    phases = scenario.get("phases")
    if not isinstance(phases, list) or not 2 <= len(phases) <= 8:
        raise Failure("configuration", "scenario requires 2..8 connection phases")
    for phase in phases:
        if not isinstance(phase, dict):
            raise Failure("configuration", "phase must be an object")
        if (not isinstance(phase.get("name"), str) or not SAFE_ID.fullmatch(phase["name"])
                or type(phase.get("expect_held_at_disconnect")) is not bool):
            raise Failure("configuration", "invalid phase")
        events = phase.get("events")
        if not isinstance(events, list) or not 1 <= len(events) <= 64:
            raise Failure("configuration", "phase requires 1..64 fixture events")
        for event in events:
            if not isinstance(event, dict):
                raise Failure("configuration", "event must be an object")
            name = event.get("event")
            fields = {"event", "button"}
            if name in ("key_down", "key_up"):
                fields |= {"key", "mask"}
            elif name not in ("mouse_down", "mouse_up"):
                raise Failure("configuration", "only fixed key/mouse fixture events are allowed")
            if set(event) != fields:
                raise Failure("configuration", "unexpected fixture fields")
            for key in fields - {"event"}:
                maximum = 5 if key == "button" and name.startswith("mouse") else 65535
                if type(event[key]) is not int or not 0 <= event[key] <= maximum:
                    raise Failure("configuration", "fixture integer out of range")
            if event["button"] == 0:
                raise Failure("configuration", "zero key/mouse button is invalid")


class SinkState:
    """Strict state for the known fixture, not production KeyState behavior."""
    def __init__(self):
        self.keys = set()
        self.buttons = set()

    def snapshot(self):
        return {"keys": sorted(self.keys), "buttons": sorted(self.buttons)}

    def held(self):
        return bool(self.keys or self.buttons)

    def apply(self, event):
        held = self.keys if event["event"].startswith("key") else self.buttons
        button = event["button"]
        if event["event"].endswith("down"):
            if button in held:
                raise Failure("state", "unexpected duplicate down in smoke fixture")
            held.add(button)
        else:
            if button not in held:
                raise Failure("state", "unmatched up in smoke fixture")
            held.remove(button)

    def reset(self):
        self.keys.clear()
        self.buttons.clear()


def checked(stop, deadline):
    if stop.is_set():
        raise Failure("cancelled", "controller cancelled the host job")
    if time.monotonic() >= deadline:
        raise Failure("timeout", "host job deadline exceeded")


def receive_exact(sock, count, stop, deadline, stage):
    data = bytearray()
    while len(data) < count:
        checked(stop, deadline)
        try:
            part = sock.recv(count - len(data))
        except socket.timeout:
            continue
        if not part:
            raise Failure(stage, "peer closed before expected bytes arrived")
        data.extend(part)
    return bytes(data)


def receive_frame(sock, stop, deadline, stage):
    count = struct.unpack("!I", receive_exact(sock, 4, stop, deadline, stage))[0]
    if not 0 < count <= MAX_FRAME:
        raise Failure(stage, "invalid test frame size")
    return receive_exact(sock, count, stop, deadline, stage)


def send_frame(sock, data, stop, deadline):
    packet = struct.pack("!I", len(data)) + data
    view = memoryview(packet)
    while view:
        checked(stop, deadline)
        try:
            sent = sock.send(view)
        except socket.timeout:
            continue
        if not sent:
            raise Failure("delivery", "peer stopped receiving")
        view = view[sent:]


def connected(address, stop, deadline):
    while True:
        checked(stop, deadline)
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(0.2)
        try:
            sock.connect(address)
            return sock
        except OSError:
            sock.close()
            stop.wait(0.05)


def emit(job, stage, **fields):
    item = dict(run_id=job["run_id"], scenario_id=job["scenario"]["scenario_id"],
                host_id=job["host_id"], role=job["role"], stage=stage,
                input_origin="protocol_fixture", monotonic_ns=time.monotonic_ns(), **fields)
    print(json.dumps(item, sort_keys=True), flush=True)


def read_control_line():
    # Raw reads avoid read-ahead and a daemon holding Python's buffered stdin
    # lock at interpreter shutdown. The channel is tiny and controller-owned.
    data = bytearray()
    while len(data) <= MAX_LINE:
        byte = os.read(sys.stdin.fileno(), 1)
        if not byte or byte == b"\n":
            return bytes(data)
        data.extend(byte)
    raise Failure("configuration", "host request too large")


def agent_run(codec_path, host_id):
    stop = threading.Event()
    job = None
    try:
        line = read_control_line()
        job = json.loads(line)
        if not isinstance(job, dict):
            raise Failure("configuration", "host request must be an object")
        if not SAFE_ID.fullmatch(job.get("run_id", "")) or job.get("host_id") != host_id:
            raise Failure("configuration", "invalid host request identity")
        validate_scenario(job["scenario"])
        if job["config_sha256"] != hashlib.sha256(canonical(job["scenario"])).hexdigest():
            raise Failure("configuration", "scenario hash mismatch")
        if job["role"] not in ("sender", "sink") or not re.fullmatch(r"[a-f0-9]{64}", job["token"]):
            raise Failure("configuration", "invalid role/token")
        if job.get("fault") not in (None, *FAULTS):
            raise Failure("configuration", "unknown fault fixture")
        info = inspect_host(codec_path, host_id)
        for field in ("codec_sha256", "source_commit", "source_digest", "source_fingerprint",
                      "agent_sha256", "agent_file_sha256", "os"):
            if info[field] != job["expected"][field]:
                raise Failure("preflight", "host changed since preflight: " + field)
        codec = Codec(codec_path)
        timeout = float(job["timeout"])
        if not 0 < timeout <= 120:
            raise Failure("configuration", "timeout must be 0..120 seconds")
        deadline = time.monotonic() + timeout

        # The control channel stays open until job completion. EOF/cancel only
        # affects this agent's own sockets and children; no persistent service.
        def cancellation():
            try:
                read_control_line()
            except (Failure, OSError):
                pass
            finally:
                stop.set()
        threading.Thread(target=cancellation, daemon=True).start()
        if job["role"] == "sender":
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
                listener.bind((job["listen"], 0))
                listener.listen(1)
                listener.settimeout(0.2)
                emit(job, "ready", port=listener.getsockname()[1], manifest=info)
                for index, phase in enumerate(job["scenario"]["phases"]):
                    while True:
                        checked(stop, deadline)
                        try:
                            sock, _ = listener.accept()
                            break
                        except socket.timeout:
                            pass
                    with sock:
                        sock.settimeout(0.2)
                        auth = receive_frame(sock, stop, deadline, "handshake")
                        if not secrets.compare_digest(auth, ("PAIR1:" + job["token"]).encode()):
                            raise Failure("handshake", "incorrect ephemeral fixture token")
                        if job.get("fault") == "handshake_timeout":
                            while True:
                                checked(stop, deadline)
                                stop.wait(0.05)
                        send_frame(sock, codec.encode({"event": "hello"}), stop, deadline)
                        hello = codec.decode(receive_frame(sock, stop, deadline, "handshake"))
                        if hello != {"event": "hello_back", "protocol": info["protocol"], "name": "pair-sink"}:
                            raise Failure("handshake", "production greeting mismatch")
                        emit(job, "handshake", phase=index)
                        events = list(phase["events"])
                        if index == 0:
                            fault = job.get("fault")
                            if fault == "peer_exit":
                                raise Failure("process", "injected peer exit")
                            if fault == "missing_up":
                                events.pop(1)
                            elif fault == "duplicate_up":
                                events.insert(2, events[1])
                            elif fault == "wrong_key":
                                events[0] = dict(events[0], key=98)
                            elif fault == "out_of_order":
                                events[0], events[1] = events[1], events[0]
                        for seq, event in enumerate(events):
                            send_frame(sock, codec.encode(event), stop, deadline)
                            emit(job, "sent", phase=index, seq=seq, event=event)
                            ack = codec.decode(receive_frame(sock, stop, deadline, "delivery"))
                            if ack != {"event": "noop"}:
                                raise Failure("delivery", "missing fixture acknowledgement")
                        emit(job, "disconnect", phase=index)
        else:
            state = SinkState()
            address = (job["connect"], int(job["port"]))
            for index, phase in enumerate(job["scenario"]["phases"]):
                if state.held():
                    raise Failure("state", "record-only sink leaked state across reconnect")
                emit(job, "state_before", phase=index, state=state.snapshot())
                with connected(address, stop, deadline) as sock:
                    send_frame(sock, ("PAIR1:" + job["token"]).encode(), stop, deadline)
                    hello = codec.decode(receive_frame(sock, stop, deadline, "handshake"))
                    if hello != {"event": "hello", "protocol": info["protocol"]}:
                        raise Failure("handshake", "production greeting mismatch")
                    send_frame(sock, codec.encode({"event": "hello_back", "name": "pair-sink"}), stop, deadline)
                    emit(job, "handshake", phase=index)
                    for seq, expected in enumerate(phase["events"]):
                        event = codec.decode(receive_frame(sock, stop, deadline, "delivery"))
                        if event != expected:
                            raise Failure("delivery", "received event differs at phase %d seq %d" % (index, seq))
                        state.apply(event)
                        emit(job, "received", phase=index, seq=seq, event=event, state=state.snapshot())
                        send_frame(sock, codec.encode({"event": "noop"}), stop, deadline)
                    while True:
                        checked(stop, deadline)
                        try:
                            extra = sock.recv(1)
                            break
                        except socket.timeout:
                            pass
                    if extra:
                        raise Failure("delivery", "unexpected extra packet after fixture")
                if state.held() != phase["expect_held_at_disconnect"]:
                    raise Failure("state", "unexpected held state at disconnect")
                before = state.snapshot()
                if job.get("fault") != "leak_state":
                    state.reset()
                emit(job, "reset", phase=index, before=before, after=state.snapshot(),
                     boundary="harness_sink_not_product_KeyState")
        if job.get("fault") == "bad_result":
            print("not-json", flush=True)
        else:
            emit(job, "result", status="passed", cleanup="completed")
        return 0
    except (Failure, OSError, ValueError, KeyError, TypeError) as error:
        if (isinstance(job, dict) and isinstance(job.get("scenario"), dict)
                and all(field in job for field in ("run_id", "host_id", "role"))
                and "scenario_id" in job["scenario"]):
            emit(job, "result", status="failed", failure_stage=getattr(error, "stage", "process"),
                 error=str(error)[:1000], cleanup="completed")
        else:
            print(json.dumps({"stage": "result", "status": "failed", "error": str(error)[:1000]}), flush=True)
        return 1


def host_command(host, action):
    args = [host["python"], host["agent"], action, "--codec", host["codec"], "--host-id", host["host_id"]]
    if host["transport"] == "local":
        return args
    if (host["transport"] != "ssh" or host["ssh_target"].startswith("-")
            or not re.fullmatch(r"[A-Za-z0-9_.@:-]+", host["ssh_target"])):
        raise Failure("configuration", "invalid host transport/SSH target")
    if host.get("ssh_shell", "posix") == "powershell":
        quote = lambda value: "'" + value.replace("'", "''") + "'"
        script = "& " + " ".join(map(quote, args)) + "; exit $LASTEXITCODE"
        encoded = base64.b64encode(script.encode("utf-16le")).decode()
        command = "powershell.exe -NoProfile -NonInteractive -EncodedCommand " + encoded
    elif host.get("ssh_shell", "posix") == "posix":
        command = " ".join(map(shlex.quote, args))
    else:
        raise Failure("configuration", "ssh_shell must be posix or powershell")
    return ["ssh", "-T", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", host["ssh_target"], command]


def validate_hosts(hosts):
    if not isinstance(hosts, list) or len(hosts) != 2:
        raise Failure("configuration", "host manifest requires exactly two hosts")
    for host in hosts:
        if not isinstance(host, dict):
            raise Failure("configuration", "host must be an object")
        for field in ("host_id", "transport", "python", "agent", "codec"):
            value = host.get(field)
            if not isinstance(value, str) or not value or "\0" in value or "\n" in value:
                raise Failure("configuration", "invalid host field: " + field)
        if not SAFE_ID.fullmatch(host["host_id"]):
            raise Failure("configuration", "invalid host ID")
        if host["transport"] not in ("local", "ssh"):
            raise Failure("configuration", "transport must be local or ssh")
        if host["transport"] == "ssh" and not isinstance(host.get("ssh_target"), str):
            raise Failure("configuration", "SSH target is required")
        host_command(host, "inspect")
        expected = host.get("expected")
        if not isinstance(expected, dict) or set(expected) - EXPECTED_FIELDS:
            raise Failure("configuration", "unknown artifact expectation field")
        if expected.get("os") not in ("Windows", "Darwin", "Linux"):
            raise Failure("configuration", "expected host OS is required")
        if expected.get("source_fingerprint") != SOURCE_FINGERPRINT:
            raise Failure("configuration", "expected source fingerprint policy is required")
        if (not isinstance(expected.get("source_commit"), str)
                or not re.fullmatch(r"(?:[a-f0-9]{40}|[a-f0-9]{64})", expected["source_commit"])):
            raise Failure("configuration", "expected Git commit fingerprint is required")
        for field in ("source_digest", "codec_sha256", "agent_sha256", "agent_file_sha256"):
            if not isinstance(expected.get(field), str) or not re.fullmatch(r"[a-f0-9]{64}", expected[field]):
                raise Failure("configuration", "expected artifact fingerprint missing/invalid: " + field)
        if "source_dirty" in expected and type(expected["source_dirty"]) is not bool:
            raise Failure("configuration", "source_dirty must be a boolean")
        if "protocol" in expected and (not isinstance(expected["protocol"], list)
                or len(expected["protocol"]) != 2
                or any(type(number) is not int or not 0 <= number <= 65535 for number in expected["protocol"])):
            raise Failure("configuration", "protocol must contain two integers")
        for field in ("listen", "connect_address"):
            if field in host:
                try:
                    socket.inet_pton(socket.AF_INET, host[field])
                except (OSError, TypeError) as error:
                    raise Failure("configuration", field + " must be an IPv4 address") from error
    if hosts[0]["host_id"] == hosts[1]["host_id"]:
        raise Failure("configuration", "two distinct host IDs are required")


def make_manifest(args):
    info = inspect_host(args.codec, args.host_id)
    host = dict(host_id=args.host_id, transport="ssh" if args.ssh_target else "local",
                python=sys.executable, agent=str(Path(__file__).resolve()), codec=info["codec"],
                listen=args.listen, connect_address=args.connect_address,
                expected={key: info[key] for key in EXPECTED_FIELDS})
    if args.ssh_target:
        host.update(ssh_target=args.ssh_target, ssh_shell=args.ssh_shell)
    # Validate without connecting or assuming a second computer exists.
    partner = dict(host, host_id="validation-peer" if args.host_id != "validation-peer" else "validation-other")
    validate_hosts([host, partner])
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(host, stream, indent=2)
        stream.write("\n")
    print(json.dumps({"status": "prepared", "manifest": str(Path(args.output).resolve()),
                      "host_id": args.host_id, "native_input": False}, sort_keys=True))
    return 0


class HostProcess:
    def __init__(self, host, job):
        self.records = []
        self.errors = []
        self.items = queue.Queue()
        self.cleaned = False
        self.proc = subprocess.Popen(host_command(host, "agent"), stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        def reader():
            while True:
                line = self.proc.stdout.readline(MAX_LINE + 1)
                if not line:
                    self.items.put(None)
                    return
                try:
                    if len(line) > MAX_LINE:
                        raise ValueError("host output line too long")
                    item = json.loads(line)
                    if not isinstance(item, dict):
                        raise ValueError("host output must be an object")
                    self.records.append(item)
                    self.items.put(item)
                except ValueError as error:
                    self.items.put(Failure("artifacts", str(error)))
        def stderr_reader():
            for _ in range(100):
                line = self.proc.stderr.readline(1024)
                if not line:
                    return
                self.errors.append(line.decode("utf-8", errors="replace").rstrip())
            # Drain without storing if a failing child is unusually noisy.
            for _ in iter(lambda: self.proc.stderr.read(4096), b""):
                pass
        self.readers = [threading.Thread(target=reader, daemon=True),
                        threading.Thread(target=stderr_reader, daemon=True)]
        for thread in self.readers:
            thread.start()
        try:
            self.proc.stdin.write(canonical(job) + b"\n")
            self.proc.stdin.flush()
        except OSError:
            self.cleanup()
            raise

    def wait_stage(self, stage, deadline):
        while True:
            try:
                item = self.items.get(timeout=max(0.01, deadline - time.monotonic()))
            except queue.Empty as error:
                raise Failure("timeout", "waiting for host stage: " + stage) from error
            if isinstance(item, Failure):
                raise item
            if item is None:
                raise Failure("process", "host exited without " + stage)
            if item.get("stage") == "result" and item.get("status") != "passed":
                raise Failure(item.get("failure_stage", "process"), item.get("error", "host failed"))
            if item.get("stage") == stage:
                return item
            if time.monotonic() >= deadline:
                raise Failure("timeout", "host stage deadline exceeded")

    def cleanup(self):
        if self.cleaned:
            return
        failure = None
        if self.proc.poll() is None:
            try:
                self.proc.stdin.write(b'{"command":"cancel"}\n')
                self.proc.stdin.flush()
            except (BrokenPipeError, OSError):
                pass
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=7)
        except subprocess.TimeoutExpired:
            self.proc.terminate()  # Only this controller's known child PID.
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=3)
            failure = Failure("cleanup", "host did not acknowledge bounded cancellation")
        for thread in self.readers:
            thread.join(timeout=2)
            if thread.is_alive():
                failure = Failure("cleanup", "host output pipe did not close")
        if all(not thread.is_alive() for thread in self.readers):
            self.proc.stdout.close()
            self.proc.stderr.close()
        self.cleaned = True
        if failure:
            raise failure


def preflight(host, timeout):
    try:
        done = subprocess.run(host_command(host, "inspect"), capture_output=True,
                              text=True, timeout=timeout, check=False)
        if done.returncode:
            raise Failure("preflight", done.stderr.strip()[:1000] or "host inspect failed")
        info = json.loads(done.stdout)
        if info["host_id"] != host["host_id"] or info["native_input"] is not False:
            raise Failure("preflight", "unexpected host identity/boundary")
        for field, expected in host["expected"].items():
            if info.get(field) != expected:
                raise Failure("preflight", "host manifest mismatch: " + field)
        return info
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        raise Failure("preflight", str(error)) from error


def validate_records(processes, scenario, job_id):
    for role, process in processes:
        items = process.records
        results = [item for item in items if item.get("stage") == "result"]
        if len(results) != 1 or results[0].get("status") != "passed" or results[0].get("cleanup") != "completed":
            raise Failure("artifacts", "missing/invalid host result")
        if any(item.get("run_id") != job_id or item.get("role") != role for item in items):
            raise Failure("artifacts", "mixed run/role identity in host logs")
        stage = "sent" if role == "sender" else "received"
        observed = [(item["phase"], item["seq"], item["event"]) for item in items if item.get("stage") == stage]
        expected = [(phase_id, seq, event) for phase_id, phase in enumerate(scenario["phases"])
                    for seq, event in enumerate(phase["events"])]
        if observed != expected:
            raise Failure("artifacts", "incomplete or different event trace")
        if len([item for item in items if item.get("stage") == "handshake"]) != len(scenario["phases"]):
            raise Failure("artifacts", "missing handshake/reconnect evidence")
        if role == "sink":
            resets = [item for item in items if item.get("stage") == "reset"]
            if len(resets) != len(scenario["phases"]) or any(item["after"] != {"keys": [], "buttons": []} for item in resets):
                raise Failure("state", "missing empty record-only state after disconnect")
            # Independently replay the log. A final success line alone cannot
            # establish that transitions and disconnect-held evidence happened.
            for phase_id, phase in enumerate(scenario["phases"]):
                state = SinkState()
                before = [item for item in items if item.get("stage") == "state_before" and item.get("phase") == phase_id]
                if len(before) != 1 or before[0].get("state") != state.snapshot():
                    raise Failure("state", "missing empty record-only state before reconnect")
                for item in items:
                    if item.get("stage") == "received" and item.get("phase") == phase_id:
                        state.apply(item["event"])
                        if item.get("state") != state.snapshot():
                            raise Failure("state", "received state trace does not match fixture")
                reset = resets[phase_id]
                if (reset.get("phase") != phase_id or reset.get("before") != state.snapshot()
                        or state.held() != phase["expect_held_at_disconnect"]):
                    raise Failure("state", "disconnect-held evidence differs from fixture")


def run_direction(hosts, infos, scenario, run_id, timeout, fault):
    processes = []
    report = {"sender": hosts[0]["host_id"], "sink": hosts[1]["host_id"], "status": "failed"}
    failure = None
    token = secrets.token_hex(32)
    common = {"run_id": run_id, "scenario": scenario, "timeout": timeout, "token": token,
              "config_sha256": hashlib.sha256(canonical(scenario)).hexdigest()}
    deadline = time.monotonic() + timeout + 10
    try:
        sender_job = dict(common, role="sender", host_id=hosts[0]["host_id"], expected=infos[0],
                          listen=hosts[0].get("listen", "127.0.0.1"), fault=fault)
        sender = HostProcess(hosts[0], sender_job)
        processes.append(("sender", sender))
        ready = sender.wait_stage("ready", deadline)
        sink_job = dict(common, role="sink", host_id=hosts[1]["host_id"], expected=infos[1],
                        connect=hosts[0].get("connect_address", "127.0.0.1"), port=ready["port"],
                        fault=fault if fault in ("leak_state", "bad_result") else None)
        sink = HostProcess(hosts[1], sink_job)
        processes.append(("sink", sink))
        # Sink reports the most specific event/state failures before sender's
        # secondary connection error; preserve that distinction in the report.
        sink.wait_stage("result", deadline)
        sender.wait_stage("result", deadline)
    except (Failure, OSError, KeyError, KeyboardInterrupt) as error:
        failure = error if isinstance(error, Failure) else Failure(
            "interrupted" if isinstance(error, KeyboardInterrupt) else "process", str(error))
    finally:
        for role, process in processes:
            try:
                process.cleanup()
            except Failure as error:
                failure = error
            report[role + "_log"] = process.records
            report[role + "_stderr"] = process.errors
            report[role + "_exit"] = process.proc.returncode
            if process.proc.returncode and failure is None:
                failure = Failure("process", role + " exited unsuccessfully")
    if failure is None:
        try:
            validate_records(processes, scenario, run_id)
        except Failure as error:
            failure = error
    if failure and failure.stage in ("delivery", "handshake", "timeout"):
        for role, process in processes:
            for item in process.records:
                if item.get("stage") == "result" and item.get("failure_stage") == "process":
                    failure = Failure("process", item["error"])
                elif (item.get("stage") == "result" and item.get("failure_stage") in ("timeout", "codec")
                      and str(failure).startswith("peer closed before expected bytes")):
                    # A deadline/codec failure can close the sender just before
                    # the sink reports EOF; retain the cause, not the symptom.
                    failure = Failure(item["failure_stage"], item["error"])
    if failure:
        report.update(failure_stage=failure.stage, error=str(failure))
    else:
        report["status"] = "passed"
    return report


def controller(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    result = {"schema_version": 1, "run_id": "pair-" + uuid.uuid4().hex, "status": "failed",
              "coverage": "production_codec_over_tcp_record_sink",
              "evidence": {"real_subprocess_tcp_codec": False, "windows_mac_pair": False,
                           "production_client_server": False, "native_input": False,
                           "physical_capture": False, "ime": False, "mouse_navigation": False,
                           "production_reconnect_state": False}, "directions": []}
    started = time.monotonic()
    try:
        if not 0 < args.timeout <= 120:
            raise Failure("configuration", "timeout must be 0..120 seconds")
        scenario = json.loads(Path(args.scenario).read_text(encoding="utf-8"))
        validate_scenario(scenario)
        pair_mode = bool(args.hosts or args.host_manifest)
        if args.hosts:
            config = json.loads(Path(args.hosts).read_text(encoding="utf-8"))
            if not isinstance(config, dict) or config.get("schema_version") != 1:
                raise Failure("configuration", "host manifest requires exactly two hosts")
            hosts = config["hosts"]
        elif args.host_manifest:
            hosts = [json.loads(Path(path).read_text(encoding="utf-8")) for path in args.host_manifest]
        else:
            if not args.codec:
                raise Failure("configuration", "supply --codec for local smoke or --hosts for a real pair")
            hosts = []
            for host_id in ("local-a", "local-b"):
                info = inspect_host(args.codec, host_id)
                hosts.append(dict(host_id=host_id, transport="local", python=sys.executable,
                                  agent=str(Path(__file__).resolve()), codec=info["codec"],
                                  expected={key: info[key] for key in EXPECTED_FIELDS}))
        validate_hosts(hosts)
        infos = [preflight(host, args.timeout) for host in hosts]
        for field in ("source_commit", "source_digest", "source_fingerprint", "protocol",
                      "agent_sha256", "source_dirty"):
            if infos[0][field] != infos[1][field]:
                raise Failure("preflight", "paired build mismatch: " + field)
        if pair_mode and {info["os"] for info in infos} != {"Windows", "Darwin"}:
            raise Failure("preflight", "real pair mode requires actual Windows and macOS hosts")
        result["manifests"] = infos
        result["config_sha256"] = hashlib.sha256(canonical(scenario)).hexdigest()
        for direction in range(2):
            report = run_direction(hosts, infos, scenario, result["run_id"] + "-" + str(direction),
                                   args.timeout, args.fault)
            result["directions"].append(report)
            if report["status"] != "passed":
                raise Failure(report["failure_stage"], report["error"])
            hosts, infos = list(reversed(hosts)), list(reversed(infos))
        result["status"] = "passed"
        result["evidence"]["real_subprocess_tcp_codec"] = True
        result["evidence"]["windows_mac_pair"] = pair_mode
    except (Failure, OSError, ValueError, KeyError, TypeError, KeyboardInterrupt) as error:
        result.update(failure_stage=getattr(error, "stage", "interrupted" if isinstance(error, KeyboardInterrupt) else "configuration"),
                      error=str(error)[:1000])
    result["duration_seconds"] = round(time.monotonic() - started, 3)
    try:
        for index, report in enumerate(result["directions"]):
            for role in ("sender", "sink"):
                (output / ("direction-%d-%s.jsonl" % (index, role))).write_text(
                    "".join(json.dumps(item, sort_keys=True) + "\n" for item in report.get(role + "_log", [])), encoding="utf-8")
    except OSError as error:
        result.update(status="failed", failure_stage="artifacts", error=str(error)[:1000])
        result["evidence"]["real_subprocess_tcp_codec"] = False
        result["evidence"]["windows_mac_pair"] = False
    try:
        (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    except OSError as error:
        print(json.dumps({"status": "failed", "failure_stage": "artifacts", "error": str(error)[:1000]}))
        return 1
    print(json.dumps({"status": result["status"], "result": str(output / "result.json"),
                      "failure_stage": result.get("failure_stage")}, sort_keys=True))
    return 0 if result["status"] == "passed" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="action", required=True)
    for name in ("inspect", "agent", "manifest"):
        sub = subs.add_parser(name)
        sub.add_argument("--codec", required=True)
        sub.add_argument("--host-id", required=True)
        if name == "manifest":
            sub.add_argument("--output", required=True, help="a new, non-existing host manifest")
            sub.add_argument("--ssh-target", help="existing SSH alias/target as seen from the controller")
            sub.add_argument("--ssh-shell", choices=("posix", "powershell"), default="posix")
            sub.add_argument("--listen", default="127.0.0.1")
            sub.add_argument("--connect-address", default="127.0.0.1")
    run = subs.add_parser("run")
    mode = run.add_mutually_exclusive_group(required=True)
    mode.add_argument("--codec")
    mode.add_argument("--hosts")
    mode.add_argument("--host-manifest", action="append", help="one host manifest; provide exactly twice")
    run.add_argument("--scenario", default=str(HERE / "smoke.json"))
    run.add_argument("--output", required=True, help="a new, non-existing result directory")
    run.add_argument("--timeout", type=float, default=30)
    run.add_argument("--fault", choices=FAULTS, help="test-harness fault injection only")
    args = parser.parse_args()
    if args.action == "run":
        try:
            return controller(args)
        except OSError as error:
            print(json.dumps({"status": "failed", "failure_stage": "artifacts", "error": str(error)[:1000]}))
            return 1
    if not SAFE_ID.fullmatch(args.host_id):
        parser.error("invalid host ID")
    if args.action == "inspect":
        try:
            print(json.dumps(inspect_host(args.codec, args.host_id), sort_keys=True))
            return 0
        except (Failure, OSError) as error:
            print(str(error), file=sys.stderr)
            return 1
    if args.action == "manifest":
        try:
            return make_manifest(args)
        except (Failure, OSError) as error:
            print(str(error), file=sys.stderr)
            return 1
    return agent_run(args.codec, args.host_id)


if __name__ == "__main__":
    sys.exit(main())
