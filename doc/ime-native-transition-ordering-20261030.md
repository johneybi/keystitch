# Native shortcut transition ordering — 2026-10-30

## Trigger and scope

The previous [A/B comparison](ime-f19-function-flag-live-20261030.md) found that
native switching works on individual attempts, but rapid typing can put the
first Roman character in the old Hangul composition. The user chose to develop
B while retaining their existing Mac input-source shortcuts. They also report
that the built-in keyboard feels faster; that is useful observational evidence,
not a measured hardware-to-browser latency comparison.

The previous B implementation posted a chord and blocked its caller for 75 ms
without observing a source change. Remote Right Alt additionally goes through
the server's existing tap/hold remapping and network delivery. The tap action
is emitted on key-up; overlapping another key can be classified as hold. None
of those server rules or Windows settings is changed here.

This increment changes only B's following-key ordering on the Mac. The existing
F19 preference, function-flag handling and native CGEvent batch are preserved.
No TIS selection, guessed shortcut, Gureum preference write, Unicode injection,
clipboard paste, focus manipulation, driver, new thread or extra client is added.
The compiled backend default remains direct-TIS; the live candidate is launched
explicitly as B. Its GUI, server and connection settings remain the installed
baseline; only the client is rebuilt.

## Behavior

- Before posting, read the selected input-source ID on the main thread and
  establish one event-queue poll timer. Missing source/timer or a failed post
  rejects the request without switching to another backend.
- After posting, return immediately instead of sleeping 75 ms. Following
  mapped keystrokes, including modifier changes, releases, repeats, group
  changes and another F19, enter one FIFO in their original order.
- Poll selection at most every 4 ms while waiting. A changed, nonempty ID must
  remain observed for 20 ms before draining the FIFO. Invalid/missing or reverted
  observations reset that stability window. Another queued toggle starts a new
  barrier only after the characters preceding it have been delivered.
- The 20 ms interval is an experimental guard, **not an IMK acknowledgement**.
  Fast observations may release input in about 24 ms rather than always paying
  75 ms; no fixed hardware-to-app performance guarantee is claimed.
- At 300 ms without a stable change, release the original keys with an explicit
  `timeout-unconfirmed` warning. At 256 queued keystrokes, use the same explicit
  unconfirmed-release policy. Do not silently discard characters, leave input
  waiting indefinitely, or select TIS as a fallback. These exceptional releases
  may still type in the old language and must not count as successful switching.
- EventQueue services ordinary timers only when its buffer is empty. Therefore
  new keys and remote pointer traffic also check the active barrier, preventing
  sustained traffic from starving it. No global EventQueue behavior is changed.
- Before a remote click or scroll, drain pending keys/toggles ahead of that
  pointer event with a `pointer-action-unconfirmed` warning if necessary. This
  preserves event-stream ordering instead of carrying old-field characters
  behind a click. Unrelated focus changes and IME IPC are not controllable here.
- Screen-leave/all-keys-up cancels the timer, discards undelivered keystrokes and
  releases only previously posted remote downs. A buffered up may already have
  cleared KeyState's logical count, so previously posted downs are separately
  tracked. Never synthesize a release for an undelivered down. Destruction
  removes the timer; normal screen teardown uses all-keys-up for key cleanup.

The 300 ms limit is a polling-policy deadline, not a guarantee under an OS/main
thread stall. TIS is not thread safe according to the installed SDK, so source
reads retain the existing main-thread dispatch rule. Main-thread scheduling,
OS API calls or an already blocked event loop can delay observation. A selected
source ID never proves that the foreground app committed its old composition.

## Diagnostics

Content-free native logs correlate a switch sequence, preference/read/post
duration, posted monotonic/epoch timestamps, first observed changed source,
barrier release reason/queue size, and the first following nonmodifier down.
No typed characters or keycodes are added to these transition diagnostics.
They establish request/selection/posting order, not browser text acceptance.

A disposable page at `/transition.html` on the existing loopback-only lab
server records event types, physical code, composition state and timestamps
only inside its editable field. It injects no keys or source changes. The
keyboard label is manual; trusted-event flags alone cannot prove hardware
origin. It retains up to 200 event records and ten local result snapshots.
The old A/B page remains loaded separately; it is not reloaded or cleared.

## Offline validation

- Client and unit-test targets build successfully, one job at nice level 15.
- Focused suite: **92/92 pass**.
- Full suite: **228/230 pass**. The two unchanged failures remain
  `SecureUtilsTest.FormatSslFingerprintHexWithSeparators` and
  `SecureUtilsTest.CreateFingerprintRandomArt`.
- Added five pure transition-policy cases and eleven dispatcher/timer cases.
  Coverage includes a source still unchanged after the old 75 ms, early source
  change, flapping/missing sources, a late change at the hard deadline, both
  directions, rapid bursts and consecutive switches, repeats/modifiers/releases,
  timer starvation, queue limits, pointer ordering and screen-exit cleanup.
- A real in-memory EventQueue drives and cancels the production timer adapter;
  source reads and key posting remain inert. It proves timer wiring/cleanup,
  not TIS or IME integration.
- Negative control: bypassing FIFO deferral makes the burst-ordering test fail.
  The mutation was restored and normal binaries/tests rebuilt afterward.
- Only the existing IOMasterPort deprecation and duplicate-library warnings
  remain. `git diff --check` passes.

Real Chrome/Safari and built-in/Windows keyboard results for this increment
must be recorded after installation. The earlier B failures are not replaced
by these offline results. Compare the same disposable field, both directions,
without committing Hangul before switching, at normal and rapid typing speeds.
If the built-in baseline fails, stop; do not recover by changing Mac shortcuts.
