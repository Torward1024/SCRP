param([switch]$Test, [switch]$Release, [switch]$SDLTest)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$outputDir = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$compiler = $null
foreach ($prefix in @($env:MSYS2_ROOT, 'C:\msys64', 'D:\msys64')) {
    if ($prefix -and (Test-Path (Join-Path $prefix 'ucrt64\bin\g++.exe'))) {
        $compiler = Join-Path $prefix 'ucrt64\bin\g++.exe'; break
    }
}
if (-not $compiler) { $compiler = (Get-Command g++ -ErrorAction Stop).Source }
$toolchainBin = Split-Path $compiler
$env:PATH = $toolchainBin + ';' + $env:PATH
$flags = @('-std=c++17', '-Wall', '-Wextra', '-Wpedantic', "-I$root\include")
if ($Release) { $flags += @('-O2', '-DNDEBUG') } else { $flags += @('-g', '-O0') }
$sources = @('Json','Vfs','IndexedImage','Config','Xml','Anim','Grid','FlowField','PathFinder','SaveStore') | ForEach-Object { Join-Path $root "src\$_.cpp" }
if ($Test) {
    $testExe = Join-Path $outputDir 'scrp_tests.exe'
    & $compiler @flags @sources (Join-Path $root 'tests\main.cpp') '-o' $testExe
    if ($LASTEXITCODE -ne 0) { throw 'SCRP compilation failed' }
    & $testExe
    if ($LASTEXITCODE -ne 0) { throw 'SCRP core tests failed' }
    $runtimeExe=Join-Path $outputDir 'scrp_runtime_tests.exe'
    & $compiler @flags @sources (Join-Path $root 'tests/runtime.cpp') '-o' $runtimeExe
    if($LASTEXITCODE -ne 0) {throw 'Runtime test compilation failed'}
    & $runtimeExe
    if($LASTEXITCODE -ne 0) {throw 'Runtime tests failed'}
    if($SDLTest) {
        $prefix=Split-Path $toolchainBin
        $sdlSources=@('PixelWindow','Assets','Gfx','MicroFont','Lighting','Decals','Particles','Window','Audio') | ForEach-Object {Join-Path $root "src/$_.cpp"}
        $sdlExe=Join-Path $outputDir 'scrp_sdl_tests.exe'
        & $compiler @flags @sources @sdlSources (Join-Path $root 'tests/sdl.cpp') "-I$prefix/include/SDL2" "-L$prefix/lib" '-DSDL_MAIN_HANDLED' '-DSCRP_USE_SDL_MIXER' '-DSCRP_TEST_WITH_MIXER' '-lSDL2' '-lSDL2_mixer' '-o' $sdlExe
        if($LASTEXITCODE -ne 0) {throw 'SDL test compilation failed'}
        $env:SDL_VIDEODRIVER='dummy';$env:SDL_AUDIODRIVER='dummy'
        & $sdlExe
        if($LASTEXITCODE -ne 0) {throw 'SDL tests failed'}
    }
    & python '-m'  'unittest' 'discover' '-s' (Join-Path $root 'tests') '-p' 'test_sync_engine.py' '-v'
    if ($LASTEXITCODE -ne 0) { throw 'SCRP synchronization tests failed' }
} else {
    $objects = @()
    foreach ($source in $sources) {
        $object = Join-Path $outputDir (([System.IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
        & $compiler @flags '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) { throw "SCRP compilation failed: $source" }
        $objects += $object
    }
    & (Join-Path $toolchainBin 'ar.exe') 'rcs' (Join-Path $outputDir 'libscrp_core.a') @objects
    if ($LASTEXITCODE -ne 0) { throw 'SCRP archive creation failed' }
    Write-Host 'Built build\libscrp_core.a'
}
