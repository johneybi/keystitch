# Paired test runner — record-only foundation

This tool makes a Windows/macOS test **one controller run**, with version-pinned
host manifests and a single result directory. It does not install, stop, replace
or interact with the KeyStitch app you are using.

## What a pass means

The native `pair-codec` executable compiles the real production
`ProtocolUtil.cpp` and `protocol_types.cpp`. The Python harness starts two
separate host processes, sends those encoded messages over TCP, decodes them
on the receiver and compares the exact fixture sequence. It reverses the host
roles and repeats. Each direction has three connections and 16 key/mouse events:

- A down/up and Shift+A down/up, including modifier release;
- mouse buttons 4 and 5 down/up;
- disconnect while Shift and button 4 are held;
- reconnect with an empty **test sink** and repeat input.

This is **not** a production Client/Server integration test. Authentication,
TCP framing, acknowledgements and reconnect state in this tool belong to the
harness. An empty harness state does not prove production `KeyState` cleanup.
No physical input is captured, no CGEvent/IOHID/Virtual HID is used, and no key
or mouse event is injected into an application. It cannot establish IME
composition, browser navigation, clipboard behavior or product performance.
`result.json` explicitly leaves those evidence flags false.

## Local smoke (no second computer needed)

Requirements: CMake 3.16+, a C++14 compiler and Python 3.10+. No Qt, OpenSSL,
GUI, driver, administrator service or global package installation is needed.
Run from a dedicated checkout/worktree, not an installed application directory.

macOS/Linux:

```sh
cmake -S tools/pair-test -B build-pair -DCMAKE_BUILD_TYPE=Release
cmake --build build-pair --parallel 1
PAIR_CODEC="$PWD/build-pair/pair-codec" python3 -m unittest discover -s tools/pair-test -p 'test_*.py' -v
python3 tools/pair-test/pair_test.py run --codec build-pair/pair-codec --output /tmp/keystitch-pair-smoke-001
```

Windows PowerShell (Visual Studio C++ tools):

```powershell
cmake -S tools/pair-test -B build-pair -A x64
cmake --build build-pair --config Release --parallel 1
$env:PAIR_CODEC = (Resolve-Path build-pair/Release/pair-codec.exe).Path
python -m unittest discover -s tools/pair-test -p 'test_*.py' -v
python tools/pair-test/pair_test.py run --codec build-pair/Release/pair-codec.exe --output C:/pair-results/smoke-001
```

Use a **new** output directory for each run; existing results are not overwritten.
Exit 0 means passed. Nonzero means failed; read `failure_stage` in `result.json`.
Tests without `PAIR_CODEC` skip native/process coverage; they are not a full pass.
Local mode uses two processes on one host and sets `windows_mac_pair: false`.

## Prepare from CI without a local C++ toolchain

The successful **Record-only paired test runner** workflow also saves
`pair-tools-<OS>-<ARCH>` artifacts containing `pair-tools.tar`. Each tar contains
only the tested codec, the Python agent, fixed fixture and a checksum/build
manifest. Windows uses a statically linked MSVC runtime for this standalone
tool; this does not change the product build or install a redistributable.

Select the successful workflow for the **exact source commit** and matching OS
and architecture. Obtain its artifact through authenticated GitHub tooling;
do not use an unrelated run, untrusted download URL or a checksum as proof of
authorship. Check out that same commit in the dedicated test worktree. Then run
the verifier **from the checkout**, not a downloaded script:

```powershell
python tools/pair-test/package_codec.py unpack --archive C:/pair-download/pair-tools.tar --output build-pair-ci --expected-commit <FULL_COMMIT_SHA>
python tools/pair-test/pair_test.py inspect --codec build-pair-ci/pair-codec.exe --host-id windows
```

