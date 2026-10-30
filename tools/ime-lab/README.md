# Isolated IME investigation (2026-09-12)

This directory does not change Input Leap's keyboard implementation. It provides
a reviewable Gureum patch candidate, an executable regression and a separate
manual macOS/WebKit test surface. Do not interpret a green unit test as a fix for
Safari, or install a new input method based on these results alone.

## State regression: actual upstream method bodies

The installed Gureum reports version 1.13.2. Its upstream tag resolves to
`19e2778af80dac378603e8862208d60b2c8dc01d`. This is a version match, not a proof
that the installed executable is byte-identical to an upstream build.

`test_gureum_state.py` accepts the upstream `OSXCore/InputReceiver.swift`, verifies
its SHA-256 (ignoring trailing newlines only), extracts the actual `input2`,
`input(text:...)` and `commitCompositionEvent` methods and compiles them with
Swift against inert collaborators. It then applies the checked-in candidate
patch to a temporary copy, checks applicability, and runs the same assertions.
The production source and installed Gureum are not modified.

```sh
fixture_dir=$(mktemp -d /private/tmp/keystitch-gureum-fixture.XXXXXX)
curl --fail --location \
  https://raw.githubusercontent.com/gureum/gureum/19e2778af80dac378603e8862208d60b2c8dc01d/OSXCore/InputReceiver.swift \
  --output "$fixture_dir/InputReceiver.swift"
python3 tools/ime-lab/test_gureum_state.py "$fixture_dir/InputReceiver.swift"
```

Requires macOS command-line tools (Swift/AppKit), Python 3 and git. Network is
used only to obtain the public upstream fixture. The test runner is offline.

### Result

Verified on 2026-09-12:

- Original: 12 of 28 assertions fail (expected regression demonstration).
- Candidate: 28 of 28 assertions pass.
- `git apply --check` passes on the fixed upstream commit.
- Cases: Control, Command, nil text, composer-requested commit and ordinary
  text, with both initial processing states. Each checks restoration and the
  subsequent external-commit cancellation branch.
- Extended coverage invokes the actual extracted input method recursively from
  an inert insertion callback. Both normal and early-commit outer paths preserve
  the outer processing scope and insert the fixture commit exactly once. This
  supersedes the initial 20-assertion test; it does not establish that Safari
  exercises the same callback sequence.

The original early `.commit` return leaves `inputting` true. The candidate uses
`defer` to restore the entry value on every exit from the processing scope,
including preserving a pre-existing true state. The latter is a defensive
reentrancy invariant, not evidence that nested input causes the reported bug.

**Limits:** collaborators do not emulate real IMK IPC, composition buffers,
candidate windows, Safari, or WebKit. The test demonstrates a control-flow
defect and its local correction, not lost-text reproduction or end-to-end
recovery. It does not cover other Gureum methods that also mutate `inputting`.
The patch must be reviewed and tested in Gureum's real build before deployment.

## Manual test surface

```sh
sh tools/ime-lab/build.sh
```

The builder prints a unique app path under `/private/tmp`. It compiles and
ad-hoc signs the app without installing or launching it. Open the printed app
manually when ready. Close its window or use Command-Q to exit.

The window contains an NSTextView and two WKWebView controls (`input search`
and `textarea`). It shows at most 200 in-memory event records: native marked
state, insertion length, web composition/input event type and UTF-16 lengths.
It never records text contents, injects keys, changes TIS sources/preferences,
observes other apps, or requires Accessibility/Input Monitoring. Web storage
is ephemeral, external navigation is blocked and the page has no network
resources. This is not Safari itself and a pass here does not guarantee Safari.

Use only disposable test text. Start each comparison from a known working
state. Compare separately:

| Toggle keyboard | Typing keyboard | Purpose |
| --- | --- | --- |
| Built-in | Built-in | Baseline |
| Built-in | Windows | Ordinary remote key delivery |
| Windows | Built-in | Remote transition affecting local typing |
| Windows | Windows | Complete remote path |

For each control, test both empty composition and a Hangul syllable still being
composed before switching to Roman and typing `abc`. Record visible output and
whether composition ends; do not accept menu-bar state alone as success.
If the baseline stops working, stop comparison rather than sending more toggles.

Build and signature verification succeeded on 2026-09-12. A launch request
succeeded, but automated visual inspection was unavailable because computer-use
access to the new app was not approved. Layout and real IME behavior therefore
still need manual verification.

The user subsequently deferred all live tests until explicitly requested. Do
not launch this app or inject keys as a follow-up without that request. A process
check after deferral found no instance of the generated lab executable running.
Installed Input Leap/Gureum and their preferences remain unchanged.

The embedded JavaScript can be checked without a browser:

```sh
node tools/ime-lab/test_web_contract.cjs
```

This runs the actual embedded script with inert DOM collaborators. It checks
all seven registered events in both controls (14 cases), absence of text
contents in the recorded payloads, and no messages on initialization. It does
not check rendering, actual browser event order or IME behavior.

Additional offline checks on 2026-09-12: shell syntax, Info.plist validation,
Swift build and ad-hoc signature verification passed. The existing
`build-stability/bin/unittests` ran 26 targeted tests successfully using
`ClientRestartTests.*:ClipboardSenderTests.*:KeyRemapperTests.*:OSXKeyStateTests.*`.
At the initial lab-only commit no production source changed, so this reused the existing test binary rather
than claiming a fresh full Input Leap build or a complete-suite pass.

See [offline review](../../doc/ime-offline-review-20260912.md) for the subsequent
source fix and expanded Input Leap pipeline tests, still without installation.

## Next decision

The [offline F19 dispatcher increment](../../doc/ime-f19-dispatch-offline-20261030.md)
extends the real remapper/key-state path through the macOS F19 handler with
inert OS boundaries. Its tests do not launch this lab or validate real IME text.

If the defect reproduces in Gureum's real input-controller tests, evaluate the
candidate there first. If remote toggling fails with native and web controls
even after that fix, trace Input Leap's group overrides and event ordering.
Do not combine changes to input-source policy, modifier synthesis and IME state
in a single experimental release. Preserve the installed connection-stability
fixes and avoid automatic direct-TIS fallbacks that obscure which path ran.
