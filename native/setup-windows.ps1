# Checks that this PC can build Radiante, and with -Install fetches what is missing.
#
#   powershell -ExecutionPolicy Bypass -File native\setup-windows.ps1            # only says what is there and what is not
#   powershell -ExecutionPolicy Bypass -File native\setup-windows.ps1 -Install   # also installs the missing parts
#
# What the build needs (docs/dev/building.md): JDK 25, Visual Studio 2026 or its Build Tools with the C++ workload,
# the Vulkan SDK, Python 3 (for the shader checks) and the NGX libraries of NVIDIA's DLSS SDK, which are not in the
# repository. -Install uses winget for the first four (each asks for administrator rights itself) and git for the DLSS
# libraries. Nothing is changed without -Install.
param([switch]$Install)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$workflow = Get-Content (Join-Path $repo '.github\workflows\build.yml') -Raw
# The versions the CI builds with, so a PC set up here builds what the CI builds.
$dlssTag = if ($workflow -match 'DLSS_SDK_TAG:\s*(\S+)') { $Matches[1] } else { 'v310.9.1' }
$vulkanMinimum = if ($workflow -match 'VULKAN_SDK_VERSION:\s*(\S+)') { [version]$Matches[1] } else { [version]'1.4.341.1' }
$missing = @()

function Report($name, $ok, $detail) {
    Write-Host ("{0} {1}: {2}" -f $(if ($ok) { '[ok]     ' } else { '[missing]' }), $name, $detail)
}

function Winget($id, $override) {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Write-Host "          winget is not available: install $id by hand"
        return
    }
    $arguments = @('install', '--id', $id, '--source', 'winget', '--accept-package-agreements', '--accept-source-agreements')
    if ($override) { $arguments += @('--override', $override) }
    & winget @arguments
}

# JDK 25
$java = Get-Command java -ErrorAction SilentlyContinue
$javaVersion = if ($java) { (& cmd /c 'java -version 2>&1' | Select-Object -First 1) } else { '' }
$javaOk = $javaVersion -match '"25[.\d]*'
Report 'JDK 25' $javaOk $(if ($java) { $javaVersion } else { 'java is not on PATH' })
if (-not $javaOk) {
    $missing += 'JDK 25'
    if ($Install) { Winget 'EclipseAdoptium.Temurin.25.JDK' $null }
}

# Visual Studio 2026 or Build Tools, with the C++ tools and CMake (what native\build-windows.bat looks for).
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = if (Test-Path $vswhere) {
    & $vswhere -latest -products * -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
} else { $null }
$cmake = if ($vs) { Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' } else { $null }
$vsOk = $vs -and (Test-Path $cmake)
Report 'Visual Studio 2026 C++ tools and CMake' $vsOk $(if ($vs) { $vs } else { 'not found' })
if (-not $vsOk) {
    $missing += 'Visual Studio 2026 C++ tools'
    if ($Install) {
        Winget 'Microsoft.VisualStudio.BuildTools' '--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait'
    }
}

# Vulkan SDK: VULKAN_SDK is set for new terminals by its installer, so the folder is looked at too.
$sdk = $env:VULKAN_SDK
if (-not $sdk -and (Test-Path 'C:\VulkanSDK')) {
    $sdk = Get-ChildItem 'C:\VulkanSDK' -Directory | Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
$sdkVersion = if ($sdk) { try { [version](Split-Path -Leaf $sdk) } catch { $null } } else { $null }
$sdkOk = $sdk -and (Test-Path (Join-Path $sdk 'Bin\glslangValidator.exe')) -and (-not $sdkVersion -or $sdkVersion -ge $vulkanMinimum)
Report "Vulkan SDK $vulkanMinimum or newer" $sdkOk $(if ($sdk) { $sdk } else { 'not found' })
if ($sdk -and -not $env:VULKAN_SDK) {
    Write-Host '          VULKAN_SDK is not set in this terminal: open a new one before building.'
}
if (-not $sdkOk) {
    $missing += 'Vulkan SDK'
    if ($Install) { Winget 'KhronosGroup.VulkanSDK' $null }
}

# Python 3, for check_shaders.py and the layout check the native build runs.
$python = Get-Command python -ErrorAction SilentlyContinue
$pythonVersion = if ($python) { (& python --version 2>&1) } else { '' }
$pythonOk = $pythonVersion -match 'Python 3'
Report 'Python 3' $pythonOk $(if ($pythonOk) { $pythonVersion } else { 'python is not on PATH' })
if (-not $pythonOk) {
    $missing += 'Python 3'
    if ($Install) { Winget 'Python.Python.3.13' $null }
}

# The NGX libraries of the DLSS SDK, at the version the build expects.
$dlss = Join-Path $repo 'native\extern\DLSS\lib\Windows_x86_64'
$dlssOk = Test-Path $dlss
Report "DLSS SDK libraries ($dlssTag)" $dlssOk $dlss
if (-not $dlssOk) {
    $missing += 'DLSS SDK libraries'
    if ($Install) {
        $temp = Join-Path ([IO.Path]::GetTempPath()) ("radiante-dlss-" + [Guid]::NewGuid().ToString('N'))
        & git clone --depth 1 --branch $dlssTag https://github.com/NVIDIA/DLSS.git $temp
        New-Item -ItemType Directory -Force (Split-Path -Parent $dlss) | Out-Null
        Copy-Item -Recurse (Join-Path $temp 'lib\Windows_x86_64') $dlss
        Remove-Item -Recurse -Force $temp
    }
}

Write-Host ''
if ($missing.Count -eq 0) {
    Write-Host 'Everything is here. Build with native\build-windows.bat, then gradlew.bat build.'
} elseif ($Install) {
    Write-Host 'Installed what was missing. Open a new terminal (for PATH and VULKAN_SDK) and run this again to check.'
} else {
    Write-Host ('Missing: ' + ($missing -join ', ') + '. Run this again with -Install to fetch them.')
    exit 1
}
