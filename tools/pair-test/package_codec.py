#!/usr/bin/env python3
"""Package a tested codec; verify a trusted CI archive before writing any files.

Checksums detect mismatch, not authorship. Download only the artifact of a
successful workflow for the expected commit. This verifier comes from the
matching Git checkout, never from the downloaded archive.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import platform
import re
import subprocess
import tarfile

import pair_test as pair

META_FIELDS = {"schema_version", "os", "arch", "source_commit", "source_digest",
               "source_dirty", "protocol", "boundary", "native_input", "files"}
BUILD_FIELDS = META_FIELDS - {"schema_version", "files"}
LIMITS = {"package.json": 32768, "pair_test.py": 262144, "smoke.json": 65536,
          "pair-codec": 10485760, "pair-codec.exe": 10485760}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def architecture(value):
    return {"amd64": "x86_64", "aarch64": "arm64"}.get(value.lower(), value.lower())


def validate_metadata(meta, expected_commit):
    if (not isinstance(meta, dict) or set(meta) != META_FIELDS
            or type(meta["schema_version"]) is not int or meta["schema_version"] != 1
            or meta["source_commit"] != expected_commit
            or not re.fullmatch(r"[a-f0-9]{40}|[a-f0-9]{64}", expected_commit)
            or meta["source_dirty"] is not False or meta["native_input"] is not False
            or meta["boundary"] != "production_ProtocolUtil"):
        raise ValueError("invalid, dirty or mismatched package identity")
    if (not isinstance(meta["os"], str) or not isinstance(meta["arch"], str)
            or meta["os"] != platform.system()
            or architecture(meta["arch"]) != architecture(platform.machine())):
        raise ValueError("package OS/architecture differs from this host")
    if (not isinstance(meta["source_digest"], str)
            or not re.fullmatch(r"[a-f0-9]{64}", meta["source_digest"])
            or not isinstance(meta["protocol"], list) or len(meta["protocol"]) != 2
            or any(type(v) is not int or not 0 <= v <= 65535 for v in meta["protocol"])):
        raise ValueError("invalid build fingerprint/protocol")
    codec_name = "pair-codec.exe" if meta["os"] == "Windows" else "pair-codec"
    files = meta["files"]
    if not isinstance(files, dict) or set(files) != {codec_name, "pair_test.py", "smoke.json"}:
        raise ValueError("unexpected package files")
    if any(not isinstance(v, str) or not re.fullmatch(r"[a-f0-9]{64}", v) for v in files.values()):
        raise ValueError("invalid package file hash")
    return codec_name


def pack(codec, output, expected_commit):
    info = pair.inspect_host(codec, "ci-package")
    name = "pair-codec.exe" if info["os"] == "Windows" else "pair-codec"
    files = {name: Path(codec).read_bytes(),
             "pair_test.py": (pair.HERE / "pair_test.py").read_bytes(),
             "smoke.json": (pair.HERE / "smoke.json").read_bytes()}
    meta = {key: info[key] for key in BUILD_FIELDS}
    meta.update(schema_version=1, files={key: sha(value) for key, value in files.items()})
    validate_metadata(meta, expected_commit)
    files["package.json"] = (json.dumps(meta, sort_keys=True, indent=2) + "\n").encode("utf-8")
    if any(len(value) > LIMITS[key] for key, value in files.items()):
        raise ValueError("package file exceeds size limit")
    with tarfile.open(output, "x:") as archive:
        for key, value in files.items():
            member = tarfile.TarInfo(key)
            member.size = len(value)
            member.mode = 0o700 if key == name else 0o600
            archive.addfile(member, io.BytesIO(value))
    return meta


def unpack(archive_path, output, expected_commit):
    output = Path(output)
    if output.exists():
        raise FileExistsError("output directory already exists")
    if Path(archive_path).stat().st_size > 12582912:
        raise ValueError("package archive exceeds size limit")
    files = {}
    # Only our uncompressed, flat regular-file tar format is accepted. No
    # extractall: paths, links, duplicates and oversized entries are rejected.
    with tarfile.open(archive_path, "r:") as archive:
        for member in archive:
            if (member.name not in LIMITS or member.name in files or not member.isreg()
                    or not 0 <= member.size <= LIMITS[member.name] or len(files) >= 4):
                raise ValueError("unsafe or unexpected archive member")
            stream = archive.extractfile(member)
            with stream:
                files[member.name] = stream.read(member.size + 1)
            if len(files[member.name]) != member.size:
                raise ValueError("truncated archive member")
    meta = json.loads(files.get("package.json", b"null"))
    codec_name = validate_metadata(meta, expected_commit)
    if set(files) != set(meta["files"]) | {"package.json"}:
        raise ValueError("missing package files")
    for name, expected_hash in meta["files"].items():
        if sha(files[name]) != expected_hash:
            raise ValueError("package checksum mismatch: " + name)
    for name in ("pair_test.py", "smoke.json"):
        if files[name] != (pair.HERE / name).read_bytes():
            raise ValueError("package differs from trusted checkout: " + name)
    # All validation finishes before the first destination write.
    output.mkdir(parents=True, mode=0o700)
    for name, value in files.items():
        with (output / name).open("xb") as stream:
            stream.write(value)
    if platform.system() != "Windows":
        (output / codec_name).chmod(0o700)
    return dict(status="prepared", source_commit=expected_commit,
                codec=str((output / codec_name).resolve()), native_input=False)


def check_checkout(expected_commit):
    root = pair.HERE.parent.parent.resolve()
    command = ["git", "-c", "safe.directory=" + str(root), "--no-optional-locks", "-C", str(root)]
    head = subprocess.check_output(command + ["rev-parse", "HEAD"], text=True, timeout=5).strip()
    if head != expected_commit:
        raise ValueError("trusted checkout is not at the expected commit")
    if subprocess.run(command + ["diff", "--quiet", "HEAD", "--", "tools/pair-test"], timeout=5).returncode:
        raise ValueError("trusted checkout test tooling has local modifications")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="action", required=True)
    for action, source in (("pack", "codec"), ("unpack", "archive")):
        sub = subs.add_parser(action)
        sub.add_argument("--" + source, required=True)
        sub.add_argument("--output", required=True)
        sub.add_argument("--expected-commit", required=True)
    args = parser.parse_args()
    try:
        check_checkout(args.expected_commit)
        if args.action == "pack":
            meta = pack(args.codec, args.output, args.expected_commit)
            print(json.dumps(dict(status="packaged", source_commit=meta["source_commit"], native_input=False)))
        else:
            print(json.dumps(unpack(args.archive, args.output, args.expected_commit)))
        return 0
    except (OSError, ValueError, TypeError, KeyError, tarfile.TarError, subprocess.SubprocessError, pair.Failure) as error:
        print(json.dumps(dict(status="failed", error=str(error), native_input=False)))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
