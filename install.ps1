# ==============================================================================
#  super-autotune — Windows installer (one line, fully self-contained)
#
#      powershell -Command "irm https://brodybigwood.com/pages/autotune/install.ps1 | iex"
# ==============================================================================

function Install-SuperAutotune {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    # --- CONFIG -------------------------------------------------------------------
    $ScriptUrl     = 'https://brodybigwood.com/pages/autotune/install.ps1'
    $BaseUrl       = 'https://brodybigwood.com/pages/autotune'
    $JuceUrl       = 'https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.6.zip'
    $CmakeUrl      = 'https://github.com/Kitware/CMake/releases/download/v3.29.6/cmake-3.29.6-windows-x86_64.zip'
    $VsBuildToolsUrls = @(
        'https://aka.ms/vs/17/release/vs_BuildTools.exe'
        'https://download.visualstudio.microsoft.com/download/pr/7441b53e-6bdf-4438-994f-b0c1874af85c/c82dd4350c6707906c71713f7c8d6134b6a6b82fa62ce97007dd883f0a1774c6/vs_BuildTools.exe'
    )
    $Bundle        = 'super-autotune.vst3'

    $files = @(
        'CMakeLists.txt'
        'Source/PluginProcessor.h'
        'Source/PluginProcessor.cpp'
        'Source/PluginEditor.h'
        'Source/PluginEditor.cpp'
        'Source/Parameters.h'
        'Source/Parameters.cpp'
        'Source/PitchDetector.h'
        'Source/PitchDetector.cpp'
        'Source/PitchShifter.h'
        'Source/PitchShifter.cpp'
        'Source/TuningManager.h'
        'Source/TuningManager.cpp'
        'Source/ScaleFileParser.h'
        'Source/ScaleFileParser.cpp'
        'Source/MidiManager.h'
        'Source/MidiManager.cpp'
        'Source/SpectrumAnalyzer.h'
        'Source/SpectrumAnalyzer.cpp'
        'Source/NeonLookAndFeel.h'
        'Source/NeonLookAndFeel.cpp'
        'Source/EditorComponents.h'
        'Source/EditorComponents.cpp'
        'Source/OverlapAddBuffer.h'
    )

    # --- Self-elevate -------------------------------------------------------------
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Host 'Requesting administrator privileges...' -ForegroundColor Yellow
        $tmpScript = Join-Path $env:TEMP 'install-super-autotune.ps1'
        Invoke-WebRequest -Uri $ScriptUrl -OutFile $tmpScript -UseBasicParsing
        Start-Process powershell -Verb RunAs -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$tmpScript)
        return
    }

    # --- 1. Ensure MSVC toolchain is available ------------------------------------
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = $null
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $vsPath) {
        Write-Host 'Installing Visual Studio Build Tools (this is the one big download)...' -ForegroundColor Cyan
        # Stop any leftover VS installer from a previous run so it releases its file lock.
        Get-Process -Name 'vs_BuildTools','vs_installer','vs_installershell' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
        $installer = Join-Path $env:TEMP 'vs_BuildTools.exe'
        $downloaded = $false
        if ((Test-Path $installer) -and ((Get-Item $installer).Length -gt 1MB)) {
            Write-Host 'Build Tools bootstrapper already downloaded; reusing it.' -ForegroundColor Green
            $downloaded = $true
        }
        else {
            foreach ($url in $VsBuildToolsUrls) {
                for ($attempt = 1; $attempt -le 3; $attempt++) {
                    Write-Host "  downloading (attempt $attempt/3): $url" -ForegroundColor White
                    try {
                        Invoke-WebRequest -Uri $url -OutFile $installer -UseBasicParsing
                        if ((Test-Path $installer) -and ((Get-Item $installer).Length -gt 1MB)) {
                            $downloaded = $true
                            break
                        }
                    }
                    catch {
                        Write-Host "  failed: $($_.Exception.Message)" -ForegroundColor Yellow
                    }
                    Start-Sleep -Seconds 2
                }
                if ($downloaded) { break }
            }
        }
        if (-not $downloaded) {
            Write-Host 'ERROR: could not download Visual Studio Build Tools from any mirror.' -ForegroundColor Red
            return
        }
        Write-Host 'Build Tools downloaded. Installing (this takes 10-20 min)...' -ForegroundColor Green

        # Targeted outbound rules: only the VS bootstrapper + its installer engine,
        # only over HTTPS (TCP 443). Each rule is scoped to a single executable.
        Get-NetFirewallRule -DisplayName 'super-autotune*' -ErrorAction SilentlyContinue | Remove-NetFirewallRule -ErrorAction SilentlyContinue

        $vsInstallerDir = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
        $targets = @(
            @{ Name = 'bootstrapper';      Path = $installer },
            @{ Name = 'setup';             Path = (Join-Path $vsInstallerDir 'setup.exe') },
            @{ Name = 'vs_installer';      Path = (Join-Path $vsInstallerDir 'vs_installer.exe') },
            @{ Name = 'vs_installershell'; Path = (Join-Path $vsInstallerDir 'vs_installershell.exe') }
        )
        foreach ($t in $targets) {
            try {
                New-NetFirewallRule -DisplayName "super-autotune: $($t.Name)" -Direction Outbound -Action Allow -Program $t.Path -Profile Any -Enabled True | Out-Null
                Write-Host "  firewall rule OK: $($t.Name)" -ForegroundColor Green
            }
            catch {
                Write-Host "  firewall rule FAILED ($($t.Name)): $($_.Exception.Message)" -ForegroundColor Red
            }
        }

        # The VS Build Tools SFX self-extracts the real downloader (vs_setup_bootstrapper.exe)
        # to a RANDOM temp path every run, which program-targeted rules can't match. Fix:
        # extract it ourselves to a stable path we control, target that exact exe, and run it.
        $7zDir = Join-Path $env:TEMP 'super-autotune-7z'
        $7za   = Join-Path $7zDir '7za.exe'
        if (-not (Test-Path $7za)) {
            New-Item -ItemType Directory -Path $7zDir -Force | Out-Null
            $7zZip = Join-Path $7zDir '7za.zip'
            Invoke-WebRequest -Uri 'https://www.7-zip.org/a/7za920.zip' -OutFile $7zZip -UseBasicParsing
            Expand-Archive -Path $7zZip -DestinationPath $7zDir -Force
        }
        $bootDir = Join-Path $env:TEMP 'super-autotune-vs-bootstrapper'
        if (Test-Path $bootDir) { Remove-Item $bootDir -Recurse -Force }
        & $7za x $installer -o"$bootDir" -y | Out-Null
        $bootExe = Join-Path $bootDir 'vs_bootstrapper_d15\vs_setup_bootstrapper.exe'
        if (-not (Test-Path $bootExe)) {
            Write-Host 'ERROR: could not extract vs_setup_bootstrapper.exe from the SFX.' -ForegroundColor Red
            return
        }
        New-NetFirewallRule -DisplayName 'super-autotune: setup_bootstrapper' -Direction Outbound -Action Allow -Program $bootExe -Profile Any -Enabled True | Out-Null
        Write-Host "  firewall rule OK: setup_bootstrapper ($bootExe)" -ForegroundColor Green

        $proc = Start-Process $bootExe -ArgumentList '--quiet','--wait','--norestart','--nocache','--add','Microsoft.VisualStudio.Workload.VCTools','--add','Microsoft.VisualStudio.Component.VC.CMake.Project','--includeRecommended' -Wait -PassThru
        Write-Host "Installer exit code: $($proc.ExitCode)" -ForegroundColor Cyan
        $logs = Get-ChildItem $env:TEMP -Filter 'dd_*.log' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 2
        foreach ($log in $logs) {
            Write-Host "--- $($log.Name) (last 15 lines) ---" -ForegroundColor Yellow
            Get-Content $log.FullName -Tail 15
        }

        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        }
        if ($vsPath) {
            Write-Host 'Build Tools install succeeded.' -ForegroundColor Green
        }
        else {
            Write-Host 'Build Tools install FAILED - Visual Studio not found after installer ran.' -ForegroundColor Red
        }
    }

    # Locate cmake (bundled with VS, or on PATH)
    $cmake = $null
    if ($vsPath) {
        $candidate = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $candidate) { $cmake = $candidate }
    }
    if (-not $cmake) { $cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source }
    if (-not $cmake) {
        $cmakeDir = Join-Path $env:TEMP 'cmake-portable'
        $existingCmake = Get-ChildItem -Path $cmakeDir -Recurse -Filter 'cmake.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($existingCmake) {
            $cmake = $existingCmake.FullName
            Write-Host 'Portable cmake already downloaded; reusing it.' -ForegroundColor Green
        }
        else {
            Write-Host 'Downloading portable cmake...' -ForegroundColor Cyan
            $cmakeZip = Join-Path $env:TEMP 'cmake.zip'
            try {
                Invoke-WebRequest -Uri $CmakeUrl -OutFile $cmakeZip -UseBasicParsing
                if (Test-Path $cmakeDir) { Remove-Item $cmakeDir -Recurse -Force }
                Expand-Archive -Path $cmakeZip -DestinationPath $cmakeDir -Force
                $cmakeExe = Get-ChildItem -Path $cmakeDir -Recurse -Filter 'cmake.exe' | Select-Object -First 1
                if ($cmakeExe) { $cmake = $cmakeExe.FullName }
                Remove-Item $cmakeZip -Force -ErrorAction SilentlyContinue
            }
            catch {
                Write-Host "ERROR: failed to download cmake: $($_.Exception.Message)" -ForegroundColor Red
            }
        }
    }
    if (-not $cmake) {
        Write-Host 'ERROR: cmake not found.' -ForegroundColor Red
        return
    }

    # --- 2. Download source + JUCE -------------------------------------------------
    # Persistent location (not %TEMP%) so incremental builds survive between runs.
    $dataDir = Join-Path $env:LOCALAPPDATA 'super-autotune'
    $src = Join-Path $dataDir 'src'
    New-Item -ItemType Directory -Path $src -Force | Out-Null

    Write-Host 'Downloading source...' -ForegroundColor Cyan
    foreach ($f in $files) {
        $dest = Join-Path $src $f
        New-Item -ItemType Directory -Path (Split-Path $dest) -Force | Out-Null
        $tmp = "$dest.download"
        try {
            Invoke-WebRequest -Uri "$BaseUrl/$f" -OutFile $tmp -UseBasicParsing
        }
        catch {
            Write-Host "ERROR: failed to download $BaseUrl/$f : $($_.Exception.Message)" -ForegroundColor Red
            Remove-Item $tmp -Force -ErrorAction SilentlyContinue
            return
        }
        # Only replace if content actually changed, so unchanged files keep their
        # timestamp and MSBuild skips recompiling them.
        if ((Test-Path $dest) -and ((Get-FileHash $tmp -Algorithm SHA256).Hash -eq (Get-FileHash $dest -Algorithm SHA256).Hash)) {
            Remove-Item $tmp -Force
        }
        else {
            Move-Item $tmp $dest -Force
        }
    }

    $juceRoot = Join-Path $dataDir 'juce'
    New-Item -ItemType Directory -Path $juceRoot -Force | Out-Null
    $juceDir = Get-ChildItem -Path $juceRoot -Directory -Filter 'JUCE-*' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $juceDir) {
        Write-Host 'Downloading JUCE...' -ForegroundColor Cyan
        $juceZip = Join-Path $juceRoot 'juce.zip'
        try {
            Invoke-WebRequest -Uri $JuceUrl -OutFile $juceZip -UseBasicParsing
        }
        catch {
            Write-Host "ERROR: failed to download $JuceUrl : $($_.Exception.Message)" -ForegroundColor Red
            return
        }
        Expand-Archive -Path $juceZip -DestinationPath $juceRoot -Force
        Remove-Item $juceZip -Force -ErrorAction SilentlyContinue
        $juceDir = Get-ChildItem -Path $juceRoot -Directory -Filter 'JUCE-*' | Select-Object -First 1
    }
    else {
        Write-Host 'JUCE already downloaded; reusing it.' -ForegroundColor Green
    }

    # --- 3. Build ------------------------------------------------------------------
    Write-Host 'Building VST3...' -ForegroundColor Cyan
    $build = Join-Path $dataDir 'build'
    $jucePath = $juceDir.FullName

    # Keep each configure argument as its own array element: "-DJUCE_DIR=$jucePath"
    # has to arrive as a single argument or the path is not expanded.
    $configureArgs = @(
        '-S', $src,
        '-B', $build,
        "-DJUCE_DIR=$jucePath",
        '-DCMAKE_BUILD_TYPE=Release',
        '-G', 'Visual Studio 17 2022',
        '-A', 'x64'
    )

    # Clear any half-written .vst3 bundle before building. An interrupted build
    # leaves a partial bundle behind, and JUCE's juce_vst3_helper then crashes with
    # an access violation (MSBuild reports it as "MSB3073 ... exited with code
    # -1073741819") while writing moduleinfo.json — which reads as a mysterious
    # failure with no indication that a stale bundle is the cause.
    #
    # Only the bundle is removed, never the object files, so the build stays
    # incremental.
    $artefacts = Join-Path $build 'super-autotune_artefacts'
    if (Test-Path $artefacts) {
        Get-ChildItem -Path $artefacts -Recurse -Directory -Filter '*.vst3' -ErrorAction SilentlyContinue |
            ForEach-Object { Remove-Item $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }
    }

    & $cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { Write-Host 'ERROR: cmake configure failed.' -ForegroundColor Red; return }

    & $cmake --build $build --target super-autotune_VST3 --config Release
    if ($LASTEXITCODE -ne 0) {
        # Still failing, so it is not an incremental-build artefact. Rebuild once
        # from an empty directory rather than leaving a wedged build tree that fails
        # every subsequent run and has to be cleaned by hand.
        Write-Host 'Build failed; retrying once from a clean build directory...' -ForegroundColor Yellow
        if (Test-Path $build) { Remove-Item $build -Recurse -Force -ErrorAction SilentlyContinue }

        & $cmake @configureArgs
        if ($LASTEXITCODE -ne 0) { Write-Host 'ERROR: cmake configure failed.' -ForegroundColor Red; return }

        & $cmake --build $build --target super-autotune_VST3 --config Release
        if ($LASTEXITCODE -ne 0) {
            Write-Host 'ERROR: build failed, also after a clean rebuild.' -ForegroundColor Red
            return
        }
    }

    # --- 4. Install ----------------------------------------------------------------
    $bundle = Get-ChildItem -Path $build -Recurse -Directory -Filter '*.vst3' | Select-Object -First 1
    if (-not $bundle) { Write-Host 'ERROR: build produced no .vst3 bundle.' -ForegroundColor Red; return }

    $vst3Root = Join-Path $env:ProgramFiles 'Common Files\VST3'
    $dest = Join-Path $vst3Root $Bundle
    if (-not (Test-Path $vst3Root)) { New-Item -ItemType Directory -Path $vst3Root -Force | Out-Null }
    # Replacing the installed copy fails while a host has the plugin open, and the
    # raw PowerShell error ("Access to the path is denied") gives no hint that the
    # fix is simply to close the DAW. Catch it and say what to do.
    try {
        if (Test-Path $dest) { Remove-Item $dest -Recurse -Force -ErrorAction Stop }
        Copy-Item $bundle.FullName $dest -Recurse -ErrorAction Stop
    }
    catch {
        Write-Host ''
        Write-Host "ERROR: could not replace $dest" -ForegroundColor Red
        Write-Host "       $($_.Exception.Message)" -ForegroundColor Red
        Write-Host '       Close anything that has the plugin loaded (FL Studio), then re-run.' -ForegroundColor Yellow
        return
    }

    Write-Host ''
    Write-Host "SUCCESS: installed to $dest" -ForegroundColor Green
    Write-Host 'In FL Studio: Options > Manage Plugins > Start scan.' -ForegroundColor White
}

Install-SuperAutotune
