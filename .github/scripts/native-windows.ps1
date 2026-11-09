param([ValidateSet('clean', 'build', 'normalize')][string]$Action = 'build')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$source = 'test/plugins/Potrace.bundle/contents/windows64/Potrace.4DX'
$destination = 'test/Plugins/Potrace.bundle/Contents/Windows64/Potrace.4DX'

function Assert-Safe([string]$Path) {
    $item = [IO.Path]::GetFullPath((Join-Path $PWD $Path))
    $root = [IO.Path]::GetFullPath($PWD)
    while ($item -ne $root) {
        if (Test-Path -LiteralPath $item) {
            if ((Get-Item -LiteralPath $item -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse point in native output path: $Path"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($item)
        if (-not $parent -or $parent -eq $item) { throw "Output outside repository: $Path" }
        $item = $parent
    }
}

function Remove-Native([string]$Path) {
    Assert-Safe $Path
    if (Test-Path -LiteralPath $Path) {
        $item = Get-Item -LiteralPath $Path -Force
        if ($item.PSIsContainer) {
            foreach ($child in Get-ChildItem -LiteralPath $Path -Force -Recurse) {
                if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw "Reparse point in stale build tree: $($child.FullName)"
                }
            }
        }
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
    if (Test-Path -LiteralPath $Path) { throw "Native output survived cleanup: $Path" }
}

function Assert-X64([string]$Path) {
    Assert-Safe $Path
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Windows binary missing at exact MSBuild output: $Path"
    }
    $bytes = [IO.File]::ReadAllBytes((Join-Path $PWD $Path))
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "Not a PE binary: $Path"
    }
    $offset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($offset -lt 64 -or $offset -gt ($bytes.Length - 26) -or
        [BitConverter]::ToUInt32($bytes, $offset) -ne 0x4550 -or
        [BitConverter]::ToUInt16($bytes, $offset + 4) -ne 0x8664 -or
        [BitConverter]::ToUInt16($bytes, $offset + 24) -ne 0x20b) {
        throw "Expected AMD64 PE32+ binary: $Path"
    }
}

if ($Action -in @('clean', 'build')) {
    # Keep committed bundle resources and the other platform's binary.
    foreach ($path in @($source, $destination, 'Objs', 'test/Plugins/Default')) {
        Remove-Native $path
    }
}
if ($Action -eq 'build') {
    & msbuild 'Potrace.sln' '/p:Configuration=Release' '/p:Platform=x64' `
        '/p:PlatformToolset=v143' '/p:WindowsTargetPlatformVersion=10.0' '/m'
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed: $LASTEXITCODE" }
}
if ($Action -in @('build', 'normalize')) {
    Assert-X64 $source
    Assert-Safe $destination
    New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
    $resolvedSource = (Resolve-Path -LiteralPath $source).ProviderPath
    $resolvedDestination = if (Test-Path -LiteralPath $destination) {
        (Resolve-Path -LiteralPath $destination).ProviderPath
    } else { $null }
    $identical = $resolvedDestination -and (
        ($resolvedSource -ceq $resolvedDestination) -or
        (($IsWindows) -and ($resolvedSource -ieq $resolvedDestination)) -or
        ((Get-FileHash -LiteralPath $source).Hash -eq
         (Get-FileHash -LiteralPath $destination).Hash))
    if (-not $identical) {
        Copy-Item -LiteralPath $source -Destination $destination -Force
    } else {
        Write-Host 'Already at canonical location; skipping copy.'
    }
    Assert-X64 $destination
}
