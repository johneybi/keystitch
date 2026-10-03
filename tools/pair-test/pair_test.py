#!/usr/bin/env python3
"""Portable controller and stdin-controlled host agent. No native input APIs.

The C++ executable supplies the *production* wire codec. TCP, acknowledgement,
strict fixture state, cancellation and orchestration are test-harness code.
"""
import argparse
import base64
from contextlib import contextmanager
import hashlib
import ipaddress
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
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
MAX_FRAME = 4096
MAX_LINE = 65536
SAFE_ID = re.compile(r"^[A-Za-z0-9_.-]{1,80}$")
FAULTS = ("missing_up", "duplicate_up", "wrong_key", "out_of_order",
          "peer_exit", "handshake_timeout", "leak_state", "bad_result")
SOURCE_FINGERPRINT = "sha256-lf-v1"
EXPECTED_FIELDS = {"os", "arch", "source_commit", "source_digest", "source_fingerprint",
                   "codec_sha256", "agent_sha256", "agent_file_sha256", "source_dirty", "protocol"}
PAIR_FIELDS = ("source_commit", "source_digest", "source_fingerprint", "protocol",
               "agent_sha256", "source_dirty")
MAX_REPORT = 2 * 1024 * 1024


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


class PeerConnections:
    """Socket direction is independent of fixture direction (no native input)."""
    def __init__(self, job, info, stop, deadline):
        self.job, self.info, self.stop, self.deadline = job, info, stop, deadline
        self.network_role = job.get("network_role", "listener" if job["role"] == "sender" else "connector")
        if self.network_role not in ("listener", "connector"):
            raise Failure("configuration", "invalid network role")
        self.listener = None

    def __enter__(self):
        if self.network_role == "listener":
            self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                port = self.job.get("listen_port", 0)
                if port:
                    prepared_port(port)
                self.listener.bind((self.job["listen"], port))
                self.listener.listen(1)
                self.listener.settimeout(0.2)
                emit(self.job, "ready", port=self.listener.getsockname()[1], manifest=self.info)
            except BaseException:
                self.listener.close()
                raise
        return self

    def __exit__(self, *_):
        if self.listener:
            self.listener.close()

    @contextmanager
    def connection(self):
        if self.listener:
            while True:
                checked(self.stop, self.deadline)
                try:
                    sock, _ = self.listener.accept()
                    break
                except socket.timeout:
                    pass
        else:
            sock = connected((self.job["connect"], int(self.job["port"])), self.stop, self.deadline)
        with sock:
            sock.settimeout(0.2)
            token = ("PAIR1:" + self.job["token"]).encode()
            if self.listener:
                auth = receive_frame(sock, self.stop, self.deadline, "handshake")
                if not secrets.compare_digest(auth, token):
                    raise Failure("handshake", "incorrect ephemeral fixture token")
            else:
                send_frame(sock, token, self.stop, self.deadline)
            yield sock


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
        for field in EXPECTED_FIELDS:
            if info[field] != job["expected"][field]:
                raise Failure("preflight", "host changed since preflight: " + field)
        codec = Codec(codec_path)
        timeout = float(job["timeout"])
        if not 0 < timeout <= 120:
            raise Failure("configuration", "timeout must be 0..120 seconds")
        deadline = time.monotonic() + timeout
        emit(job, "preflight", manifest=info)

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
        with PeerConnections(job, info, stop, deadline) as peers:
            if job["role"] == "sender":
                for index, phase in enumerate(job["scenario"]["phases"]):
                    with peers.connection() as sock:
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
                for index, phase in enumerate(job["scenario"]["phases"]):
                    if state.held():
                        raise Failure("state", "record-only sink leaked state across reconnect")
                    emit(job, "state_before", phase=index, state=state.snapshot())
                    with peers.connection() as sock:
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
                    if len(self.records) >= 4096:
                        raise ValueError("too many host records")
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
        if [item.get("phase") for item in items if item.get("stage") == "handshake"] != list(range(len(scenario["phases"]))):
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
        for field in PAIR_FIELDS:
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


