"""Exercise the built installer on a disposable GitHub-hosted Windows runner."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from verify_build import verify


def registered_location():
    import winreg

    key = (r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"
           r"\{D9785C41-CF76-4DC8-9460-FCA5EF15558A}_is1")
    try:
        entry = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key, 0,
                               winreg.KEY_READ | winreg.KEY_WOW64_64KEY)
    except FileNotFoundError:
        return None
    with entry:
        return Path(winreg.QueryValueEx(entry, "InstallLocation")[0])


def run_installer(executable, log, *options):
    subprocess.run([str(executable), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
                    f"/LOG={log}", *options], check=True, timeout=180)


def main(candidate, evidence):
    if sys.platform != "win32" or os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted":
        raise ValueError("Requires a disposable GitHub-hosted Windows runner")
    revision = os.environ["GITHUB_SHA"]
    target = Path(os.environ["RUNNER_TEMP"]) / "KeyStitch install test"
    if target.exists() or registered_location() is not None:
        raise ValueError("Expected a fresh machine and an unused install location")
    evidence.mkdir(parents=True, exist_ok=True)
    installer, = candidate.rglob("*.exe")
    checksum, = candidate.rglob("windows-SHA256SUMS")
    receipt, = candidate.rglob("windows-build.json")
    digest, filename = checksum.read_text(encoding="utf-8-sig").strip().split(maxsplit=1)
    if filename != installer.name or hashlib.sha256(installer.read_bytes()).hexdigest() != digest:
        raise ValueError("Installer does not match the candidate checksum")
    staged = json.loads(receipt.read_text(encoding="utf-8"))
    os.environ["PATH"] = os.pathsep.join([str(Path(os.environ["SystemRoot"]) / "System32"),
                                         os.environ["SystemRoot"]])
    for phase in ("install", "reinstall"):
        run_installer(installer, evidence / f"{phase}.log", "/SP-", "/NOCLOSEAPPLICATIONS",
                      "/RESTARTEXITCODE=42", f"/DIR={target}")
        if registered_location() != target:
            raise ValueError(f"{phase}: wrong installed location in Add/Remove Programs")
        installed = verify(target, revision, "Windows")
        if installed != staged:
            raise ValueError(f"{phase}: installed executables differ from staged executables")
        (evidence / f"{phase}-build.json").write_text(json.dumps(installed, indent=2), encoding="utf-8")
        print(f"{phase}: confirmed registration and all four installed binaries", flush=True)

    uninstaller, = target.glob("unins*.exe")
    run_installer(uninstaller, evidence / "uninstall.log")
    deadline = time.monotonic() + 30
    while target.exists() and time.monotonic() < deadline:
        time.sleep(0.5)
    if target.exists() or registered_location() is not None:
        raise ValueError("Uninstall left the install directory or product registration behind")
    print("Uninstall removed the install directory and product registration")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
