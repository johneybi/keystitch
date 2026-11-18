"""Observe the public v0.1.0 to candidate transition on a disposable Windows runner."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from urllib.request import urlopen

from test_windows_installer import (
    read_candidate, registered_location, remove_developer_path, require_hosted_windows,
    run_installer, run_powershell,
)
from verify_build import verify


PUBLIC_URL = ("https://github.com/johneybi/keystitch/releases/download/"
              "keystitch-v0.1.0/keystitch-windows-x64.exe")
PUBLIC_SHA256 = "cbec8d381ebe5f5b5c9d0a712e300ca11ca5ac9d5447aa237b832fd057466ffe"
PUBLIC_APP_ID = "{41036EA6-3F7A-4803-8AE0-469E5E91EFCC}"
LEGACY_SETTINGS = r"Software\Debauchee\Barrier"
CURRENT_SETTINGS = r"Software\InputLeap\InputLeap"
PORT = 24983


def startup_snapshot():
    return json.loads(run_powershell(r"""
$product = '(?i)\b(barrier\w*|input-leap\w*|keystitch\w*)\.exe\b'
@{
    services = @(Get-CimInstance Win32_Service | Where-Object {
        $_.Name -in 'Barrier','InputLeap','Input Leap','KeyStitch' -or $_.PathName -match $product
    } | Select-Object Name, StartMode, State, PathName, ProcessId)
    signInCommands = @(Get-CimInstance Win32_StartupCommand | Where-Object {
        $_.Command -match $product -or $_.Name -match 'Barrier|Input.?Leap|KeyStitch'
    } | Select-Object Name, Command, Location, User)
    scheduledTasks = @(Get-ScheduledTask | Where-Object {
        $_.TaskName -match 'Barrier|Input.?Leap|KeyStitch' -or @($_.Actions.Execute) -match $product
    } | Select-Object TaskPath, TaskName, State, @{Name='Actions';Expression={@($_.Actions.Execute)}})
} | ConvertTo-Json -Depth 5 -Compress
"""))


def exercise_saved_automatic_start(target):
    with subprocess.Popen([str(target / "input-leap.exe")], cwd=target) as gui:
        try:
            probe = run_powershell(f"""