def read_json_bounded(path, limit=MAX_LINE):
    with Path(path).open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise Failure("artifacts", "JSON file exceeds size limit")
    return json.loads(data)


def write_private_json(path, value):
    # No overwrite and no token in command arguments or normal stdout.
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def split_address(value):
    try:
        address = ipaddress.IPv4Address(value)
    except (ValueError, TypeError) as error:
        raise Failure("configuration", "split endpoint must be an IPv4 address") from error
    networks = ("127.0.0.0/8", "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")
    if not any(address in ipaddress.IPv4Network(net) for net in networks):
        raise Failure("configuration", "split endpoint must be loopback or a specific private LAN address")
    if str(address) in ("127.0.0.0", "127.255.255.255"):
        raise Failure("configuration", "invalid loopback endpoint")
    return str(address)


def split_hosts(hosts):
    validate_hosts(hosts)
    for host in hosts:
        if (host["transport"] != "local" or set(host["expected"]) != EXPECTED_FIELDS
                or host["expected"]["source_dirty"] is not False):
            raise Failure("preflight", "split jobs require complete, clean local host pins")
    for field in PAIR_FIELDS:
        if hosts[0]["expected"][field] != hosts[1]["expected"][field]:
            raise Failure("preflight", "paired build mismatch: " + field)


def validate_split_request(request):
    fields = {"schema_version", "boundary", "pair_run_id", "direction", "scenario", "config_sha256",
              "hosts", "connect", "port", "token", "timeout"}
    if (not isinstance(request, dict) or set(request) != fields or type(request["schema_version"]) is not int
            or request["schema_version"] != 1
            or request["boundary"] != "production-codec-record-only"
            or not isinstance(request["pair_run_id"], str)
            or not SAFE_ID.fullmatch(request["pair_run_id"]) or len(request["pair_run_id"]) > 78
            or type(request["direction"]) is not int or request["direction"] not in (0, 1)):
        raise Failure("configuration", "invalid split request identity/boundary")
    validate_scenario(request["scenario"])
    if request["config_sha256"] != hashlib.sha256(canonical(request["scenario"])).hexdigest():
        raise Failure("configuration", "split scenario hash mismatch")
    split_hosts(request["hosts"])
    split_address(request["connect"])
    if type(request["port"]) is not int or not 1 <= request["port"] <= 65535:
        raise Failure("configuration", "invalid split port")
    if not isinstance(request["token"], str) or not re.fullmatch(r"[a-f0-9]{64}", request["token"]):
        raise Failure("configuration", "invalid ephemeral split token")
    if type(request["timeout"]) not in (int, float) or not 0 < request["timeout"] <= 120:
        raise Failure("configuration", "split timeout must be 0..120 seconds")


def prepared_port(port):
    if type(port) is not int or not 1024 <= port <= 65535 or port in (24800, 24801):
        raise Failure("configuration", "prepared port must be an unprivileged non-product test port")
    return port


def split_request(args, host):
    hosts = [host, read_json_bounded(args.peer_host_manifest)]
    scenario = read_json_bounded(args.scenario)
    return dict(schema_version=1, boundary="production-codec-record-only",
                pair_run_id=args.run_id, direction=args.direction, scenario=scenario,
                config_sha256=hashlib.sha256(canonical(scenario)).hexdigest(), hosts=hosts,
                connect=split_address(host.get("connect_address", "127.0.0.1")),
                port=1, token=secrets.token_hex(32), timeout=args.timeout)


def split_preflight(host):
    if (Path(host["agent"]).resolve() != Path(__file__).resolve()
            or Path(host["python"]).resolve() != Path(sys.executable).resolve()):
        raise Failure("preflight", "split job must use the current local Python and trusted agent")
    # Check the local executable pin before executing even inspect.
    if digest(host["codec"]) != host["expected"]["codec_sha256"]:
        raise Failure("preflight", "local codec file pin mismatch")
    return preflight(host, 5)


