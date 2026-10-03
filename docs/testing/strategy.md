# Testing strategy

KeyStitch changes should be verified at three levels:

1. **Configuration-level checks** — parse representative `section: remaps`
   rules, including direct remaps, tap/hold rules, modifier chords, and
   screen-specific rules.
2. **Build and automated checks** — run the platform-appropriate CMake build
   and test targets, plus the GitHub Actions CI workflow.
3. **Live cross-platform checks** — verify the resulting behavior between the
   actual Windows, macOS, and Linux machines involved in the scenario.

The live checks are especially important for modifier semantics, Korean
Windows `Right Alt`/`Hangul`, macOS extended keys, and input-source switching.
Record the observed baseline and regressions in the existing documents under
`doc/` rather than relying only on a commit message.

## Isolated paired test foundation

Use the [record-only paired runner](../../tools/pair-test/README.md) in a dedicated
checkout before interacting with a daily-use build. It pins each host's actual
native codec and script fingerprints, sends production-format fixture messages
over TCP in both directions, and gathers one result bundle. Its local CI runs
are two processes on a single OS, **not** a Windows/macOS pairing test.

When both hosts have Codex chats, use the runner's bounded split jobs and collect
all four host reports. SSH/Remote Login is not required. The control chats launch
and collect jobs; only fixed fixture messages travel over the separate test TCP
connection. Keep socket direction separate from event direction so Windows can
connect outbound for both directions without an inbound listener or firewall change.

The runner reuses production `ProtocolUtil`, not the product's Client/Server or
native input adapters. Its successful reconnect/reset checks establish only the
test sink's behavior. They do not prove product cleanup, physical capture,
clipboard isolation, browser navigation or IME correctness. Keep these evidence
levels separate in reports and extend the production integration boundary next.

For native acceptance, compare actual strings/composition in a dedicated native
text field, browser input/textarea/contenteditable and address bar. Test Hangul
to English as well as English to Hangul, including active composition. Record
menu selection, local OS shortcut and remote Right Alt as separate paths. An
input-source menu change alone is not a passed test. Gureum is an optional
compatibility case; include the built-in macOS Korean input source as baseline.

For releases, follow the [release checklist](../../doc/release-checklist.md)
and verify the generated archives and SHA256 files before publishing.
