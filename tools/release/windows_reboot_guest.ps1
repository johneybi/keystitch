param([switch] $AfterReboot)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$oem = 'C:\OEM'
$share = '\\host.lan\Data'
$publicInstall = 'C:\KeyStitch Test\Public'
$candidateInstall = 'C:\KeyStitch Test\Candidate'
$publicSettings = 'HKCU:\Software\Debauchee\Barrier'
$candidateSettings = 'HKCU:\Software\InputLeap\InputLeap'
$configFile = Join-Path $oem 'existing sharing.conf'
$port = 24983

function Write-Evidence($Name, $Value) {
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        try {
            $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $share $Name) -Encoding UTF8
            return
        } catch {
            if ($attempt -eq 29) { throw }
            Start-Sleep -Seconds 2
        }
    }
}

function Install-App($Installer, $Directory, $LogName) {
    $arguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /NOCLOSEAPPLICATIONS /RESTARTEXITCODE=42 /LOG="{0}" /DIR="{1}"'
    $setup = Start-Process -FilePath (Join-Path $oem $Installer) -ArgumentList ($arguments -f "$oem\$LogName", $Directory) -Wait -PassThru
    Copy-Item -LiteralPath (Join-Path $oem $LogName) -Destination $share
    if ($setup.ExitCode -ne 0) { throw "$Installer exited with code $($setup.ExitCode)" }
}

function Get-BootEvent {
    # Event record IDs remain ordered when Windows setup adjusts the guest clock.
    $event = Get-WinEvent -FilterHashtable @{
        LogName = 'System'; ProviderName = 'Microsoft-Windows-Kernel-General'; Id = 12
    } -MaxEvents 1
    $xml = [xml]$event.ToXml()
    return @{ recordId = $event.RecordId; timestamp = $xml.Event.System.TimeCreated.SystemTime }
}

function Initialize-Upgrade {
    $runtime = Join-Path $oem 'vc_redist.x64.exe'
    $signature = Get-AuthenticodeSignature -LiteralPath $runtime
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=Microsoft Corporation(,|$)') {
        throw 'The Visual C++ Redistributable signature did not verify'
    }
    $redist = Start-Process -FilePath $runtime -ArgumentList '/install /quiet /norestart' -Wait -PassThru
    if ($redist.ExitCode -notin 0, 1638, 3010) { throw "VC++ runtime exited with code $($redist.ExitCode)" }

    Install-App 'public.exe' $publicInstall 'public-install.log'
    if (-not (Test-Path -LiteralPath "$publicInstall\barrier.exe")) { throw 'Public GUI was not installed' }
    $legacy = Get-CimInstance Win32_Service -Filter "Name='Barrier'"
    if ($legacy.StartMode -ne 'Auto' -or $legacy.State -ne 'Running' -or
        $legacy.PathName.Trim('"') -ne "$publicInstall\barrierd.exe") {
        throw 'The public installer did not start the expected automatic service'
    }

    @('section: screens', '    UpgradeWindows:', '    UpgradeMac:', 'end') |
        Set-Content -LiteralPath $configFile -Encoding ascii
    $settings = @{
        screenName = 'UpgradeWindows'; interface = '127.0.0.1'; port = "$port"
        groupServerChecked = 'true'; groupClientChecked = 'false'; useInternalConfig = 'false'
        useExternalConfig = 'true'; configFile = $configFile; startedBefore = 'true'
        autoStart = 'true'; wizardLastRun = '9'; language = 'en'; autoConfig = 'false'
        autoConfigPrompted = 'true'; cryptoEnabled = 'false'
    }
    New-Item -Path $publicSettings -Force | Out-Null
    foreach ($setting in $settings.GetEnumerator()) {
        New-ItemProperty -LiteralPath $publicSettings -Name $setting.Key -Value $setting.Value -PropertyType String |
            Out-Null
    }
    Install-App 'candidate.exe' $candidateInstall 'candidate-install.log'
    $legacyAfter = Get-CimInstance Win32_Service -Filter "Name='Barrier'"
    if ($legacyAfter.StartMode -ne 'Disabled' -or $legacyAfter.State -ne 'Stopped') {
        throw 'Candidate setup did not retire the old automatic service'
    }
    if (Get-Process -Name input-leap -ErrorAction SilentlyContinue) {
        throw 'The candidate GUI started before reboot'
    }

    $baseline = @{
        boot = Get-BootEvent
        legacyBeforeUpgrade = ($legacy | Select-Object StartMode, State, PathName)
        legacyAfterUpgrade = ($legacyAfter | Select-Object StartMode, State, PathName)
        candidateSha256 = (Get-FileHash -LiteralPath "$candidateInstall\input-leap.exe").Hash
        externalConfigSha256 = (Get-FileHash -LiteralPath $configFile).Hash
        savedSettings = $settings
    }
    $baseline | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$oem\before-reboot.json" -Encoding UTF8
    Write-Evidence 'before-reboot.json' $baseline

    $user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $user
    $action = New-ScheduledTaskAction -Execute "$PSHOME\powershell.exe" -Argument (
        '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "C:\OEM\windows_reboot_guest.ps1" -AfterReboot'
    )
    Register-ScheduledTask -TaskName 'KeyStitch reboot observer' -Trigger $trigger -Action $action -User $user -RunLevel Highest |
        Out-Null
    & "$env:SystemRoot\System32\shutdown.exe" /r /t 20 /d p:4:2
    if ($LASTEXITCODE -ne 0) { throw "Windows rejected the restart request: $LASTEXITCODE" }
}