$gui = Get-Process -Id {gui.pid}
$deadline = [DateTime]::UtcNow.AddSeconds(42)
do {{
    $gui.Refresh()
    if ($gui.HasExited) {{ throw 'Candidate GUI exited before starting sharing' }}
    $connections = Get-NetTCPConnection -LocalAddress '127.0.0.1' -LocalPort {PORT} -State Listen -ErrorAction SilentlyContinue
    foreach ($connection in $connections) {{
        $engine = Get-CimInstance Win32_Process -Filter ('ProcessId=' + $connection.OwningProcess)
        if ($engine.Name -eq 'input-leaps.exe' -and $engine.ParentProcessId -eq {gui.pid}) {{
            return $engine | Select-Object ProcessId, ParentProcessId, ExecutablePath, CommandLine |
                ConvertTo-Json -Compress
        }}
    }}
    Start-Sleep -Milliseconds 500
}} while ([DateTime]::UtcNow -lt $deadline)
throw ('Candidate did not open the expected listener. GUI title: ' + $gui.MainWindowTitle)
""")
            return json.loads(probe)
        except subprocess.CalledProcessError as error:
            raise ValueError(f"Candidate automatic sharing failed: {error.stderr.strip()}") from error
        finally:
            if gui.poll() is None:
                gui.kill()


def main(candidate, evidence):
    require_hosted_windows()
    import winreg

    old, target = (Path(os.environ["RUNNER_TEMP"]) / name
                   for name in ("KeyStitch public test", "KeyStitch candidate test"))
    if old.exists() or target.exists() or registered_location(PUBLIC_APP_ID) or registered_location():
        raise ValueError("Expected a disposable VM without either version installed")
    evidence.mkdir(parents=True, exist_ok=True)
    installer, staged = read_candidate(candidate)
    remove_developer_path()
    with urlopen(PUBLIC_URL, timeout=60) as response:
        public_bytes = response.read()
    if hashlib.sha256(public_bytes).hexdigest() != PUBLIC_SHA256:
        raise ValueError("Public v0.1.0 installer does not match the pinned SHA256")
    public_installer = old.parent / "keystitch-v0.1.0.exe"
    public_installer.write_bytes(public_bytes)
    install_args = ("/SP-", "/NOCLOSEAPPLICATIONS", "/RESTARTEXITCODE=42")
    run_installer(public_installer, evidence / "public-install.log", *install_args, f"/DIR={old}")
    if registered_location(PUBLIC_APP_ID) != old or not (old / "barrier.exe").is_file():
        raise ValueError("Public installer did not register the expected application")

    before = startup_snapshot()
    (evidence / "startup-before.json").write_text(json.dumps(before, indent=2), encoding="utf-8")
    if not any(service["Name"] == "Barrier" and service["StartMode"] == "Auto"
               and str(old / "barrierd.exe").casefold() in service["PathName"].casefold()
               for service in before["services"]):
        raise ValueError("Public installer did not register the automatic Barrier service")

    config = old.parent / "Existing Barrier sharing.conf"
    config_contents = b"section: screens\n    UpgradeWindows:\n    UpgradeMac:\nend\n"
    config.write_bytes(config_contents)
    preferences = {
        "screenName": "UpgradeWindows", "interface": "127.0.0.1", "port": str(PORT),
        "groupServerChecked": "true", "groupClientChecked": "false", "useInternalConfig": "false",
        "useExternalConfig": "true", "configFile": str(config), "serverHostname": "saved.example.invalid",
        "startedBefore": "true", "autoStart": "true", "wizardLastRun": "9",
        "language": "en", "autoConfig": "false", "autoConfigPrompted": "true", "cryptoEnabled": "false",
    }
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, LEGACY_SETTINGS) as entry:
        for name, value in preferences.items():
            winreg.SetValueEx(entry, name, 0, winreg.REG_SZ, value)

    run_installer(installer, evidence / "candidate-install.log", *install_args, f"/DIR={target}")
    after = startup_snapshot()
    (evidence / "startup-after-install.json").write_text(json.dumps(after, indent=2), encoding="utf-8")
    if registered_location() != target or verify(target, os.environ["GITHUB_SHA"], "Windows") != staged:
        raise ValueError("The expected candidate is not installed")
    print(f"After candidate installation, public application is registered at {registered_location(PUBLIC_APP_ID)}",
          flush=True)

    # Capture actual engine startup from the installed GUI with the imported preference.
    engine = exercise_saved_automatic_start(target)
    if Path(engine["ExecutablePath"]) != target / "input-leaps.exe" or str(config) not in engine["CommandLine"]:
        raise ValueError("The listener did not use the installed engine and existing config file")
    if config.read_bytes() != config_contents:
        raise ValueError("The existing sharing file was changed")
    for key in (LEGACY_SETTINGS, CURRENT_SETTINGS):
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key) as entry:
            for name, expected in preferences.items():
                value, _ = winreg.QueryValueEx(entry, name)
                if str(value) != expected:
                    raise ValueError(f"Unexpected setting {name} in {key}")
    after_launch = startup_snapshot()
    result = {
        "publicSha256": PUBLIC_SHA256,
        "candidateRevision": os.environ["GITHUB_SHA"],
        "publicInstallStillRegistered": registered_location(PUBLIC_APP_ID) == old,
        "copiedSettingNames": sorted(preferences),
        "externalConfigUnchanged": True,
        "automaticSharingOnGuiLaunch": {"port": PORT, "address": "127.0.0.1", "process": engine},
        "startupAfterGuiLaunch": after_launch,
    }
    (evidence / "upgrade-result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("Candidate opened the existing config and automatically listened on the migrated loopback port", flush=True)
    print(json.dumps(after_launch, indent=2), flush=True)


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
