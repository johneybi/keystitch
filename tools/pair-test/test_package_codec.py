import copy
import io
import json
import os
from pathlib import Path
import platform
import tarfile
import tempfile
import unittest
from unittest import mock

import package_codec as package
import pair_test as pair


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="keystitch-package-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.codec = self.root / "codec"
        self.codec.write_bytes(b"fake codec; never executed")
        self.archive = self.root / "tools.tar"
        self.output = self.root / "unpacked"
        self.info = dict(os=platform.system(), arch=platform.machine(), source_commit="a" * 40,
                         source_digest="b" * 64, source_dirty=False, native_input=False,
                         boundary="production_ProtocolUtil", protocol=[1, 6])
        self.patcher = mock.patch.object(pair, "inspect_host", return_value=self.info)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def make_archive(self):
        package.pack(self.codec, self.archive, self.info["source_commit"])

    def rewrite(self, mutate):
        with tarfile.open(self.archive, "r:") as archive:
            members = [(member, archive.extractfile(member).read()) for member in archive]
        mutate(members)
        changed = self.root / "changed.tar"
        with tarfile.open(changed, "w:") as archive:
            for member, data in members:
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data) if member.isreg() else None)
        return changed

    def test_roundtrip_and_no_overwrite(self):
        self.make_archive()
        result = package.unpack(self.archive, self.output, "a" * 40)
        self.assertFalse(result["native_input"])
        self.assertEqual(Path(result["codec"]).read_bytes(), self.codec.read_bytes())
        with self.assertRaises(FileExistsError):
            package.unpack(self.archive, self.output, "a" * 40)
        with self.assertRaises(FileExistsError):
            package.pack(self.codec, self.archive, "a" * 40)

    def test_wrong_commit_dirty_or_os_rejected_before_write(self):
        for key, value in (("source_commit", "c" * 40), ("source_dirty", True),
                           ("os", "wrong-os"), ("arch", "wrong-arch"), ("arch", None),
                           ("native_input", True)):
            with self.subTest(key=key):
                altered = copy.deepcopy(self.info)
                altered[key] = value
                with mock.patch.object(pair, "inspect_host", return_value=altered):
                    with self.assertRaises(ValueError):
                        package.pack(self.codec, self.archive, "a" * 40)
                self.assertFalse(self.archive.exists())

    def test_unpack_wrong_commit_before_write(self):
        self.make_archive()
        with self.assertRaises(ValueError):
            package.unpack(self.archive, self.output, "c" * 40)
        self.assertFalse(self.output.exists())

    def test_traversal_link_duplicate_and_oversize_rejected(self):
        self.make_archive()
        def traversal(members):
            members[0][0].name = "../escape"
        def link(members):
            members[0][0].type = tarfile.SYMTYPE
            members[0][0].linkname = "elsewhere"
        def duplicate(members):
            members.append(copy.deepcopy(members[0]))
        for mutate in (traversal, link, duplicate):
            with self.subTest(mutate=mutate.__name__):
                changed = self.rewrite(mutate)
                with self.assertRaises(ValueError):
                    package.unpack(changed, self.output, "a" * 40)
                self.assertFalse(self.output.exists())
        changed = self.root / "oversized.tar"
        with tarfile.open(changed, "w:") as archive:
            member = tarfile.TarInfo("pair_test.py")
            member.size = package.LIMITS[member.name] + 1
            archive.addfile(member, io.BytesIO(b"x" * member.size))
        with self.assertRaises(ValueError):
            package.unpack(changed, self.output, "a" * 40)
        self.assertFalse(self.output.exists())

    def test_corruption_and_self_consistent_untrusted_agent_rejected(self):
        self.make_archive()
        def corrupt(members):
            members[0] = (members[0][0], b"corrupted")
        changed = self.rewrite(corrupt)
        with self.assertRaises(ValueError):
            package.unpack(changed, self.output, "a" * 40)
        self.assertFalse(self.output.exists())
        def replace_agent(members):
            for index, (member, data) in enumerate(members):
                if member.name == "pair_test.py":
                    members[index] = (member, b"not the trusted agent")
                if member.name == "package.json":
                    meta = json.loads(data)
                    meta["files"]["pair_test.py"] = package.sha(b"not the trusted agent")
                    members[index] = (member, json.dumps(meta).encode())
        changed = self.rewrite(replace_agent)
        with self.assertRaises(ValueError):
            package.unpack(changed, self.output, "a" * 40)
        self.assertFalse(self.output.exists())


@unittest.skipUnless(os.environ.get("PAIR_CODEC"), "set PAIR_CODEC for real binary packaging")
class RealPackageTests(unittest.TestCase):
    def test_real_codec_package_unpacks_and_runs_describe(self):
        codec = os.environ["PAIR_CODEC"]
        info = pair.inspect_host(codec, "original")
        with tempfile.TemporaryDirectory(prefix="keystitch-real-package-") as root:
            archive = Path(root) / "tools.tar"
            package.pack(codec, archive, info["source_commit"])
            result = package.unpack(archive, Path(root) / "prepared", info["source_commit"])
            unpacked = pair.inspect_host(result["codec"], "unpacked")
            for field in ("source_commit", "source_digest", "codec_sha256", "agent_sha256", "native_input"):
                self.assertEqual(unpacked[field], info[field])


if __name__ == "__main__":
    unittest.main()
