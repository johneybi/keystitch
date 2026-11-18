"""Exercise source-state refresh with disposable Git repositories, never the app."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "cmake/WriteBuildInfo.cmake"


class BuildMetadataTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "source"
        self.repo.mkdir()
        self.git("init")
        (self.repo / "source.cpp").write_text("original\n", encoding="utf-8")
        (self.repo / ".gitignore").write_text("/build/\n/aqtinstall.log\n", encoding="utf-8")
        self.commit()

    def git(self, *args):
        return subprocess.check_output(
            ["git", "-C", str(self.repo), "-c", "user.name=Build metadata fixture",
             "-c", "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false",
             "-c", f"core.hooksPath={self.root / 'no-hooks'}", *args],
            text=True, stderr=subprocess.PIPE).strip()

    def commit(self):
        self.git("add", ".")
        self.git("commit", "-m", "fixture")

    def metadata(self, source=None):
        header = self.root / "BuildInfoGenerated.h"
        subprocess.run(
            ["cmake", f"-DSOURCE_ROOT={source or self.repo}", f"-DOUTPUT_FILE={header}",
             "-DBUILD_ID=" + "b" * 32, "-DPRODUCT_VERSION=3.0.3",
             "-DTARGET_PLATFORM=Windows", "-DTARGET_ARCH=x64",
             '-DCOMPILER_ID=fixture"compiler', "-DCOMPILER_VERSION=1",
             "-DMINIMUM_MACOS=", "-P", str(SCRIPT)],
            check=True, capture_output=True, text=True)
        literal = header.read_text(encoding="utf-8").split("#define KEYSTITCH_BUILD_METADATA ", 1)[1].strip()
        return json.loads(json.loads(literal))

    def test_clean_identity_handles_empty_platform_fields_and_quotes(self):
        info = self.metadata()
        self.assertFalse(info["sourceDirty"])
        self.assertEqual(self.git("rev-parse", "HEAD"), info["sourceRevision"])
        self.assertEqual("", info["minimumMacOS"])
        self.assertEqual('fixture"compiler', info["compiler"])

    def test_refresh_detects_edits_and_new_commit_without_configure(self):
        first = self.metadata()
        (self.repo / "source.cpp").write_text("modified\n", encoding="utf-8")
        self.assertTrue(self.metadata()["sourceDirty"])
        self.commit()
        final = self.metadata()
        self.assertFalse(final["sourceDirty"])
        self.assertNotEqual(first["sourceRevision"], final["sourceRevision"])
        self.assertEqual(first["buildId"], final["buildId"])

    def test_untracked_source_is_dirty_but_build_artifacts_are_not(self):
        (self.repo / "aqtinstall.log").write_text("installer log\n", encoding="utf-8")
        (self.repo / "build").mkdir()
        (self.repo / "build" / "artifact").write_bytes(b"artifact")
        self.assertFalse(self.metadata()["sourceDirty"])
        (self.repo / "new.cpp").write_text("new source\n", encoding="utf-8")
        self.assertTrue(self.metadata()["sourceDirty"])

    def test_unknown_source_cannot_claim_clean_identity(self):
        source = self.root / "no-git"
        source.mkdir()
        info = self.metadata(source)
        self.assertTrue(info["sourceDirty"])
        self.assertEqual("unknown", info["sourceRevision"])


if __name__ == "__main__":
    unittest.main()
