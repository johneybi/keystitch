"""Pure fixture checks; no app execution, signing or installation."""
import unittest

from audit_macos_bundle import parse_load_commands, validate_binary, version


class BundleAuditTests(unittest.TestCase):
    def test_current_and_universal_load_commands(self):
        text = """app (architecture arm64):
Load command 1
      cmd LC_BUILD_VERSION
 platform 1
    minos 13.0
Load command 2
      cmd LC_LOAD_DYLIB
     name @rpath/QtCore.framework/Versions/A/QtCore (offset 24)
app (architecture x86_64):
Load command 0
      cmd LC_VERSION_MIN_MACOSX
  version 12.0
"""
        minimums, deps = parse_load_commands(text)
        self.assertEqual(minimums, ["13.0", "12.0"])
        validate_binary(minimums, deps, "13.0")
        with self.assertRaises(ValueError):
            validate_binary(minimums, deps, "12.0")
        with self.assertRaises(ValueError):
            parse_load_commands(text + "\napp (architecture arm64e):\nLoad command 0\n cmd LC_SEGMENT_64\n")

    def test_declared_minimum_cannot_hide_newer_binary(self):
        with self.assertRaisesRegex(ValueError, "requires macOS 26.0"):
            validate_binary(["26.0"], [], "11.0")
        validate_binary(["11.0", "12.3.1"], [], "12.3.1")

    def test_external_build_machine_dependencies_fail(self):
        for path in ("/opt/homebrew/lib/libssl.dylib", "/Users/builder/lib.dylib", "libssl.dylib"):
            with self.assertRaises(ValueError):
                validate_binary(["11.0"], [path], "11.0")
        validate_binary(["11.0"], ["/usr/lib/libSystem.B.dylib",
                                  "/System/Library/Frameworks/Carbon.framework/Carbon",
                                  "@loader_path/../Frameworks/lib.dylib"], "11.0")

    def test_missing_or_non_macos_commands_fail(self):
        for text in ("", "Load command 1\n cmd LC_BUILD_VERSION\n platform 2\n minos 13.0",
                     "Load command 1\n cmd LC_BUILD_VERSION\n platform 1",
                     "Load command 1\n cmd LC_VERSION_MIN_IPHONEOS\n version 13.0"):
            with self.assertRaises(ValueError):
                parse_load_commands(text)

    def test_version_comparison_is_numeric_and_strict(self):
        self.assertGreater(version("11.10"), version("11.9.9"))
        self.assertEqual(version("26.0"), version("26.0.0"))
        for value in (None, "", "latest", "11", "11.0suffix", " 11.0", "11.0.0.1"):
            with self.assertRaises(ValueError):
                version(value)


if __name__ == "__main__":
    unittest.main()
