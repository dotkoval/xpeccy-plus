# Downloads the build dependencies that aren't expected to be installed system-wide.
# Right now that is SDL2 only: Qt, MinGW and zlib come from a Qt installation.
#
#   .\fetch-deps.ps1            # get what deps.json pins, skip what is already there
#   .\fetch-deps.ps1 -Force     # re-download and re-extract
#   .\fetch-deps.ps1 -Update    # move the pin to the newest upstream release
#
# Versions are pinned on purpose: rebuilding a given tag half a year later must
# produce the same binary. -Update rewrites deps.json so a version bump lands as
# its own reviewable commit instead of happening silently on every build.

param(
	[switch]$Update,
	[switch]$Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'	# the progress bar makes downloads crawl

$srcRoot = Split-Path $PSScriptRoot -Parent
$depsDir = Join-Path $srcRoot 'build\deps'
$manifest = Join-Path $PSScriptRoot 'deps.json'

New-Item -ItemType Directory -Path $depsDir -Force | Out-Null

function Get-Pin {
	(Get-Content $manifest -Raw | ConvertFrom-Json).sdl2
}

function Set-Pin {
	param([string]$Version, [string]$Sha256)
	# patch the two values in place so the file keeps its formatting
	$text = Get-Content $manifest -Raw
	$text = $text -replace '("version"\s*:\s*")[^"]*(")', "`${1}$Version`${2}"
	$text = $text -replace '("sha256"\s*:\s*")[^"]*(")', "`${1}$Sha256`${2}"
	Set-Content $manifest $text -NoNewline
}

function Get-Zip {
	param([string]$Version)
	$url = "https://github.com/libsdl-org/SDL/releases/download/release-$Version/SDL2-devel-$Version-mingw.zip"
	$zip = Join-Path $depsDir "SDL2-devel-$Version-mingw.zip"
	if (-not (Test-Path $zip) -or $Force) {
		Write-Host "downloading $url"
		Invoke-WebRequest $url -OutFile $zip -TimeoutSec 600
	}
	return $zip
}

if ($Update) {
	# NOTE: libsdl-org/SDL hosts SDL3 as well, and its "latest" release is an SDL3
	# one, so the newest release-2.* tag is what has to be looked for here.
	$releases = Invoke-RestMethod 'https://api.github.com/repos/libsdl-org/SDL/releases?per_page=100' -TimeoutSec 60
	$newest = $releases | Where-Object { $_.tag_name -like 'release-2.*' } | Select-Object -First 1
	if (-not $newest) { throw 'no SDL2 release found' }
	$newVer = $newest.tag_name -replace '^release-', ''

	if ($newVer -eq (Get-Pin).version) {
		Write-Host "SDL2 pin is current ($newVer)"
	} else {
		$old = (Get-Pin).version
		$zip = Get-Zip $newVer
		Set-Pin $newVer (Get-FileHash $zip -Algorithm SHA256).Hash
		Write-Host "SDL2 pin: $old -> $newVer (commit packaging\deps.json)" -ForegroundColor Yellow
	}
}

$pin = Get-Pin
$target = Join-Path $depsDir "SDL2-$($pin.version)-mingw"

if ((Test-Path $target) -and -not $Force) {
	Write-Host "SDL2 $($pin.version): already in build\deps"
	return
}

$zip = Get-Zip $pin.version

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
if ($hash -ne $pin.sha256) {
	Remove-Item $zip -Force
	throw "SDL2 $($pin.version) checksum mismatch`n  expected $($pin.sha256)`n  got      $hash"
}

if (Test-Path $target) { Remove-Item $target -Recurse -Force }
$unpacked = Join-Path $depsDir "SDL2-$($pin.version)"
if (Test-Path $unpacked) { Remove-Item $unpacked -Recurse -Force }

Expand-Archive -Path $zip -DestinationPath $depsDir -Force
Rename-Item $unpacked (Split-Path $target -Leaf)

Write-Host "SDL2 $($pin.version): ready in build\deps" -ForegroundColor Green
