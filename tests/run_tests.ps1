<#
.SYNOPSIS
    Static checks, unit + integration tests, and gcov coverage on Windows.
.DESCRIPTION
    PowerShell equivalent of tests/run_tests.sh.
    Usage: powershell -ExecutionPolicy Bypass -File tests/run_tests.ps1 [-FormatOnly]
    Outputs: tests/out/*.log, tests/out/*.gcov
#>
param(
    [switch]$FormatOnly
)

$ErrorActionPreference = "Continue"

$TestDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $TestDir
$SrcDir  = Join-Path $RootDir "src"
$IncDir  = Join-Path $RootDir "include"
$OutDir  = Join-Path $TestDir "out"

$Sources = @("axi_iic_hal.c", "adt7410_driver.c")
$StaticSources = @("axi_iic_hal.c", "adt7410_driver.c", "main.c")
$RuleFlags = @(
    "-std=c99", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
    "-Wconversion", "-Wsign-conversion", "-Wshadow", "-Wstrict-prototypes",
    "-Wmissing-prototypes", "-Wundef", "-Wcast-qual", "-Wcast-align=strict",
    "-Wvla", "-Wstack-usage=256", "-fstack-usage", "-fno-common"
)
$TestFlags = @("-std=c99", "-Wall", "-Wextra", "-O0", "-g", "-I$IncDir", "-I$TestDir")
$Overall = 0

if (Test-Path $OutDir) {
    Remove-Item -Recurse -Force $OutDir
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host "=== 1a. Formatting & banned-construct checks (C rules §0, §13) ==="
$BadCount = 0
$SourceFiles = Get-ChildItem -Path @($SrcDir, $IncDir) -File | Where-Object {
    $_.Extension -in @(".c", ".h")
} | Sort-Object FullName

foreach ($File in $SourceFiles) {
    $RawText = [System.IO.File]::ReadAllText($File.FullName, [System.Text.UTF8Encoding]::new($false))
    $Text = $RawText -replace "`r`n", "`n"
    $Lines = $Text -split "`n"
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        $LineNum = $i + 1
        $Line = $Lines[$i]
        $Code = [regex]::Replace($Line, '/\*.*?\*/', '')
        if ($Line.Length -gt 80) {
            Write-Host "  $($File.Name):${LineNum}: line > 80 chars ($($Line.Length))"
            $BadCount++
        }
        if ($Line.Contains("`t")) {
            Write-Host "  $($File.Name):${LineNum}: TAB character"
            $BadCount++
        }
        if ($Code.Contains("//")) {
            Write-Host "  $($File.Name):${LineNum}: // comment"
            $BadCount++
        }
        if ($Code -match '\b(goto|continue|malloc|free)\b') {
            Write-Host "  $($File.Name):${LineNum}: forbidden keyword"
            $BadCount++
        }
    }
    if (-not $Text.EndsWith("`n") -or $Text.EndsWith("`n`n")) {
        Write-Host "  $($File.Name): must end with exactly one newline"
        $BadCount++
    }
}

if ($BadCount -eq 0) {
    Write-Host "[PASS] formatting: <=80 cols, no TAB, no //, no goto/continue/malloc/free, single final newline (0 findings)"
} else {
    Write-Host "[FAIL] formatting: $BadCount finding(s)"
    $Overall = 1
}

if ($FormatOnly) {
    if ($Overall -eq 0) { Write-Host "OVERALL (FormatOnly): PASS" } else { Write-Host "OVERALL (FormatOnly): FAIL" }
    exit $Overall
}

$GccCmd = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $GccCmd) {
    Write-Host ""
    Write-Host "[FAIL] gcc not found in PATH."
    Write-Host "       Install MSYS2/MinGW-w64 GCC (or run tests/run_tests.sh in WSL/MSYS2) to run compile, test, and coverage checks."
    exit 1
}

