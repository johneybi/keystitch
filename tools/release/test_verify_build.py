"""Build-set contract checks; fake binaries are never launched."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from verify_build import inspect_binary, validate_reports

SHA = "a" * 40


def reports(platform="Darwin"):
    roles = ["gui", "client", "server"] + (["daemon"] if platform == "Windows" else [])
    return {role: {"format": "KeyStitchBuildInfo:v1", "schemaVersion": 1,
                   "component": role, "sourceRevision": SHA, "sourceDirty": False,
                   "buildId": "b" * 32, "version": "3.0.3", "platform": platform,
                   "architecture": "arm64" if platform == "Darwin" else "x64",
                   "compiler": "fixture", "compilerVersion": "1", "minimumMacOS": "26.0",
                   "configuration": "Release"} for role in roles}


class BuildSetTests(unittest.TestCase):
    def test_complete_mac_and_windows_sets(self):
        for platform in ("Darwin", "Windows"):
            identity = validate_reports(reports(platform), SHA, platform)
            self.assertEqual(identity["sourceRevision"], SHA)

    def test_missing_component_and_wrong_role(self):
        data = reports("Windows")
        del data["daemon"]
        with self.assertRaises(ValueError): validate_reports(data, SHA, "Windows")
        data = reports()
        data["client"]["component"] = "server"
        with self.assertRaises(ValueError): validate_reports(data, SHA, "Darwin")

    def test_mixed_builds_revisions_and_configurations_rejected(self):
        for field, value in (("buildId", "c" * 32), ("sourceRevision", "d" * 40),
                             ("architecture", "x86_64"), ("configuration", "Debug"),
                             ("platform", "Windows"), ("compilerVersion", "2")):
            data = reports()
            data["client"][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_reports(data, SHA, "Darwin")

    def test_dirty_is_rejected_unless_explicit_development_mode(self):
        data = reports()
        for item in data.values(): item["sourceDirty"] = True
        with self.assertRaises(ValueError): validate_reports(data, SHA, "Darwin")
        self.assertTrue(validate_reports(data, SHA, "Darwin", allow_dirty=True)["sourceDirty"])
        data["client"]["sourceDirty"] = False
        with self.assertRaises(ValueError): validate_reports(data, SHA, "Darwin", allow_dirty=True)

    def test_malformed_and_incomplete_identity_rejected(self):
        for changes in ({"sourceDirty": "false"}, {"buildId": ""}, {"schemaVersion": 2},
                        {"compiler": ""}):
            data = reports()
            data["client"].update(changes)
            with self.assertRaises(ValueError): validate_reports(data, SHA, "Darwin")
        data = reports()
        del data["client"]["sourceRevision"]
        with self.assertRaises(ValueError): validate_reports(data, SHA, "Darwin")

    def test_old_executable_is_rejected_without_starting_a_process(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "old-app"
            path.write_bytes(b"legacy executable without the new entry point")
            with patch("verify_build.subprocess.run") as run:
                with self.assertRaisesRegex(ValueError, "refusing to execute"):
                    inspect_binary(path)
                run.assert_not_called()


if __name__ == "__main__": unittest.main()
