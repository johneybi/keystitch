# Bidirectional macOS IME switching — 2026-09-14

## Scope

This source-only change removes the rejected explicit-Gureum-han2
reselection experiment from the F19 path. Windows Korean-layout Right Alt is
still remapped to a tap of F19; the macOS client now resolves F19 through the
existing input-source policy with offset == 1.

The policy classifies the current source and selects the opposite language
family. Apple Korean/ABC are the supported baseline. Gureum preferences are
read-only, and a Gureum mode is used only when it is present, enabled and
selectable. No input source is silently enabled, no fixed source ID is forced,
and no Unicode or clipboard injection is used.

## Evidence and limits

The Windows tap/hold path, protocol packet and existing modifier-side fix were
already covered by offline tests and are preserved. The rejected experiment
was deterministic source-wise: PickOSXHangulReselectTarget always returned
Gureum han2, so it could not implement Roman → Hangul and Hangul → Roman
switching. The new F19 path cannot call that one-way helper.

The pure policy tests now include both Apple directions. They do not prove
TIS, IMK, Safari, Chrome, composition completion or actual committed text.
Those require user-approved live validation after a candidate app is built;
this change does not install, launch or replace an app and does not modify
input-source settings.

## Follow-up reproduction — 2026-09-14

After the candidate had appeared to work during ordinary use, the user
observed a recurrence in a Chrome ChatGPT input field: Windows Right Alt
changed the macOS menu-bar language indicator, but subsequent real typing was
English only. Selecting Gureum's Hangul mode directly from the macOS input
menu in that same field restored Hangul input. This is an observation from the
user's physical keyboard, not from synthetic test characters.

The existing client log records repeated
`F19 requested bidirectional macOS input-source switch (foreground IME state
unverified)` messages around the recurrence. Those records prove receipt of
the remapped tap and entry into the selection path only; they do not prove
that the foreground application's IMK session, selected input mode,
keyboard-layout override, or active composition became ready. A `defaults`
snapshot of the selected source is not treated as authoritative when it
disagrees with the user's visible menu state.

The direct-menu path remains a separate comparator: it may change focus or
composition state in addition to selecting an input source. The recurrence
therefore does not establish that TIS selection, Gureum, or Chrome alone is
the cause. The next diagnostic should log only non-content state before and
after F19: foreground application identity, selected input-source ID and
input-mode/layout IDs, keyboard-layout override state, modifier/event state,
and activation confirmation. Do not collect typed text or clipboard data.

## Verification boundary

Before release, validate actual strings—not the menu-bar label—in native text,
Safari and Chrome fields, in both directions and with/without an active
composition. Also test remote-to-local and local-to-remote switching. If live
validation fails, retain the installed app and restore only through the dated
backup procedure after explicit approval.