def split_prepare(args):
    """Choose a port and request, then close the socket: no job/listener yet."""
    host = read_json_bounded(args.host_manifest)
    request = split_request(args, host)
    validate_split_request(request)
    split_preflight(host)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
        reservation.bind((split_address(host.get("listen", "127.0.0.1")), 0))
        request["port"] = prepared_port(reservation.getsockname()[1])
    output = Path(args.output).resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    write_private_json(output / "request.json", request)
    print(json.dumps(dict(stage="prepared", request=str(output / "request.json"),
                          run_id=request["pair_run_id"] + "-" + str(request["direction"]),
                          port=request["port"], native_input=False,
                          request_sha256=hashlib.sha256(canonical(request)).hexdigest())), flush=True)
    return 0


def split_job(args):
    """Own one local subprocess; exchange only a bounded request via Codex chat."""
    host = read_json_bounded(args.host_manifest)
    listener = args.action in ("split-listen", "split-serve")
    if args.action == "split-listen":
        request = split_request(args, host)
    else:
        request = read_json_bounded(args.request)
        validate_split_request(request)
        # Commands always come from the current trusted local host manifest.
        if host != request["hosts"][0 if listener else 1]:
            raise Failure("preflight", "request endpoint differs from local host manifest")
        if listener:
            prepared_port(request["port"])
            if request["connect"] != split_address(host.get("connect_address", "127.0.0.1")):
                raise Failure("preflight", "prepared address differs from local host manifest")
    validate_split_request(request)
    hosts = request["hosts"]
    info = split_preflight(host)
    role = ("sender" if request["direction"] == 0 else "sink")
    if not listener:
        role = "sink" if role == "sender" else "sender"
    run_id = request["pair_run_id"] + "-" + str(request["direction"])
    job = dict(run_id=run_id, host_id=host["host_id"], role=role, scenario=request["scenario"],
               expected=info, timeout=request["timeout"], token=request["token"],
               config_sha256=request["config_sha256"], network_role="listener" if listener else "connector")
    if listener:
        job["listen"] = split_address(host.get("listen", "127.0.0.1"))
        if args.action == "split-serve":
            job["listen_port"] = request["port"]
    else:
        job.update(connect=request["connect"], port=request["port"])
    output = Path(args.output).resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    report = dict(schema_version=1, boundary=request["boundary"], pair_run_id=request["pair_run_id"],
                  direction=request["direction"], run_id=run_id, role=role, network_role=job["network_role"],
                  manifest=info, hosts=hosts, scenario=request["scenario"], connect=request["connect"],
                  config_sha256=request["config_sha256"], status="failed", cleanup="unconfirmed")
    process, failure = None, None
    deadline = time.monotonic() + request["timeout"] + 10
    try:
        process = HostProcess(host, job)
        if listener:
            ready = process.wait_stage("ready", deadline)
            if args.action == "split-serve" and ready["port"] != request["port"]:
                raise Failure("preflight", "listener differs from prepared port")
            request["port"] = ready["port"]
            validate_split_request(request)
            write_private_json(output / "request.json", request)
            print(json.dumps({"stage": "ready", "request": str(output / "request.json"),
                              "run_id": run_id, "port": ready["port"], "native_input": False}), flush=True)
        else:
            # Expose actual child preflight, not just a queued chat/Popen claim.
            process.wait_stage("preflight", deadline)
            print(json.dumps(dict(stage="started", run_id=run_id, role=role,
                                  native_input=False, monotonic_ns=time.monotonic_ns())), flush=True)
        report["request_sha256"] = hashlib.sha256(canonical(request)).hexdigest()
        process.wait_stage("result", deadline)
    except (Failure, OSError, KeyboardInterrupt) as error:
        failure = error if isinstance(error, Failure) else Failure(
            "interrupted" if isinstance(error, KeyboardInterrupt) else "process", str(error))
    finally:
        if process:
            try:
                process.cleanup()
                report["cleanup"] = "completed"
            except Failure as error:
                failure = error
            report.update(records=process.records, stderr=process.errors, exit_code=process.proc.returncode)
    if failure is None:
        try:
            if report.get("exit_code") != 0:
                raise Failure("process", "local agent exited unsuccessfully")
            validate_records([(role, process)], request["scenario"], run_id)
        except Failure as error:
            failure = error
    if failure:
        report.update(failure_stage=failure.stage, error=str(failure)[:1000])
    else:
        report["status"] = "passed"
    write_private_json(output / "host-result.json", report)
    print(json.dumps({"status": report["status"], "report": str(output / "host-result.json"),
                      "failure_stage": report.get("failure_stage")}), flush=True)
    return 0 if report["status"] == "passed" else 1


