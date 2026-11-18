"""Verify this project's newly built binaries before packaging.

The --build-info entry point exits before UI, settings, service or input setup.
The marker check rejects old executables before invoking them. This is an
accidental-mix/dirty-build guard, not a sandbox for untrusted executables or a
cryptographic source attestation. Only use binaries built from trusted source.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


MARKER = b"KeyStitchBuildInfo:v1"
COMPONENTS = {"gui": "input-leap", "client": "input-leapc", "server": "input-leaps"}
IDENTITY_FIELDS = ("sourceRevision", "sourceDirty", "buildId", "version", "platform",
                   "architecture", "compiler", "compilerVersion", "minimumMacOS", "configuration")


def validate_reports(reports, expected_revision, platform, configuration="Release", allow_dirty=False):
    expected_components = set(COMPONENTS) | ({"daemon"} if platform == "Windows" else set())
    if set(reports) != expected_components:
        raise ValueError("Missing or unexpected product component")
    if not re.fullmatch(r"[0-9a-f]{40}", expected_revision):
        raise ValueError("Expected revision must be a full Git SHA-1")
    first = None
    for component, report in reports.items():
        if not isinstance(report, dict) or report.get("format") != MARKER.decode() or report.get("schemaVersion") != 1:
            raise ValueError(f"{component}: unsupported build-info format")
        if report.get("component") != component:
            raise ValueError(f"{component}: wrong executable role")
        if any(field not in report for field in IDENTITY_FIELDS):
            raise ValueError(f"{component}: incomplete build identity")
        if report["sourceRevision"] != expected_revision or report["platform"] != platform:
            raise ValueError(f"{component}: wrong source revision or platform")
        if type(report["sourceDirty"]) is not bool:
            raise ValueError(f"{component}: source cleanliness must be explicit")
        if report["sourceDirty"] and not allow_dirty:
            raise ValueError(f"{component}: dirty source cannot form a clean candidate")
        if report["configuration"] != configuration:
            raise ValueError(f"{component}: wrong build configuration")
        if not isinstance(report["buildId"], str) or not re.fullmatch(r"[0-9a-f]{32}", report["buildId"]):
            raise ValueError(f"{component}: invalid build ID")
        for field in ("version", "architecture", "compiler", "compilerVersion"):
            if not isinstance(report[field], str) or not report[field]:
                raise ValueError(f"{component}: missing {field}")
        identity = {field: report[field] for field in IDENTITY_FIELDS}
        if first is not None and identity != first:
            raise ValueError(f"{component}: mixed build identities")
        first = identity
    return first


def inspect_binary(path):
    data = path.read_bytes()
    if MARKER not in data:
        raise ValueError(f"{path.name}: no safe build-info entry point; refusing to execute")
    before = hashlib.sha256(data).hexdigest()
    result = subprocess.run([str(path.resolve()), "--build-info"], capture_output=True,
                            text=True, encoding="utf-8", timeout=10, check=True)
    if len(result.stdout) > 16384:
        raise ValueError(f"{path.name}: oversized build report")
    report = json.loads(result.stdout)
    if hashlib.sha256(path.read_bytes()).hexdigest() != before:
        raise ValueError(f"{path.name}: executable changed during inspection")
    return report, before


def verify(directory, expected_revision, platform, configuration="Release", allow_dirty=False):
    names = dict(COMPONENTS)
    if platform == "Windows":
        names["daemon"] = "input-leapd"
    reports, records = {}, []
    for component, name in names.items():
        filename = name + (".exe" if platform == "Windows" else "")
        report, digest = inspect_binary(directory / filename)
        reports[component] = report
        records.append({"component": component, "filename": filename, "sha256": digest})
    identity = validate_reports(reports, expected_revision, platform, configuration, allow_dirty)
    return {"schemaVersion": 1, "identity": identity, "binaries": records,
            "scope": "self-reported-build-identity-and-current-binary-hashes",
            "notVerified": ["source-attestation", "signing", "dependency-payload",
                            "installation", "input-acceptance", "runtime-os-support"]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--expected-revision", required=True)
    parser.add_argument("--platform", choices=["Darwin", "Windows"], required=True)
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--allow-dirty", action="store_true", help="Development only; keep dirty status in receipt")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        receipt = verify(args.directory, args.expected_revision, args.platform, args.configuration, args.allow_dirty)
        if args.output:
            args.output.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        print(f"Build set verified: {receipt['identity']['buildId']} ({len(receipt['binaries'])} components)")
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, f"Build verification failed: {exc}\n")