The verifier checks the checkout, commit, OS/arch, bounded flat regular files,
file hashes and equality with the trusted agent/fixture **before writing**.
Text identity uses the explicit `sha256-lf-v1` policy: only CRLF line endings
are canonicalized to LF. Each package entry, executable and host-local agent
also retains an exact raw-file hash. Cross-host comparison uses canonical code
identity; a changed host-local file still fails its original raw hash pin.
It rejects links, traversal, duplicates and existing output directories. It
never runs the downloaded codec. After verification, `inspect` validates the
actual executable; generate a host manifest from that local executable path.
On Unix use `python3` and `build-pair-ci/pair-codec` instead. The tar preserves
executable permissions despite the outer artifact ZIP's mode normalization.

This prepares tooling only. It does not establish SSH access, change OS Remote
Login/firewall settings, open a LAN listener or prove a Windows/Mac pair pass.
Real cross-host execution remains a separately authorized step.

## Codex-managed control versus SSH automation

With both hosts connected to Codex, the coordinating chat can request commands
in the existing Windows chat and read its results. This control path can prepare,
inspect and start each host's one-shot tools without enabling an SSH server. It does not make
the hosts share a filesystem or carry the test fixture's TCP traffic.

For a Codex-managed pair run, launch bounded one-shot jobs on each host and
collect their matching run IDs, source fingerprints, complete event traces and
exit results. Do not infer a pair pass from messages saying the jobs were sent.
The `split-prepare`, `split-connect`, `split-serve` and `split-collect` commands support this
split, chat-controlled workflow. The original `run --host-manifest` CLI still
automates remote processes through SSH. SSH is an optional control
transport, not a requirement of the product or of protocol testing. Do not enable
Remote Login/OpenSSH or change firewall rules merely to prepare the test tools.

## One-shot pair using Codex chats (no SSH)

Use the same clean commit and verified native codec on both hosts. Generate fresh
**local** manifests there (`manifest`, without `--ssh-target`) and copy only the
manifest JSON through the coordinating chats. The Mac manifest must use its
specific private LAN IPv4 address for both `listen` and `connect-address`.
Loopback is the safe default and is only valid for a local smoke. The split path
rejects wildcard, public and IPv6 endpoints. It does not change firewall rules.

One host (typically Mac) listens on a temporary prepared port for **both**
fixture directions. The other host (Windows) only makes outbound connections;
it does not need an inbound Python firewall exception. Socket direction is not
fixture direction: direction 0 sends Mac→Windows; direction 1 sends Windows→Mac.
Each direction repeats all three fixture connection phases. No product port or
installed process is reused.

1. Choose a fresh shared run ID. Keep both manifests at their
   actual native paths; peer paths are informational, never executed remotely.
   On Mac, prepare requests in its dedicated worktree **without starting jobs**:

   ```sh
   python3 tools/pair-test/pair_test.py split-prepare --host-manifest mac-host.json --peer-host-manifest windows-host.json --run-id pair-example-001 --direction 0 --timeout 120 --output pair-example-001/prepared-0
   python3 tools/pair-test/pair_test.py split-prepare --host-manifest mac-host.json --peer-host-manifest windows-host.json --run-id pair-example-001 --direction 1 --timeout 120 --output pair-example-001/prepared-1
   ```

   Each prints `prepared` with a path to a new private `request.json`. It briefly
   binds an OS-assigned port, then closes the socket without listening. No agent,
   active job deadline or persistent port reservation remains. The eventual
   listener must fail if that port becomes occupied; it does not choose another
   port or stop the occupant. Privileged and product ports 24800/24801 are excluded.
2. Copy the **exact JSON text** through the trusted coordinating chats into fresh
   files on Windows, before any network job starts. Preserve numeric spelling
   (for example `120.0`); reserializing through another language can change the
   canonical request hash. Verify the printed request hash against the saved file.
   This contains an ephemeral fixture token: do not commit or publish it. No
   token is printed by the CLI, placed in command arguments or saved in reports.
