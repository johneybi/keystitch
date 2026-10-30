# Offline F19 dispatcher coverage — 2026-10-30

## Baseline and scope

Based on `2127baf15d4210a49994a2326923d312eb353edf`, the Input Leap branch
containing bidirectional F19 selection and the documented Chrome recurrence.
This is not the old one-way han2 reselection experiment or the separate
Barrier-based record-only TCP runner.

This increment makes the actual macOS F19 dispatcher testable without live OS
input. It does **not** change the switching policy, TIS activation polling,
historical 75 ms settling interval, modifier bookkeeping or keyboard delivery.
There is no new fallback, native shortcut, forced Gureum mode, input-source
enablement, clipboard injection or browser-focus manipulation.

`OSXKeyState` exposes three protected virtual OS-side boundaries:

- `cycleInputSource`: the existing complete TIS selection/activation adapter;
- `describeInputSourceState`: the existing content-free diagnostic snapshot;
- `waitForInputSourceSettling`: the existing 75 ms F19 wait.

Production defaults keep their prior behavior. The tests replace these three,
the existing `postKeyboardEvent` delivery boundary, hardware polling and layout
enumeration. The test executable does not create/post CGEvents, read/select TIS
sources, read preferences, poll the user's keys, sleep for activation or launch
an app. The fixture key map is single-group and its source candidates are
in-memory Apple ABC/Korean IDs; a fixture source change is not a real IME change.

## Coverage added

`src/test/unittests/platform/OSXF19DispatchTests.cpp` runs nine tests through
the real `OSXKeyState::fakeKey`, including:

- fixture ABC → Korean and Korean → ABC selection before the next key pair;
- F19 up/repeat consumption without another selection or synthetic F19;
- failed selection: no retry/settling, with the next ordinary key still delivered;
- ordinary keys without input-source calls;
- held Shift/Control/Option/Command across successful and failed transitions;
- duplicate F19 down normalized by the real `KeyState` as a repeat;
- real `KeyRemapper` → `KeyState`/`KeyMap` → macOS dispatcher for 32 Right Alt
  taps and 32 Windows Hangul taps, each followed by one recorded A-key pair;
- Shift restoration around a remapped tap;
- remapped hold and key-state cleanup, followed by a new tap without a stuck
  Command modifier. This is local cleanup coverage, not a network-disconnect test.

The simulated source adapter reuses the production pure selection helper. It
does **not** exercise the TIS adapter's candidate enumeration, preference reads,
activation polling, real composition or asynchronous OS readiness. “Next key”
means a recorded physical virtual-key down/up pair, not committed Hangul or
English text. Real Windows hooks, transport, real layouts/group overrides,
IMK, Safari and Chrome remain outside this new coverage.

## Verification

Fresh native arm64 `unittests` and `input-leapc` builds succeeded, single-job at
nice level 15. The initial fresh CMake configuration had an empty SDK root and
failed to link libc++; explicitly selecting the existing SDK below resolved
that build configuration issue without changing the system developer directory.
Existing deprecated macOS API and duplicate-library linker warnings remain.

```sh
nice -n 15 cmake -S . -B build-f19 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
  -DINPUTLEAP_BUILD_GUI=OFF -DINPUTLEAP_BUILD_TESTS=ON \
  -DINPUTLEAP_BUILD_AUDIO_DAEMON=OFF \
  -DINPUTLEAP_BUILD_GULRAK_FILESYSTEM=ON \
  -DOPENSSL_USE_STATIC_LIBS=TRUE -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3
nice -n 15 cmake --build build-f19 --parallel 1 --target unittests input-leapc
nice -n 15 build-f19/bin/unittests --gtest_filter='OSXF19DispatchTests.*:OSXKeyStateTests.*:OSXInputSourceSelection.*:KeyRemapPipelineTests.*:KeyRemapperTests.*' --gtest_output=xml:build-f19/focused-results.xml
```

- Focused suite: **56/56 passed**, including all nine new dispatcher tests.
- Full unit suite: **192/194 passed**. The two failures are
  `SecureUtilsTest.FormatSslFingerprintHexWithSeparators` and
  `SecureUtilsTest.CreateFingerprintRandomArt`.
  Both also failed in the pre-existing `build-stability/bin/unittests` binary.
  Their implementation/test/fixture sources are unchanged between that source
  baseline and this one. This is not a fresh full-suite baseline build and is
  not an all-green full-suite claim. No SecureUtils behavior is changed here.
- Negative controls: temporarily bypassing the F19 selection call made the
  bidirectional/next-key test fail; temporarily removing `!repeat` made the
  repeat-consumption test fail. Both exited 1 as expected. Both mutations were
  restored, the normal binaries rebuilt, and the focused suite passed again.
- CMake's existing `platform/OSX*.cpp` glob includes the new test. Generated
  test reports and the dedicated `build-f19` tree are not committed.

No app was installed, replaced or launched; no live keyboard/mouse test or
input-method setting change was performed. The installed client retained SHA256
`b2cec6806eb3de1da75195833274f909c8480b39cd4cd3f50596e6ee21830b24`.

## Next boundary

Use this coverage to protect the dispatcher while introducing a separately
selectable switching backend. Compare direct TIS selection with the user's
configured native macOS switching path, one change at a time. Do not add an
automatic fallback that hides which backend ran.

Only after explicit approval, validate actual committed/composing strings in
native controls, Chrome and Safari, in both directions and with/without an
active composition. Built-in macOS Korean/ABC are the baseline; Gureum is an
optional compatibility case. A menu-bar/source-ID change or this offline pass
is never evidence that the browser IME problem is fixed.
