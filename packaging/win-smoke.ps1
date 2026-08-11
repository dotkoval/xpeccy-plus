# Checks that a staged folder is complete and that the binary in it starts,
# finds its own machine configuration and reaches the end of main(). --help does
# all of that: it builds the windows and reads the configuration, it just never
# shows anything or enters the event loop.
#
#   .\win-smoke.ps1 -Dist ..\build\dist\xpeccy-plus-<version>-win-x64
#
# The run gets a PATH with neither Qt nor MinGW on it, so a dll windeployqt did
# not copy fails here instead of on a machine that has no Qt installed.

param(
	[Parameter(Mandatory)][string]$Dist,
	[int]$Timeout = 60
)

$ErrorActionPreference = 'Stop'

$exe = Join-Path $Dist 'xpeccy-plus.exe'
$out = Join-Path $env:TEMP 'xpeccy-smoke.log'
$err = Join-Path $env:TEMP 'xpeccy-smoke.err'

if (-not (Test-Path $exe)) { throw "no binary at $exe" }

# the install rules, before anything is run
if (-not (Test-Path (Join-Path $Dist 'config\config.conf'))) { throw 'no config\config.conf in the staged folder' }
$roms = (Get-ChildItem (Join-Path $Dist 'config\roms') -File).Count
Write-Host "roms staged: $roms"
if ($roms -lt 20) { throw 'the rom images did not make it into the staged folder' }

$keepPath = $env:PATH
$env:PATH = "$env:SystemRoot\system32;$env:SystemRoot"
try {
	$p = Start-Process $exe -ArgumentList '--help' -WorkingDirectory $Dist `
		-RedirectStandardOutput $out -RedirectStandardError $err -PassThru -NoNewWindow
	if (-not $p.WaitForExit($Timeout * 1000)) {
		# a modal dialog is the usual reason, and there is nobody to click it away
		$p.Kill()
		Get-Content $out, $err -ErrorAction SilentlyContinue
		throw "still running after $Timeout s"
	}
} finally {
	$env:PATH = $keepPath
}

Get-Content $out -ErrorAction SilentlyContinue
$stderr = Get-Content $err -Raw -ErrorAction SilentlyContinue
if ($stderr) { Write-Host "stderr: $stderr" }

# what main() prints on its way out, so a crash cannot pass for success
if (-not (Select-String -Path $out -Pattern '^exit' -Quiet)) { throw 'did not reach the end of main()' }
# checked after the line above: a crash in a destructor happens after it
if ($p.ExitCode -ne 0) { throw "exited with 0x$('{0:X8}' -f $p.ExitCode) - main() finished, so this is a destructor" }

Write-Host "ok: $Dist" -ForegroundColor Green