function Confirm-AfterReboot {
    $baseline = Get-Content -LiteralPath "$oem\before-reboot.json" -Raw | ConvertFrom-Json
    $boot = Get-BootEvent
    if ($boot.recordId -le $baseline.boot.recordId) {
        throw "No new Windows kernel boot event after event $($baseline.boot.recordId)"
    }
    $deadline = [DateTime]::UtcNow.AddMinutes(3)
    do {
        $gui = @(Get-CimInstance Win32_Process -Filter "Name='input-leap.exe'")
        $engine = @(Get-CimInstance Win32_Process -Filter "Name='input-leaps.exe'")
        if ($gui.Count -eq 1 -and $engine.Count -eq 1) {
            $listener = Get-NetTCPConnection -State Listen -LocalAddress '127.0.0.1' -LocalPort $port -ErrorAction SilentlyContinue |
                Where-Object { $_.OwningProcess -eq $engine[0].ProcessId }
            if ($listener) { break }
        }
        Start-Sleep -Seconds 2
    } while ([DateTime]::UtcNow -lt $deadline)

    if (-not $listener -or $gui[0].ExecutablePath -ne "$candidateInstall\input-leap.exe" -or
        $engine[0].ExecutablePath -ne "$candidateInstall\input-leaps.exe" -or
        $engine[0].ParentProcessId -ne $gui[0].ProcessId -or $gui[0].SessionId -eq 0) {
        throw 'The candidate GUI and its sharing engine did not start in the logged-in session after reboot'
    }
    $legacy = Get-CimInstance Win32_Service -Filter "Name='Barrier'"
    if ($legacy.StartMode -ne 'Disabled' -or $legacy.State -ne 'Stopped' -or
        (Get-Process -Name barrier, barrierd, barriers, barrierc -ErrorAction SilentlyContinue)) {
        throw 'A public-version process started after reboot'
    }
    if ((Get-FileHash -LiteralPath $gui[0].ExecutablePath).Hash -ne $baseline.candidateSha256 -or
        (Get-FileHash -LiteralPath $configFile).Hash -ne $baseline.externalConfigSha256) {
        throw 'The GUI or the external configuration changed during the reboot test'
    }
    $migrated = Get-ItemProperty -LiteralPath $candidateSettings
    foreach ($setting in $baseline.savedSettings.PSObject.Properties) {
        if ([string]$migrated.($setting.Name) -ne $setting.Value) {
            throw "Unexpected migrated value for $($setting.Name)"
        }
    }
    Write-Evidence 'reboot-pass.json' @{
        previousBoot = $baseline.boot; currentBoot = $boot
        gui = ($gui[0] | Select-Object ProcessId, ParentProcessId, ExecutablePath, SessionId, CreationDate)
        engine = ($engine[0] | Select-Object ProcessId, ParentProcessId, ExecutablePath, CommandLine)
        legacy = ($legacy | Select-Object State, StartMode, PathName)
        listeningAddress = '127.0.0.1'; listeningPort = $port
        copiedSettings = @($baseline.savedSettings.PSObject.Properties.Name)
    }
}

try {
    $machine = Get-CimInstance Win32_ComputerSystem
    if ($PSScriptRoot -ne $oem -or $machine.Model -notmatch 'QEMU|Standard PC') {
        throw 'This script requires the disposable QEMU guest and its OEM test directory'
    }
    if ($AfterReboot) { Confirm-AfterReboot } else { Initialize-Upgrade }
} catch {
    Write-Evidence 'reboot-failure.json' @{ stageAfterReboot = [bool]$AfterReboot; error = ($_ | Out-String) }
    exit 1
}
