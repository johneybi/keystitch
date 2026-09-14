# macOS input-source selection policy (pure helper + read-only Gureum prefs) — 2026-09-13

Source-only change. No app launch, install, live-input, event-tap, or
preference-write was performed in this work. The installed
connection-stability build is untouched.

## What changed

- New pure helper `src/lib/platform/OSXInputSourceSelection.{h,cpp}`:
  `PickOSXInputSourceTarget(candidates{id,enabled,selectable}, currentId,
  preferredRomanId, preferredHangulId, offset) -> {targetId, reason}`.
  No TIS/CF/preferences/sleep/event calls inside.
- `OSXKeyState::cycleInputSource` now:
  - enumerates all keyboard input sources with `enabled && selectable`
    (single `TISSelectInputSource` on the final target only),
  - reads `org.youknowone.Gureum` `LastRomanInputMode` /
    `LastHangulInputMode` via `CFPreferencesCopyValue` read-only, with
    ownership release, `CFString` type check, and empty-value handling
    (missing/type-mismatch never crashes; degrades to historical fallback),
  - delegates `offset == 1` forward policy to the pure helper,
  - keeps activation polling/settling and returns on selection failure
    without chaining or settings writes.
- Tests: `src/test/unittests/platform/OSXInputSourceSelectionTests.cpp`
  (pure helper only; no TIS/event writes). CMake globs
  (`OSX*.cpp` in `src/lib/platform` and `src/test/unittests/platform`)
  pick up the new source/test with no CMake edit.

## Policy

- Candidates: only `enabled && selectable` are usable; duplicates are
  deduplicated explicitly (first-seen order, usable if ANY entry is
  enabled+selectable). Empty list, missing current ID, and same-reselection
  all return diagnosable reasons.
- `offset == 1` forward:
  - current Hangul (exact Gureum Hangul set or Apple Korean) → prefer Roman;
    otherwise → prefer Hangul.
  - Preference honors exact registered IDs only (no prefix matching):
  Roman = `{Gureum.system, qwerty, dvorak, colemak}` + `ABC`;
    Hangul = `{Gureum.han2, han2classic, han3classic, han3final,
    han3finalnoshift, han3layout2, han3noshift, han390, hanahnmatae,
    hanroman, han3-2011, han3-2012}` + Apple Korean. The preference itself
    must be a Gureum ID; Apple IDs are fallback candidates, not accepted
    preference values. The Hangul list is an exact registration list, not a
    prefix rule.
  - Preference empty/unregistered/wrong-direction/not-found/disabled/
    nonselectable → NO auto-activation; historical fallback order is kept:
    Roman `{Gureum.system, ABC}`, Hangul `{Gureum.han2, Apple Korean}`.
    Disabled is called out explicitly in `reason`
    (e.g. `forward-roman-fallback:...:pref-disabled:<id>`).
- Reverse/general-offset cycling is preserved verbatim
  (`((current+offset)%n+n)%n`, `reverse-cycle:`/`cycle:` reasons).

## Known current behavior

- If Gureum `qwerty` is disabled (or not selectable), the Roman preference is
  rejected with a `pref-disabled`/`pref-nonselectable` reason and the
  historical fallback remains — typically `Gureum.system`, else `ABC`.
  So **ABC fallback can remain while qwerty stays disabled**. This is
  intentional (no silent activation) and is covered by tests.

## Future validation procedure (NOT executed; requires user approval)

Do NOT run until the user explicitly approves. Do not claim this fixes the
root cause; it only makes the preference policy explicit and testable.

1. User enables `qwerty` in System Settings → Keyboard → Input Sources.
2. Confirm `qwerty` reports `enabled && selectable` (diagnostic log line
   `macOS input source candidate id=... selectable=yes enabled=yes`).
3. Apply the newly built app (replace installed app only after approval).
4. Record actual strings in BOTH directions, each in three clients and two
   composition states:
   - no-composition (committed text) vs composition (marked/underlined text),
   - native app (e.g. TextEdit), Safari (plain textarea / contenteditable /
     address bar), Chrome (same three fields),
   - Hangul→Roman and Roman→Hangul.
   Capture the exact committed/composing strings, not just the selected
   source name.
5. On any problem, roll back to the previous app AND the previous
   input-source state (re-select the source that was active before the test,
   restore removed/disabled sources only if the user approves).

No composition-finalization claim is made: selecting a source does not prove
the foreground application's IME session switched.

The Windows Right Alt F19 target uses this same offset == 1 policy. It is
therefore bidirectional: an Apple Korean source selects a usable Roman source,
and ABC (or another usable Roman source) selects a usable Hangul source. F19
does not hard-code Gureum han2 or reselect a Hangul source while already in a
Roman source. Gureum remains optional; Apple Korean and ABC are the baseline.