3. On Windows, run the connector against direction 0's staged request:

   ```powershell
   python tools/pair-test/pair_test.py split-connect --host-manifest windows-host.json --request pair-example-001-request-0.json --output pair-example-001/windows-0
   ```

   The request must exactly match Windows' own manifest. Commands and executable
   paths come only from that local manifest/current trusted agent, not a peer
   instruction. Both the wrapper and its child inspect/pin their actual runtime.
   The wrapper prints `started` only after its actual child emits runtime
   preflight. Use a short command yield and report this immediately in the chat;
   a queued message or a launch promise is not a start signal. The connector
   retries until its bounded deadline while waiting for the listener.
4. As soon as that real `started` signal is observed, start Mac's listener:

   ```sh
   python3 tools/pair-test/pair_test.py split-serve --host-manifest mac-host.json --request pair-example-001/prepared-0/request.json --output pair-example-001/mac-0
   ```

   Wait for both jobs' exit results. Repeat steps 3–4 with the already staged
   direction 1 request and fresh `mac-1`/`windows-1` directories. Each job owns and
   cleans up only its own subprocess/socket; timeout, EOF and cancellation are
   failures, not background services. The deadline is at most 120 seconds;
   if chat dispatch takes too long, collect the failure and retry a new run ID.
   Preparation and file transfer do not consume this deadline. `split-listen`
   remains available for fast manual/local workflows, but starting it before
   a slow chat dispatch can let the listener expire before the connector exists.
5. Copy the two complete Windows `host-result.json` reports back through the
   trusted control channel. Collect all four reports into a new directory:

   ```sh
   python3 tools/pair-test/pair_test.py split-collect --run-id pair-example-001 --report pair-example-001/mac-0/host-result.json --report windows-0-host-result.json --report pair-example-001/mac-1/host-result.json --report windows-1-host-result.json --output pair-example-001/collected
   ```

The collector requires matching run/request/scenario hashes, unchanged clean
host pins, actual runtime preflight traces, successful exit/cleanup, complete
event/handshake traces and independently replayed sink state in both directions.
Two same-OS or loopback jobs cannot pass as a real Windows/Mac pair. A deliberate
same-host pipeline test must add `split-collect --local-smoke`; its pair evidence
stays false. Reports are trusted host-produced evidence, not cryptographically
signed remote attestation: do not accept arbitrarily authored report JSON.

Only fixed record-only fixtures travel over TCP. The ephemeral token is not TLS;
use a trusted LAN. Private output directories/files use 0700/0600 on Unix; on
Windows they inherit the containing directory's ACL. Missing reports, delayed
chat dispatch, an inaccessible LAN address or firewall rejection are explicit
failures. Do not work around them by enabling SSH/services, widening listeners,
changing system settings or using native input. A split pass still proves none
of the native IME, browser, clipboard or production behavior described above.

## Actual Windows/macOS pair using an existing SSH controller

1. Put the **same commit** in dedicated checkouts on both hosts and build the
   standalone codec on each. Reconfigure CMake after changing commits: Git
   provenance is embedded at configure time. The native executable hashes will
   differ between operating systems; the source digest and agent hash must match.
2. Choose one controller. It must already have authenticated, non-interactive
   SSH access to the other host (an SSH config alias is supported). This tool does
   not enable Remote Login/OpenSSH, install services, accept unknown host keys or
   save credentials. Use a trusted LAN; the fixture token is not TLS encryption.
3. Generate one host manifest **on each host**. This inspects the actual native
   executable and script and pins their hashes, commit, protocol, OS and arch.
   `listen` is the host's specific LAN IPv4 address; `connect-address` is that
   address as reachable by the other host. Both hosts become listeners in turn.
   The OS firewall must allow the temporary Python listener on that private LAN.

Example with a Mac controller (replace the example addresses and SSH alias):

```sh
# On the Mac, in its dedicated checkout:
python3 tools/pair-test/pair_test.py manifest --codec build-pair/pair-codec --host-id mac --listen 192.168.1.20 --connect-address 192.168.1.20 --output /tmp/mac-host.json
```

