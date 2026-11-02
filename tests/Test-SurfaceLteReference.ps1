# Offline selector regression. Never runs CIM, exports, firmware reads or a modem.
param(
    [string]$Collector = (Join-Path $PSScriptRoot '../scripts/Collect-SurfaceLteReference.ps1'),
    [switch]$ObserveOnly
)
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Resolve-Path $Collector), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($name in @('prefixPattern','selected')) {
    $nodes = @($ast.FindAll({param($n) $n -is [Management.Automation.Language.AssignmentStatementAst] -and $n.Left.Extent.Text -eq ('$' + $name)}, $true))
    if ($nodes.Count -ne 1) { throw "Expected one $name assignment" }
    Set-Variable -Name ($name + 'Expression') -Value $nodes[0].Extent.Text
}
Invoke-Expression $prefixPatternExpression
$cases = @(
    @('surfaceprox_mbb.inf', 'Net', $true),
    @('surfaceprox_subextmpss.inf', 'Extension', $true),
    @('qcremotefs8180.inf', 'System', $true),
    @('qcipa8180.inf', 'System', $true),
    @('qcrevrmnet8180.inf', 'NetTrans', $true),
    @('qcsocpartition.inf', 'System', $true),
    @('qcpil8180.inf', 'System', $true),
    @('qcscm8180.inf', 'System', $true),
    @('qcsmmu8180.inf', 'System', $true),
    @('qciommu8180.inf', 'System', $true),
    @('netwmbclass.inf', 'Net', $true),
    @('netvchannel.inf', 'Net', $true),
    @('qcipcrouter8180.inf', 'System', $true),
    @('qcglink8180.inf', 'System', $true),
    @('qcrpen8180.inf', 'System', $true),
    @('qctftpkmdf.inf', 'System', $true),
    @('qcpdsr.inf', 'System', $true),
    @('surfaceprox_pep.inf', 'System', $true),
    @('surfaceprox_subsys.inf', 'System', $true),
    @('qcdx8180.inf', 'Display', $false),
    @('qcwlan8180.inf', 'Net', $false),
    @('qcauddev8180.inf', 'MEDIA', $false),
    @('qcsubsys_ext_cdsp8180.inf', 'Extension', $false),
    @('surfaceprox_subextadsp.inf', 'Extension', $false),
    @('unrelated.inf', 'System', $false)
)
$AllDrivers = $false
$observed = [ordered]@{}
foreach ($case in $cases) {
    $original = $case[0]; $package = [pscustomobject]@{ClassName = $case[1]}
    Invoke-Expression $selectedExpression
    $observed[$original] = [bool]$selected
    if (-not $ObserveOnly -and [bool]$selected -ne $case[2]) { throw "Unexpected default selection: $original" }
}
if ($ObserveOnly) { $observed | ConvertTo-Json -Compress; return }
$AllDrivers = $true; $original = 'unrelated.inf'; $package = [pscustomobject]@{ClassName = 'System'}
Invoke-Expression $selectedExpression
if (-not $selected) { throw 'Explicit AllDrivers opt-in failed' }
Write-Output ('PASS: ' + $cases.Count + ' default package cases; explicit AllDrivers opt-in; PowerShell AST')