def collect_split(args):
    output = Path(args.output).resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    result = dict(schema_version=1, run_id=args.run_id, status="failed", control_transport="codex_split",
                  coverage="production_codec_over_tcp_record_sink", directions=[],
                  evidence=dict(real_subprocess_tcp_codec=False, windows_mac_pair=False,
                                production_client_server=False, native_input=False, physical_capture=False,
                                ime=False, mouse_navigation=False, production_reconnect_state=False))
    try:
        if not isinstance(args.run_id, str) or not SAFE_ID.fullmatch(args.run_id) or len(args.run_id) > 78:
            raise Failure("configuration", "invalid split collection run ID")
        if len(args.report) != 4:
            raise Failure("artifacts", "collect requires four host reports for two directions")
        reports = [read_json_bounded(path, MAX_REPORT) for path in args.report]
        if any(not isinstance(item, dict) for item in reports):
            raise Failure("artifacts", "split reports must be objects")
        manifests, hosts, scenario = {}, None, None
        for direction in (0, 1):
            pair = [item for item in reports if item.get("direction") == direction]
            if len(pair) != 2 or {item.get("network_role") for item in pair} != {"listener", "connector"}:
                raise Failure("artifacts", "missing/duplicate direction endpoints")
            pair.sort(key=lambda item: item["network_role"] != "listener")
            first, second = pair
            current_hosts = first["hosts"]
            split_hosts(current_hosts)
            if (hosts is not None and hosts != current_hosts) or second["hosts"] != current_hosts:
                raise Failure("artifacts", "host pins changed between split reports")
            hosts = current_hosts
            current_scenario = first["scenario"]
            validate_scenario(current_scenario)
            if (scenario is not None and scenario != current_scenario) or second["scenario"] != current_scenario:
                raise Failure("artifacts", "scenario changed between split reports")
            scenario = current_scenario
            config_hash = hashlib.sha256(canonical(scenario)).hexdigest()
            request_hash = first.get("request_sha256", "")
            if not re.fullmatch(r"[a-f0-9]{64}", request_hash) or second.get("request_sha256") != request_hash:
                raise Failure("artifacts", "split request hashes differ")
            if first["connect"] != second["connect"]:
                raise Failure("artifacts", "split endpoints differ")
            split_address(first["connect"])
            job_id = args.run_id + "-" + str(direction)
            processes = []
            for index, item in enumerate(pair):
                role = "sender" if (direction == 0) == (index == 0) else "sink"
                info = item["manifest"]
                host = hosts[index]
                if (item.get("schema_version") != 1 or item.get("boundary") != "production-codec-record-only"
                        or type(item.get("direction")) is not int
                        or item.get("pair_run_id") != args.run_id or item.get("run_id") != job_id
                        or item.get("role") != role or item.get("status") != "passed"
                        or type(item.get("exit_code")) is not int or item["exit_code"] != 0
                        or item.get("cleanup") != "completed" or item.get("config_sha256") != config_hash
                        or info.get("host_id") != host["host_id"] or info.get("native_input") is not False
                        or info.get("boundary") != "production_ProtocolUtil"):
                    raise Failure("artifacts", "invalid/failed split host evidence")
                for field in EXPECTED_FIELDS:
                    if info.get(field) != host["expected"][field]:
                        raise Failure("preflight", "split runtime differs from host pin: " + field)
                if host["host_id"] in manifests and manifests[host["host_id"]] != info:
                    raise Failure("preflight", "runtime changed between directions")
                manifests[host["host_id"]] = info
                records = item.get("records")
                if not isinstance(records, list) or not 1 <= len(records) <= 4096:
                    raise Failure("artifacts", "missing/oversized split trace")
                if any(not isinstance(record, dict) or record.get("host_id") != host["host_id"]
                       or record.get("scenario_id") != scenario["scenario_id"]
                       or record.get("input_origin") != "protocol_fixture" for record in records):
                    raise Failure("artifacts", "mixed host/scenario in split trace")
                inspected = [record.get("manifest") for record in records if record.get("stage") == "preflight"]
                if inspected != [info]:
                    raise Failure("preflight", "missing/mismatched child runtime preflight")
                processes.append((role, SimpleNamespace(records=records)))
            validate_records(processes, scenario, job_id)
            result["directions"].append(dict(direction=direction, status="passed", reports=pair))
        actual_os = {info["os"] for info in manifests.values()}
        if not args.local_smoke and actual_os != {"Windows", "Darwin"}:
            raise Failure("preflight", "real split pair requires actual Windows and macOS reports")
        if not args.local_smoke and any(ipaddress.IPv4Address(item["connect"]).is_loopback for item in reports):
            raise Failure("preflight", "real split pair cannot use a loopback endpoint")
        result.update(status="passed", manifests=list(manifests.values()), config_sha256=config_hash)
        result["evidence"].update(real_subprocess_tcp_codec=True, windows_mac_pair=not args.local_smoke)
    except (Failure, OSError, ValueError, KeyError, TypeError) as error:
        result.update(failure_stage=getattr(error, "stage", "artifacts"), error=str(error)[:1000])
    write_private_json(output / "result.json", result)
    print(json.dumps({"status": result["status"], "result": str(output / "result.json"),
                      "failure_stage": result.get("failure_stage")}), flush=True)
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
    for action in ("split-prepare", "split-listen", "split-serve", "split-connect"):
        sub = subs.add_parser(action, help="one local bounded job, coordinated without SSH")
        sub.add_argument("--host-manifest", required=True)
        sub.add_argument("--output", required=True)
        if action in ("split-prepare", "split-listen"):
            sub.add_argument("--peer-host-manifest", required=True)
            sub.add_argument("--run-id", required=True)
            sub.add_argument("--direction", type=int, choices=(0, 1), required=True)
            sub.add_argument("--timeout", type=float, default=120)
            sub.add_argument("--scenario", default=str(HERE / "smoke.json"))
        else:
            sub.add_argument("--request", required=True)
    collect = subs.add_parser("split-collect", help="validate all four reports, not chat acknowledgements")
    collect.add_argument("--run-id", required=True)
    collect.add_argument("--report", action="append", required=True)
    collect.add_argument("--output", required=True)
    collect.add_argument("--local-smoke", action="store_true", help="explicit same-host test; never a Windows/Mac pair pass")
    args = parser.parse_args()
    if args.action.startswith("split-"):
        try:
            if args.action == "split-collect":
                return collect_split(args)
            return split_prepare(args) if args.action == "split-prepare" else split_job(args)
        except (Failure, OSError, ValueError, KeyError, TypeError) as error:
            print(json.dumps({"status": "failed", "failure_stage": getattr(error, "stage", "configuration"),
                              "error": str(error)[:1000]}), flush=True)
            return 1
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
