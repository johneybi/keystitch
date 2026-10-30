# F19 function flag and live comparison preparation — 2026-10-30

## Correction to the original candidate

The user authorized applying the candidate and comparing both F19 backends in
the same field. They explicitly declined changing their macOS shortcut. No
input-source shortcut, input-method preference or Windows setting was changed.

The original candidate rejected the actual stored action-60 chord. Its values
are enabled=true, parameters=[65535, 80, 8388608], type=standard. Interpreting
that stored function flag as a requirement to press physical Fn plus F19 was
inaccurate: System Settings displays **F19**, not Fn+F19.

Read-only host checks established:

- `CopySymbolicHotKeys` returns an enabled key 80 with Carbon flags `0x20000`
  (`kEventKeyModifierFnMask`); key 79 has the same mask but is disabled.
- Creating, without posting, F19 down and up events with a private CGEvent
  source produces Quartz flags `0x20800000`, including SecondaryFn.
- The first probe under the command sandbox could not access the hotkey
  service (`-108`). The explicitly approved unsandboxed read succeeded. That
  first failure is not evidence that the installed client lacks registration.

The correction allows the stored SecondaryFn flag **only for F1–F20** and maps
it to the Carbon Fn mask for registration checking. It preserves the flag on
the shortcut down/up pair. It does not synthesize a standalone Fn/Globe press,
set a held modifier, strip the stored mask, relax exact registration matching,
or enable unsupported Fn+Space/text shortcuts. Ordinary modifier events retain
their existing flags and ordering; the maximum plan size remains ten events.

This follows the SDK's distinction between device-independent modifiers and
special key identifiers in `CGEventTypes.h`. The public
[SecondaryFn flag documentation](https://developer.apple.com/documentation/coregraphics/cgeventflags/masksecondaryfn)
describes the flag; it does not establish that posting it will activate an IME.
The observed Settings display and generated-event flags above are host
observations, not a claim about every macOS version or Fn/Globe combination.

## Verification

- Three new regression tests fail against the original implementation (3/3).
- After correction, the focused suite passes **76/76**.
- Full suite: **212/214 pass**; only the previously recorded SecureUtils
  fingerprint formatting/random-art tests fail.
- Tests cover the observed preference and Carbon mask, rejection of a missing
  or Quartz registration mask, all 16 ordinary-modifier combinations with the
  function flag, no standalone Fn events, no recursive semantic F19, repeat/up
  consumption and an ordinary following letter without leaked flags.
- A separate read-only probe using the production parser/registration/planner
  accepts the current real preference and public list: key=80, flags=0x800000,
  two planned events. It posts no events and makes no TIS selection.

Offline success and a valid registered chord do not prove actual text input.
The real remote path still needs a Windows keyboard and visible committed
Hangul/English strings. macOS automation that pastes text or presses keys
locally is not a substitute for that path.

## Installed candidate and recovery

Before changing the app, the existing bundle and preferences were preserved at:

`~/Library/Application Support/InputLeapKeymap/Backups/20261030-ime-backends-c61708c5/`

`original.app` is the verified backup; `original-at-swap.app` is the untouched
original moved out of `/Applications` during replacement. Original client
SHA256: `b2cec6806eb3de1da75195833274f909c8480b39cd4cd3f50596e6ee21830b24`.
The directory also contains the Input Leap preferences and exported symbolic
hotkey/HIToolbox settings as references. Do not wholesale restore unrelated
shortcut preferences merely to undo this experiment.

The first installed candidate used client revision `c61708c5`, preserving the
existing GUI, server and bundled frameworks. It passed strict/deep signature
verification and reconnected with one GUI and one client to the unchanged
Windows endpoint. Its child environment was confirmed as `direct-tis`.
The initial client candidate SHA256 after bundle signing was
`d0f31a715db5f2d7a94d4270fd0a2ab23e42665f4ab99e7aa37be4d93c13e31e`.
The function-flag correction requires a subsequent client replacement.

Candidate metadata uses `KeyStitchClientSourceRevision`: only the client is
rebuilt, so the unchanged GUI/server must not be described as built from the
new revision. Preserve the bundle identifier, existing endpoint, client name
and connection settings. Switching backends is per-launch using
`open -a '/Applications/Input Leap Keymap.app' --env INPUTLEAP_MACOS_IME_BACKEND=...`
after confirming the previous GUI and child have exited. No global environment
or shortcut setting is required. Never run both clients simultaneously.

## Comparison protocol

Keep one disposable browser input field open across the A/B restart. An
isolated local page in ignored `build-f19/live-comparison/index.html` provides
a contenteditable field and bounded event-type/composition-state observations.
It has no external resources, storage, transmission or input-source/key
injection. Its A/B label is manual, not evidence of the executing backend.

1. Confirm the new client revision, exactly one instance, connection and actual
   per-process backend. Stop if built-in baseline typing is broken.
2. Observe the user's current ABC/Gureum setup without silently replacing it
   with Apple Korean. That comparison is a Gureum compatibility case, not
   evidence for the untested Apple Korean baseline.
3. Start from ABC selected with the mouse. In the same field, use the Windows
   keyboard: Right Alt, `rksk`, Right Alt, `abc`; expect `가나abc`. The second
   toggle occurs with active Hangul composition. Repeat three times.
4. Repeat with Space before the second toggle to distinguish committed from
   active composition. Check both switch directions and built-in keyboard use.
5. Record visible output, composition/input events and F19 backend diagnostics.
   For B, require the native log to say key=80, flags=0x800000, no fallback.
6. Compare A and B in the exact same field, then separately in actual Chrome
   ChatGPT and Safari fields without submitting messages or searches.

If baseline fails, source/menu diverges or a chord is unavailable, stop and
record the evidence. Never claim a fix based on menu state or a posted-event
log alone. A/B results remain pending until the user performs the real remote
keyboard steps; this document does not pre-record them as successful.
