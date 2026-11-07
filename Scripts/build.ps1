<#
.SYNOPSIS
Builds Weltall and points the fixed play folder at the result.

.DESCRIPTION
Runs Scripts/package.py — compile, cook, stage, then start the package headless and check its log — and
then makes WELTALL_PLAY_DIR a directory junction to out/builds/<Configuration>/Windows, so the game is
always started from the same path.

package.py builds the committed revision (git archive HEAD): commit your changes first, or the build
will not contain them.

UE_ROOT and WELTALL_PLAY_DIR are read from the environment or from the git-ignored local.env at the
repository root. See AGENTS.md.

.PARAMETER Configuration
Development (default), DebugGame or Shipping.

.PARAMETER LinkOnly
Create or repoint the play folder without building anything.

.PARAMETER Archive
Also write the package as a release zip in out/releases.

.EXAMPLE
.\Scripts\build.ps1

.EXAMPLE
.\Scripts\build.ps1 -Configuration Shipping -Archive

.EXAMPLE
.\Scripts\build.ps1 -LinkOnly
#>

[CmdletBinding()]
param(
	[ValidateSet("Development", "DebugGame", "Shipping")]
	[string]$Configuration = "Development",
	[switch]$LinkOnly,
	[switch]$Archive
)

$ErrorActionPreference = "Stop"

$Repo = Split-Path -Parent $PSScriptRoot
$Package = Join-Path $Repo "out\builds\$Configuration\Windows"
$Launcher = Join-Path $Package "Weltall.exe"

function Read-Setting([string]$Name) {
	$FromEnvironment = Get-Item "Env:$Name" -ErrorAction SilentlyContinue
	if ($FromEnvironment -and $FromEnvironment.Value) { return $FromEnvironment.Value }
	$EnvFile = Join-Path $Repo "local.env"
	if (Test-Path -LiteralPath $EnvFile) {
		foreach ($Line in Get-Content -LiteralPath $EnvFile) {
			if ($Line -match "^\s*#" -or $Line -notmatch "=") { continue }
			$Key, $Value = $Line -split "=", 2
			if ($Key.Trim() -eq $Name) { return $Value.Trim() }
		}
	}
	return $null
}

function Set-PlayFolder {
	if (-not (Test-Path -LiteralPath $Package -PathType Container)) {
		throw "there is no package at $Package yet; build one first"
	}
	$Play = Read-Setting "WELTALL_PLAY_DIR"
	if (-not $Play) { throw "WELTALL_PLAY_DIR is not set in the environment or local.env" }
	$Play = [System.IO.Path]::GetFullPath($Play)

	$Existing = Get-Item -LiteralPath $Play -Force -ErrorAction SilentlyContinue
	if ($Existing) {
		if (-not ($Existing.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
			throw "$Play is a real folder, not a junction; move it aside (it may hold a game install) and run again"
		}
		# rmdir removes a junction itself and never what it points at.
		& cmd /c rmdir $Play
		if ($LASTEXITCODE -ne 0) { throw "could not remove the old junction at $Play" }
	}
	New-Item -ItemType Directory -Path (Split-Path -Parent $Play) -Force | Out-Null
	New-Item -ItemType Junction -Path $Play -Target $Package | Out-Null
	Write-Host "play folder: $Play -> $Package"
}

if (-not $LinkOnly) {
	$EngineRoot = Read-Setting "UE_ROOT"
	if (-not $EngineRoot -or -not (Test-Path -LiteralPath $EngineRoot -PathType Container)) {
		throw "UE_ROOT is not set, or does not point at an Unreal Engine folder (see local.env)"
	}
	$Arguments = @((Join-Path $Repo "Scripts\package.py"), $Configuration)
	if ($Archive) { $Arguments += "--archive" }
	Push-Location $Repo
	try {
		& python @Arguments
		$Status = $LASTEXITCODE
	} finally {
		Pop-Location
	}
	if ($Status -ne 0) {
		throw "package.py failed: out/builds/$Configuration.log (the build) and out/builds/$Configuration-check.log (the start-up check)"
	}
}

Set-PlayFolder
Write-Host "run: $Launcher"
