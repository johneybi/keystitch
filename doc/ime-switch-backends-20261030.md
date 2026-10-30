# Explicit F19 switching backends — 2026-10-30

## Scope

This source-only increment follows `26025a20`. It adds an **experimental,
opt-in** native-shortcut backend to the real macOS F19 dispatcher. Direct TIS
selection remains the default. No installed app, input source, shortcut setting
or Windows configuration is changed. No app is launched and no live key event
is posted by the validation below. This is not a verified Chrome/Safari fix.

## Selection and behavior

`INPUTLEAP_MACOS_IME_BACKEND` is read once when `OSXKeyState` is constructed:

- unset, empty or `direct-tis`: prior selection policy, activation polling and
  75 ms settling interval; Gureum preferences remain read-only;
- `native-shortcut`: request the configured **previous input source** shortcut;
- any other value: reject F19 without sending events or selecting a source.

This is a per-process experimental setting, not a GUI preference. It has not
been enabled in the current app. Do not use `launchctl setenv`, alter global
shell startup files or start a second production client to try it. A future
user-approved candidate session must explicitly select its backend before
launch and preserve the installed daily-use app/configuration for recovery.

F19 up/repeat are consumed in both backends. There is **no automatic fallback**
between backends, to another shortcut action or to a guessed Control-Space.
Ordinary key delivery is unchanged. Separate relative-group requests still use
the existing TIS adapter; absolute layout overrides are also unchanged. The new
choice is F19-specific, not a rewrite of all keyboard-layout handling.

### Experimental shortcut lookup

Only after a native-backend F19 request, production code reads
`AppleSymbolicHotKeys` from `com.apple.symbolichotkeys` through CFPreferences.
It interprets entry `60` as the previous-input-source action. **That identifier
and preference shape are undocumented compatibility assumptions**, not a public
Apple action API. Missing/disabled entries, wrong types, malformed arrays,
non-integer/out-of-range values and unsupported chords are rejected. No source
or shortcut is silently enabled. Action `61` is not used as a fallback.

The parsed chord must additionally appear enabled in `CopySymbolicHotKeys`.
The SDK's `CarbonEvents.h` explicitly says that API's dictionaries have no
action identity. Thus a matching chord is a sanity check, **not** proof of its
semantic action, OS acceptance, active source or foreground IME readiness.
Preference/public-list snapshots can also race with settings changes. This
undocumented mapping needs live verification before any default/release use.

The preference uses Quartz modifier flags while the public list uses Carbon
flags. Conversion is explicit and tested; the two numeric masks are not compared
directly. A primary implementation reference for the public-list representation
is [MonitorControl's KeyboardShortcuts source](https://github.com/MonitorControl/MonitorControl/blob/main/MonitorControl/Support/KeyboardShortcuts.swift).

Supported experimental chords are F1–F20 (with supported modifiers), or Space
with Control, Option or Command (optionally Shift). Bare/Shift-Space,
text-generating keys, modifier/lock keys and Fn/Globe flags are rejected.
If the configured chord is F19, it is posted through the native batch adapter,
not recursively through the semantic F19 handler. This is not raw passthrough
of every remote F19.

### Posting and limits

Held synthetic modifiers block the native request rather than being forcibly
released. This does not add arbitration with locally held hardware modifiers;
future live comparison should start with all keys released.

The data-only planner makes balanced modifier-down, shortcut down/up and
reverse modifier-up events. It does not mutate held-key bookkeeping. Production
allocates the entire chord with a private CGEvent source **before** posting any
event, so allocation failure cannot post half a prepared chord. This is not an
atomic OS transaction: process interruption or OS rejection can still occur.
`CGEventPost` provides no acceptance/composition acknowledgement. A successful
adapter return means only that posting was requested. The historical 75 ms wait
is retained for a successful request, not treated as an IME acknowledgement.

The native backend uses OS input-source history/behavior. With multiple input
sources it does not promise the same opposite-language-family selection as the
direct-TIS policy, and neither path proves a committed Hangul/English string.
No keyboard driver, Karabiner dependency, Unicode injection, clipboard paste,
focus manipulation or foreign IMK-state reset is introduced.

## Offline verification

New OS boundaries for shortcut lookup/posting and backend selection are stubbed
in dispatcher tests. Property-list tests construct only in-memory CF objects;
they do not call CFPreferences, CopySymbolicHotKeys, TIS or CGEvent posting.
Native fixture tests deliberately keep the source unchanged after a posting
request: they do not fabricate activation/composition success.

- Added 9 data-only tests for backend defaults, strict preference parsing,
  disabled/malformed input, public registration/mask conversion, balanced
  chords, all 16 function-key modifier combinations and held-key rejection.
- Added 8 dispatcher tests for native routing, repeat/up consumption, lookup/
  unsupported-chord/posting failures without TIS fallback, held modifiers,
  configured F19 dispatch, invalid backend, unchanged relative-group handling,
  and 32 Right Alt plus 32 Hangul tap/first-key cycles through the real remapper.
- `unittests` and `input-leapc` build successfully, single-job. A first sandbox
  build could not set nice priority; subsequent builds used approved nice level
  15. Existing deprecated API/duplicate-library warnings remain.
- Focused suite: **73/73 passed**.
- Full suite: **209/211 passed**. Only the already recorded
  `SecureUtilsTest.FormatSslFingerprintHexWithSeparators` and
  `SecureUtilsTest.CreateFingerprintRandomArt` fail. No all-green full-suite
  claim; no related implementation/test/fixture was changed.
- Negative controls: adding forbidden TIS fallback made the native failure
  test fail; changing the default to native made the backend-default test fail.
  Both mutations were restored before the normal build/test run.
- Preference-reading/public-list adapters and real CGEvent allocation/posting
  were compiled and reviewed, **not executed or OS-fault-injection tested**.
  Neither OS acceptance nor failure recovery is established by inert mocks.

Build flags are in the [previous increment](ime-f19-dispatch-offline-20261030.md).
Reconfigure after adding the new `OSX*.cpp` files so CMake globs include them.

```sh
nice -n 15 cmake --build build-f19 --parallel 1 --target unittests input-leapc
nice -n 15 build-f19/bin/unittests --gtest_filter='OSXF19DispatchTests.*:OSXInputSourceShortcutTests.*:OSXKeyStateTests.*:OSXInputSourceSelection.*:KeyRemapPipelineTests.*:KeyRemapperTests.*' --gtest_output=xml:build-f19/backend-focused-results.xml
```

Reports and binaries remain in ignored `build-f19`. Installed client SHA256
remains `b2cec6806eb3de1da75195833274f909c8480b39cd4cd3f50596e6ee21830b24`.

## Next approval gate

Do not install/launch the candidate or send keys as an implicit follow-up.
After explicit approval, confirm the configured shortcut in System Settings,
compare each backend in disposable native/Chrome/Safari fields, both directions,
with/without active composition, and compare actual strings/marked state. Start
with built-in Korean/ABC; Gureum remains an optional compatibility case.
If baseline typing fails, stop instead of sending more toggles. Keep the prior
app/configuration and selected-source state available for controlled recovery.
