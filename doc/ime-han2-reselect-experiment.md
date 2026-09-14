# Explicit han2 reselection experiment

Branch: codex/ime-hangul-reselect-experiment, based on ad72bf9f.

F19 (the existing Windows Right Alt tap target) now requests exactly
org.youknowone.inputmethod.Gureum.han2 on each non-repeat down event, including
when han2 is already current. It is not a toggle. Up and repeat remain consumed.
If han2 is unavailable there is no fallback, no automatic enablement, and no
synthetic F19. Use the menu bar to select Roman mode during the experiment.
Other group-switch requests retain the preference-aware policy.

The purpose is to compare explicit API reselection with the user's successful
mouse menu selection, in the same failed input field. It is not established
that TIS reselection invokes the same IMK lifecycle or focus changes as a menu
selection. A successful return/current source ID does not prove input recovery.
There is no forced ABC round-trip, focus manipulation, qwerty activation,
Gureum replacement or Windows change.

Compared to the currently installed cd1086ee build, this includes the previously
reviewed modifier-side fix and selection diagnostics/preferences commits. This
is not a byte-minimal experiment against that old binary; note this when
interpreting results. All connection stability changes are retained.

Validation: build GUI/client/server/tests using one job; run focused tests,
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
