"""Audit a staged app without executing its code or modifying the bundle.

This checks deployment metadata and bundled Mach-O dependencies, not signing,
notarization, source provenance, ABI compatibility or actual OS support.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import subprocess


MAGICS = {bytes.fromhex(value) for value in (
    "feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca",
    "cafebabf", "bfbafeca",
)}


def version(value):
    if not isinstance(value, str) or not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", value):
        raise ValueError("Invalid macOS version: " + repr(value))
    parts = tuple(map(int, value.split(".")))
    return parts + (0,) * (3 - len(parts))


def parse_load_commands(output):
    slices = re.split(r"^.+\(architecture [^)]+\):\s*$", output, flags=re.M)
    if len(slices) > 1:
        minimums, dependencies = [], []
        for section in slices[1:]:
            mins, deps = parse_load_commands(section)
            minimums.extend(mins)
            dependencies.extend(deps)
        return minimums, dependencies
    minimums, dependencies = [], []
    for block in re.split(r"Load command \d+", output)[1:]:
        match = re.search(r"^\s*cmd (\S+)\s*$", block, re.M)
        if not match:
            continue
        command = match.group(1)
        if command == "LC_BUILD_VERSION":
            platform = re.search(r"^\s*platform (\S+)", block, re.M)
            if not platform or platform.group(1) not in ("1", "MACOS", "macos"):
                raise ValueError("Non-macOS or unknown Mach-O platform")
            match = re.search(r"^\s*minos (\S+)", block, re.M)
            if not match:
                raise ValueError("Missing Mach-O minimum OS")
            minimums.append(match.group(1))
        elif command == "LC_VERSION_MIN_MACOSX":
            match = re.search(r"^\s*version (\S+)", block, re.M)
            if not match:
                raise ValueError("Missing legacy Mach-O minimum OS")
            minimums.append(match.group(1))
        elif command.startswith("LC_VERSION_MIN_"):
            raise ValueError("Non-macOS Mach-O deployment target")
        elif command in ("LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
                          "LC_LAZY_LOAD_DYLIB", "LC_LOAD_UPWARD_DYLIB"):
            match = re.search(r"^\s*name (.+?) \(offset \d+\)", block, re.M)
            if not match:
                raise ValueError("Unreadable dependency load command")
            dependencies.append(match.group(1))
    if not minimums:
        raise ValueError("No macOS minimum version load command")
    for minimum in minimums:
        version(minimum)
    return minimums, dependencies


def validate_binary(minimums, dependencies, declared):
    for minimum in minimums:
        if version(minimum) > version(declared):
            raise ValueError(f"Binary requires macOS {minimum}, but plist declares {declared}")
    for dependency in dependencies:
        if not dependency.startswith(("/System/Library/", "/usr/lib/", "@rpath/",
                                      "@loader_path/", "@executable_path/")):
            raise ValueError("Unbundled external dependency: " + dependency)


def audit(app):
    app = app.resolve(strict=True)
    info_path = app / "Contents/Info.plist"
    with info_path.open("rb") as stream:
        info = plistlib.load(stream)
    declared = info.get("LSMinimumSystemVersion")
    version(declared)
    executable = info.get("CFBundleExecutable", "")
    if not executable or Path(executable).name != executable:
        raise ValueError("Invalid CFBundleExecutable")
    main = app / "Contents/MacOS" / executable
    records = []
    for path in sorted((app / "Contents").rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as stream:
            magic = stream.read(4)
        if magic not in MAGICS:
            continue
        output = subprocess.run(["otool", "-l", str(path)], check=True,
                                text=True, capture_output=True).stdout
        minimums, dependencies = parse_load_commands(output)
        try:
            validate_binary(minimums, dependencies, declared)
        except ValueError as exc:
            raise ValueError(f"{path.relative_to(app)}: {exc}") from exc
        records.append({"path": path.relative_to(app).as_posix(),
                        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "minimumOSByLoadCommand": minimums,
                        "dependencies": sorted(set(dependencies))})
    if not any(record["path"] == main.relative_to(app).as_posix() for record in records):
        raise ValueError("Main executable is absent or is not a regular Mach-O file")
    return {"schemaVersion": 1, "bundleIdentifier": info.get("CFBundleIdentifier"),
            "declaredMinimumOS": declared,
            "infoPlistSha256": hashlib.sha256(info_path.read_bytes()).hexdigest(),
            "binaries": records,
            "checks": ["minimum-os-load-commands", "external-absolute-dependencies"],
            "notVerified": ["source-provenance", "signing", "notarization",
                            "rpath-resolution", "runtime-input", "minimum-os-runtime"]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("--manifest", type=Path, help="Write an audit receipt outside the signed app")
    args = parser.parse_args()
    try:
        if args.manifest and (args.app.resolve() == args.manifest.resolve() or
                              args.app.resolve() in args.manifest.resolve().parents):
            raise ValueError("Audit receipt must be outside the signed app")
        result = audit(args.app)
        if args.manifest:
            args.manifest.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(f"Bundle audit passed: {len(result['binaries'])} Mach-O files; declared macOS {result['declaredMinimumOS']}")
    except (OSError, ValueError, plistlib.InvalidFileException, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Bundle audit failed: {exc}\n")
