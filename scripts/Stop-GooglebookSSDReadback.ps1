#requires -RunAsAdministrator
$ErrorActionPreference='Stop'
$p=Get-Process -Id 42492 -ErrorAction SilentlyContinue
if($p -and $p.ProcessName -eq 'powershell' -and $p.StartTime -gt (Get-Date '2026-10-27 11:49:00') -and $p.StartTime -lt (Get-Date '2026-10-27 11:50:00')){$p.Kill()}
$root=Split-Path -Parent $PSScriptRoot
@{state='cancelled_for_surface_verification';note='Windows USB bridge still requires restart; no writes performed by readback process.'} | ConvertTo-Json | Set-Content (Join-Path $root 'out/ssd/readback-status.json') -Encoding UTF8