```powershell
# On Windows, in its dedicated checkout; win-test is the Mac's existing SSH alias:
python tools/pair-test/pair_test.py manifest --codec build-pair/Release/pair-codec.exe --host-id windows --listen 192.168.1.10 --connect-address 192.168.1.10 --ssh-target win-test --ssh-shell powershell --output C:/pair-results/windows-host.json
```

Copy just `windows-host.json` to the controller using your existing transfer
method. Absolute Python, agent and codec paths inside it refer to **Windows**;
do not replace them with the Mac paths. Then, on the Mac:

```sh
python3 tools/pair-test/pair_test.py run --host-manifest /tmp/mac-host.json --host-manifest /tmp/windows-host.json --output /tmp/keystitch-pair-win-mac-001 --timeout 30
```

Alternatively `--hosts hosts.json` accepts
`{"schema_version": 1, "hosts": [MAC_HOST_OBJECT, WINDOWS_HOST_OBJECT]}`.
Only one input mode (`--codec`, `--hosts`, or two `--host-manifest`) is allowed.
The real-pair mode rejects two local Macs or a declared OS that doesn't match
the inspected host. It verifies hashes **again in each agent** after preflight.

## Results, failures and cleanup

The controller owns all processes and sockets it creates. Agents are one-shot,
stdin-controlled subprocesses, not background services. EOF or cancellation
stops their bounded socket loops; each also has a deadline (maximum 120 seconds).
The controller waits for its known children and records their exit codes. It
never kills another KeyStitch process. A lost SSH channel without the remote
agent's completion record is a failed run, not proven remote cleanup.

Each run writes:

- `result.json`: native build manifests, scenario hash, direction outcomes,
  exit codes, bounded stderr and explicit evidence limits;
- `direction-0-sender.jsonl`, `direction-0-sink.jsonl` and direction 1 equivalents:
  handshake, sequence IDs, fixed events and harness state before/after disconnect.

Host-local monotonic timestamps help reconstruct order; they are **not**
synchronized cross-host latency measurements. Logs contain only fixed protocol
fixtures and tooling paths, not typed text, clipboard contents or credentials.
Treat result directories as private nonetheless (host names/paths are included).

Fault-injection checks are available without touching the installed app:

```sh
python3 tools/pair-test/pair_test.py run --codec build-pair/pair-codec --fault missing_up --output /tmp/keystitch-pair-fault-001
```

The injected faults are `missing_up`, `duplicate_up`, `wrong_key`, `out_of_order`,
`peer_exit`, `handshake_timeout`, `leak_state` and `bad_result`. Each must exit
nonzero. Automated tests also check cancellation, frame bounds, invalid messages,
manifest mismatch, forged state traces and failed log writes.

## Next boundary: actual product and native behavior

Do not add native input to this record-only default. Build three separate lanes:

1. **Production headless integration:** reuse real Client/Server sessions and
   replace only the platform sink. Check product handshake, remapping, reconnect
   key-state cleanup and clipboard contention, with no physical capture or UI.
2. **Opt-in OS tests:** dedicated temporary app/agent builds, explicit permission
   and isolated test input windows. Include Win→Mac and Mac→Win, and distinguish
   physical mouse buttons from semantic back/forward commands.
3. **Opt-in IME acceptance:** native text field, Chrome/Safari input/textarea/
   contenteditable, actual Hangul composition and English strings, both directions,
   with/without an active composition. Compare menu selection, local OS shortcut
   and remote Right Alt; a changed menu icon is never the acceptance criterion.

Keep daily-use and test builds on separate paths/configs/ports, pin each host's
actual binary, and collect one run ID across both sides. Automate lanes 1 and
record-only coverage in CI; reserve user interaction for the OS-dependent checks
that genuinely require it. Gureum should be an optional compatibility case, not
a required dependency of the product.
