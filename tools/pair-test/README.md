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
in the existing Windows chat and read its results. This control path can prepare
and inspect each host's tools without enabling an SSH server. It does not make
the hosts share a filesystem or carry the test fixture's TCP traffic.

For a Codex-managed pair run, launch bounded one-shot jobs on each host and
collect their matching run IDs, source fingerprints, complete event traces and
exit results. Do not infer a pair pass from messages saying the jobs were sent.
The current `run --host-manifest` CLI automates remote processes through SSH;
it does not yet automate split, chat-controlled jobs. SSH is an optional control
transport, not a requirement of the product or of protocol testing. Do not enable
Remote Login/OpenSSH or change firewall rules merely to prepare the test tools.

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
