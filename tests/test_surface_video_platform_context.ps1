# Exercise the collector's actual query block without querying this workstation.
$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../scripts/Collect-SurfaceVideoReference.ps1') -Raw
$start = $source.IndexOf('# Read-only platform context:')
$end = $source.IndexOf('# End read-only platform context.')
if ($start -lt 0 -or $end -le $start) { throw 'Platform block not found.' }
$block = [scriptblock]::Create($source.Substring($start, $end - $start))
function Confirm-SecureBootUEFI {
    if ($scenario -eq 'failure') { throw 'fixture unavailable' }
    return ($scenario -eq 'enabled')
}
function Get-CimInstance {
    param($Namespace, $ClassName)
    if ($Namespace -ne 'root\Microsoft\Windows\DeviceGuard' -or $ClassName -ne 'Win32_DeviceGuard') {
        throw 'Unexpected CIM query.'
    }
    if ($scenario -eq 'failure') { throw 'fixture unavailable' }
    if ($scenario -eq 'empty') { return $null }
    [pscustomobject]@{ VirtualizationBasedSecurityStatus = 2; SecurityServicesConfigured = @(1,2); SecurityServicesRunning = @(2) }
}
function Record-Issue($Step, $Message) { $issues.Add($Step) }
function Save-Json($Name, $Value) {
    if ($Name -ne 'platform-state.json') { throw 'Unexpected output.' }
    $script:captured = $Value
}
foreach ($scenario in @('enabled','disabled','failure','empty')) {
    $computer = [pscustomobject]@{ HypervisorPresent = $true }
    $issues = New-Object 'System.Collections.Generic.List[string]'
    $script:captured = $null
    . $block
    if (-not $captured.hypervisorPresent) { throw 'Hypervisor context lost.' }
    if ($scenario -eq 'failure') {
        if ($null -ne $captured.secureBootEnabled -or $null -ne $captured.deviceGuard -or $issues.Count -ne 2) {
            throw 'Failed queries must stay unknown and record issues.'
        }
    } elseif ($scenario -eq 'empty') {
        if ($null -ne $captured.deviceGuard -or $issues.Count -ne 1) { throw 'Missing instance was accepted.' }
    } else {
        if ($captured.secureBootEnabled -ne ($scenario -eq 'enabled') -or
            $captured.deviceGuard.VirtualizationBasedSecurityStatus -ne 2 -or
            $issues.Count -ne 0) { throw 'Valid platform state changed.' }
    }
}
Write-Output 'PASS: enabled, disabled, failed and empty platform queries; no live system queries.'
