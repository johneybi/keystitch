# Superseded: explicit han2 reselection experiment

This document is historical. The one-way F19 behavior described here is
rejected and is not the product policy on the current development branch.

Branch: codex/ime-hangul-reselect-experiment, based on ad72bf9f.

The old experiment made F19 request exactly
org.youknowone.inputmethod.Gureum.han2 on each non-repeat down event, including
when han2 is already current. It is not a toggle. Up and repeat remain consumed.
If han2 is unavailable there is no fallback, no automatic enablement, and no
synthetic F19. Use the menu bar to select Roman mode during the experiment.
Other group-switch requests retain the preference-aware policy.

The purpose was to compare explicit API reselection with the user's successful
mouse menu selection, in the same failed input field. It is not established
that TIS reselection invokes the same IMK lifecycle or focus changes as a menu
selection. A successful return/current source ID does not prove input recovery.
There is no forced ABC round-trip, focus manipulation, qwerty activation,
Gureum replacement or Windows change.

Compared to the currently installed cd1086ee build, this includes the previously
reviewed modifier-side fix and selection diagnostics/preferences commits. This
is not a byte-minimal experiment against that old binary; note this when
interpreting results. All connection stability changes are retained.

Historical validation: build GUI/client/server/tests using one job; run focused tests,
including fixed han2 selection, unavailable/disabled/nonselectable han2 with no
fallback, and duplicate candidates. Review that the OS adapter does not skip
the final TISSelectInputSource call when current and target IDs match.

Deployment: copy the existing app and preferences to a new dated backup,
verify the copy, package and verify signatures, stop only the verified old GUI
and client, swap the bundle, then verify one new GUI/client and connection.
Never send test characters to the user's documents. Ask the user to press
Right Alt in a problematic input field and report actual Hangul insertion.
If ineffective or regressive, restore the saved app; no input-method settings
need restoration because this experiment does not change them.

## Deployment record (2026-09-14)

The staged bundle was installed at `/Applications/Input Leap Keymap.app` after
the previously installed client was stopped. The previous bundle was preserved
at:

`/Users/esk/Library/Application Support/InputLeapKeymap/Backups/20260914-han2-6bb67bfa/original-at-swap.app`

The installed bundle reports version/build `3.0.3-han2-reselect-6bb67bfa`,
bundle ID `com.johneybi.input-leap-keymap.client`, minimum macOS `26.0`, and
arm64. Deep/strict code-signature verification passed. The installed client
binary SHA-256 is `ded9015757b1b3c72adcbafa1c8a1ace246039183e9a8716827e33c6100f1596`.
The preserved pre-experiment client binary SHA-256 is
`162e72fc24a094f8387b6a84fdb0cd4abfce8408fd1ee73481ea117b8eceebaf`.

Post-launch validation observed one GUI and one bundled client, both running
from the installed bundle, with the client connected to the configured server.
The same process pair and established connection remained after a short
observation interval; no immediate restart loop was observed. The staged path
was consumed by the swap, and the original bundle remains recoverable in the
dated backup above. Preferences were exported but not changed.

This does not validate foreground IME recovery. No automated text or key input
was sent, and no claim is made about Safari, Chrome, or any other input field.
Final experiment evidence is pending the user's manual check in a problematic
field: press Windows Right Alt once, then report whether actual Hangul text is
inserted. During the experiment, select Roman/English from the macOS menu bar
when needed.