Push-Location $OutDir
try {
    Write-Host ""
    Write-Host "=== 1b. Static compiler checks (C rules §0, §12) ==="
    $Builds = @(
        @{ Name = "release"; Def = $null },
        @{ Name = "debug";   Def = "-DDEBUG_BUILD" }
    )
    foreach ($Build in $Builds) {
        foreach ($Src in $StaticSources) {
            $BaseName = [System.IO.Path]::GetFileNameWithoutExtension($Src)
            $ObjFile = "rules_$($Build.Name)_$BaseName.o"
            $LogFile = "rules_$($Build.Name)_$BaseName.log"
            $ArgsList = @() + $RuleFlags + @("-O1", "-fno-inline")
            if ($Build.Def) { $ArgsList += $Build.Def }
            $ArgsList += @("-I$IncDir", "-c", (Join-Path $SrcDir $Src), "-o", $ObjFile)

            & gcc @ArgsList 2> $LogFile
            if ($LASTEXITCODE -eq 0) {
                Write-Host "[PASS] compile $($Build.Name) $Src (strict flags, -Werror)"
            } else {
                Write-Host "[FAIL] compile $($Build.Name) $Src"
                Get-Content $LogFile
                $Overall = 1
            }
        }
    }

    $MaxStack = 0
    $MaxFn = ""
    $StackBad = $false
    foreach ($SuFile in (Get-ChildItem -Filter "rules_release_*.su")) {
        foreach ($SuLine in (Get-Content $SuFile.FullName)) {
            $Parts = $SuLine -split "`t"
            if ($Parts.Count -ge 2) {
                $Bytes = [int]$Parts[1]
                if ($Bytes -gt 256) { $StackBad = $true }
                if ($Bytes -gt $MaxStack) {
                    $MaxStack = $Bytes
                    $LocParts = $Parts[0] -split ":"
                    $MaxFn = $LocParts[-1]
                }
            }
        }
    }
    if ($StackBad) {
        Write-Host "[FAIL] stack: max $MaxStack B per function ($MaxFn), limit 256 B"
        $Overall = 1
    } else {
        Write-Host "[PASS] stack: max $MaxStack B per function ($MaxFn), limit 256 B"
    }

    Write-Host ""
    Write-Host "=== 2. Build tests with coverage ==="
    foreach ($Src in $Sources) {
        $BaseName = [System.IO.Path]::GetFileNameWithoutExtension($Src)
        & gcc @TestFlags --coverage -c (Join-Path $SrcDir $Src) -o "$BaseName.o"
        if ($LASTEXITCODE -ne 0) { throw "Failed compiling $Src" }
    }
    & gcc @TestFlags -c (Join-Path $TestDir "mock_axi_iic.c") -o "mock_axi_iic.o"
    if ($LASTEXITCODE -ne 0) { throw "Failed compiling mock_axi_iic.c" }

    & gcc @TestFlags --coverage (Join-Path $TestDir "test_adt7410_driver.c") "adt7410_driver.o" "mock_axi_iic.o" -lm -o "test_adt7410_driver.exe"
    if ($LASTEXITCODE -ne 0) { throw "Failed linking test_adt7410_driver.exe" }

    & gcc @TestFlags --coverage (Join-Path $TestDir "test_axi_iic_hal.c") "axi_iic_hal.o" -o "test_axi_iic_hal.exe"
    if ($LASTEXITCODE -ne 0) { throw "Failed linking test_axi_iic_hal.exe" }

    & gcc @TestFlags --coverage (Join-Path $TestDir "test_integration.c") "adt7410_driver.o" "axi_iic_hal.o" -o "test_integration.exe"
    if ($LASTEXITCODE -ne 0) { throw "Failed linking test_integration.exe" }

    # Application under test: main() renamed to app_main() in the TEST build only
    & gcc @TestFlags --coverage "-Dmain=app_main" -c (Join-Path $SrcDir "main.c") -o "main.o"
    if ($LASTEXITCODE -ne 0) { throw "Failed compiling main.c (test build)" }
    & gcc @TestFlags --coverage (Join-Path $TestDir "test_app.c") "main.o" "adt7410_driver.o" "mock_axi_iic.o" -o "test_app.exe"
    if ($LASTEXITCODE -ne 0) { throw "Failed linking test_app.exe" }
    Write-Host "[PASS] build"

    Write-Host ""
    Write-Host "=== 3. Run test suites ==="
    foreach ($Suite in @("test_adt7410_driver", "test_axi_iic_hal", "test_integration", "test_app")) {
        $LogPath = "$Suite.log"
        & ".\$Suite.exe" > $LogPath 2>&1
        $Rc = $LASTEXITCODE
        Get-Content $LogPath | Where-Object { $_ -match '^\[(PASS|FAIL)\]|FAIL |^SUMMARY' } | ForEach-Object { Write-Host $_ }
        if ($Rc -ne 0) { $Overall = 1 }
    }

    Write-Host ""
    Write-Host "=== 4. Structural coverage (gcov: statement + branch) ==="
    $GcovCmd = Get-Command gcov -ErrorAction SilentlyContinue
    if ($GcovCmd) {
        foreach ($Src in $StaticSources) {
            $BaseName = [System.IO.Path]::GetFileNameWithoutExtension($Src)
            $GcovOut = & gcov -b -c -o . (Join-Path $SrcDir $Src) 2>&1
            $GcovOut | Out-File -FilePath "gcov_$BaseName.log" -Encoding utf8
            $GcovOut | ForEach-Object { Write-Host "  $_" }
        }
    } else {
        Write-Host "[SKIP] gcov not installed"
    }

    Write-Host ""
    Write-Host "=== 5. Supplementary static analysis (clang-tidy, if installed) ==="
    $TidyCmd = Get-Command clang-tidy -ErrorAction SilentlyContinue
    if ($TidyCmd) {
        $Checks = '-*,clang-analyzer-*,bugprone-*,cert-*,misc-*,readability-braces-around-statements,readability-else-after-return,-misc-include-cleaner,-bugprone-easily-swappable-parameters'
        foreach ($Src in $StaticSources) {
            $BaseName = [System.IO.Path]::GetFileNameWithoutExtension($Src)
            $TidyLog = "tidy_$BaseName.log"
            & clang-tidy "-checks=$Checks" (Join-Path $SrcDir $Src) -- -std=c99 "-I$IncDir" > $TidyLog 2>&1
            $WarnCount = (Select-String -Path $TidyLog -Pattern "warning:" | Measure-Object).Count
            if ($WarnCount -eq 0) {
                Write-Host "[PASS] clang-tidy $Src (0 warnings)"
            } else {
                Write-Host "[INFO] clang-tidy $Src : $WarnCount warnings (tests/out/$TidyLog)"
            }
        }
    } else {
        Write-Host "[SKIP] clang-tidy not installed"
    }
}
finally {
    Pop-Location
}

Write-Host ""
if ($Overall -eq 0) { Write-Host "OVERALL: PASS" } else { Write-Host "OVERALL: FAIL" }
exit $Overall
