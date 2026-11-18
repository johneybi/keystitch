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


def run_powershell(command):
    powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    command = "$ErrorActionPreference = 'Stop'\n" + command
    result = subprocess.run([str(powershell), "-NoProfile", "-NonInteractive", "-Command", command],
                            check=True, capture_output=True, text=True, timeout=60)
    return result.stdout.strip()


def listener_rule_count():
    return int(run_powershell(
        "@(Get-NetFirewallRule -PolicyStore PersistentStore | "
        "Where-Object DisplayName -eq 'Input Leap Keymap Listener').Count"
    ))


def check_legacy_settings(target, evidence):
    import winreg

    legacy = r"Software\Debauchee\Barrier"
    current = r"Software\InputLeap\InputLeap"
    for path in (legacy, current):
        try:
            entry = winreg.OpenKey(winreg.HKEY_CURRENT_USER, path)
        except FileNotFoundError:
            continue
        entry.Close()
        raise ValueError(f"Expected an unused test profile at HKCU\\{path}")

    # The wizard exercises Qt's native platform plugin and GUI entry point.
    # The fixture leaves input sharing stopped and disables certificate creation.
    preferences = {
        "": {"serverHostname": "peer.example.invalid", "useExternalConfig": "true",
             "configFile": str(target.parent / "기존 KeyStitch.conf"),
             "language": "en", "wizardLastRun": "0", "autoStart": "false", "cryptoEnabled": "false"},
        r"internalConfig\keyRemaps": {"size": "1"},
        r"internalConfig\keyRemaps\1": {"screen": "TestMac", "source": "Alt_R", "output": "F19"},
    }
    for subkey, values in preferences.items():
        path = legacy + ("\\" + subkey if subkey else "")
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, path) as entry:
            for name, value in values.items():
                winreg.SetValueEx(entry, name, 0, winreg.REG_SZ, value)

    with subprocess.Popen([str(target / "input-leap.exe")], cwd=target) as process:
        try:
            title = run_powershell(
                f"$app = Get-Process -Id {process.pid}\n"
                "$deadline = [DateTime]::UtcNow.AddSeconds(40)\n"
                "do {\n"
                "  $app.Refresh()\n"
                "  if ($app.HasExited) { throw 'Application exited before setup' }\n"
                "  if ($app.MainWindowTitle -eq 'Setup InputLeap') { $app.MainWindowTitle; exit }\n"
                "  Start-Sleep -Milliseconds 500\n"
                "} while ([DateTime]::UtcNow -lt $deadline)\n"
                "throw ('Setup window did not appear. Last title: ' + $app.MainWindowTitle)"
            )
        except subprocess.CalledProcessError as error:
            raise ValueError(f"Installed GUI startup failed: {error.stderr.strip()}") from error
        finally:
            if process.poll() is None:
                process.kill()
    if title != "Setup InputLeap":
        raise ValueError("Installed GUI did not open the setup wizard")

    for root in (legacy, current):
        for subkey, values in preferences.items():
            path = root + ("\\" + subkey if subkey else "")
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as entry:
                for name, expected in values.items():
                    actual, _ = winreg.QueryValueEx(entry, name)
                    if actual != expected:
                        raise ValueError(f"Unexpected value for {path}\\{name}")

    (evidence / "settings-import.txt").write_text(
        "Installed GUI opened the setup wizard. Source and copied registry values match.\n",
        encoding="utf-8"
    )
    print("Installed GUI opened the wizard and imported Barrier settings", flush=True)


def main(candidate, evidence):
    if sys.platform != "win32" or os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted":
        raise ValueError("Requires a disposable GitHub-hosted Windows runner")
    revision = os.environ["GITHUB_SHA"]
    target = Path(os.environ["RUNNER_TEMP"]) / "KeyStitch install test"
    if target.exists() or registered_location() is not None or listener_rule_count() != 0:
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
        if listener_rule_count() != 1:
            raise ValueError(f"{phase}: expected one persistent listener firewall rule")
        installed = verify(target, revision, "Windows")
        if installed != staged:
            raise ValueError(f"{phase}: installed executables differ from staged executables")
        (evidence / f"{phase}-build.json").write_text(json.dumps(installed, indent=2), encoding="utf-8")
        print(f"{phase}: confirmed registration and all four installed binaries", flush=True)
        if phase == "install":
            check_legacy_settings(target, evidence)

    uninstaller, = target.glob("unins*.exe")
    run_installer(uninstaller, evidence / "uninstall.log")
    deadline = time.monotonic() + 30
    while target.exists() and time.monotonic() < deadline:
        time.sleep(0.5)
    if target.exists() or registered_location() is not None:
        raise ValueError("Uninstall left the install directory or product registration behind")
    if listener_rule_count() != 0:
        raise ValueError("Uninstall left a persistent listener firewall rule behind")
    print("Uninstall removed the install directory, product registration and listener rule")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
