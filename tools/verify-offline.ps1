<#
.SYNOPSIS
    Checks that a WhisperVTT build contains no networking code.

.DESCRIPTION
    Fails if WhisperVTT.exe, whisper.dll or any ggml DLL (the speech
    engine and its CPU and GPU backends) links a Windows networking
    library, or if the package ships Qt's networking module, a
    network-capable Qt plugin, or ggml's RPC backend (which offloads work
    to other machines).  CI runs this on every build; you can run it on a
    downloaded or installed copy too.

    Note: Qt6Core.dll itself imports ws2_32.dll for small helpers (such as
    looking up the computer's name), so ws2_32.dll appears among the
    process's loaded modules.  It is never used to open a connection:
    while WhisperVTT runs, it owns no TCP or UDP endpoints, which you can
    confirm in Resource Monitor or with
        Get-NetTCPConnection, Get-NetUDPEndpoint -OwningProcess <pid>

    Requires dumpbin.exe (Visual Studio C++ tools) on PATH or installed in
    the default location.

.EXAMPLE
    .\tools\verify-offline.ps1 "$env:LOCALAPPDATA\Programs\WhisperVTT"
#>
param(
    [Parameter(Mandatory)] [string] $Path
)
$ErrorActionPreference = 'Stop'
$Path = (Resolve-Path $Path).Path

$dumpbin = (Get-Command dumpbin.exe -ErrorAction SilentlyContinue).Source
if (-not $dumpbin) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $dumpbin = Get-ChildItem "$vs\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" |
               Select-Object -Last 1 -ExpandProperty FullName
}
if (-not $dumpbin) { throw 'dumpbin.exe not found; install the Visual Studio C++ tools.' }

# Windows libraries for sockets, HTTP, DNS, network configuration and
# downloads, plus Qt's networking module.
$networking = @(
    'ws2_32', 'wsock32', 'mswsock', 'winhttp', 'wininet', 'urlmon', 'httpapi',
    'websocket', 'dnsapi', 'iphlpapi', 'netapi32', 'Qt6Network'
)

if (-not (Test-Path (Join-Path $Path 'WhisperVTT.exe'))) { throw "WhisperVTT.exe not found in $Path" }

# Everything built from this repository: the app and the speech engine.
$binaries = Get-ChildItem $Path -File |
            Where-Object { $_.Name -match '^(WhisperVTT\.exe|whisper\.dll|ggml.*\.dll)$' }

$failures = @()
foreach ($bin in $binaries) {
    $imports = & $dumpbin /nologo /dependents $bin.FullName |
               Where-Object { $_ -match '^\s+(\S+)\.dll\s*$' } |
               ForEach-Object { $Matches[1] }
    foreach ($lib in $imports) {
        if ($networking -contains $lib) { $failures += "$($bin.Name) links $lib.dll" }
    }
}

$shipped = Get-ChildItem $Path -Recurse -File |
           Where-Object { $_.Name -match '^(Qt6Network|qtuiotouchplugin|ggml-rpc)' -or
                          $_.DirectoryName -match '\\plugins\\(tls|networkinformation|generic)$' }
foreach ($f in $shipped) { $failures += "package ships $($f.FullName.Substring($Path.Length).TrimStart('\'))" }

if ($failures) {
    $failures | ForEach-Object { Write-Host "FAIL  $_" -ForegroundColor Red }
    exit 1
}
Write-Host "OK  No networking libraries linked by: $($binaries.Name -join ', ')"
Write-Host "OK  No Qt networking module, network-capable plugins or ggml RPC backend in $Path"
