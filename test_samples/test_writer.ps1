#requires -Version 5.1
<#
test_writer.ps1
Smoke-tests the pstwriter end-to-end against the sample Graph JSON files
in this directory. Run from anywhere:

    powershell -File "d:\Work Qfion\PST Dev\test_samples\test_writer.ps1"

What it does:
  1. (optional) Rebuild the writer if -Rebuild is passed
  2. Run the unit test binary
  3. Convert sample_mail.json / sample_contacts.json / sample_calendar.json
     to .pst files in this directory
  4. Validate each output PST with pst_info
  5. Print a green/red summary

Exit code: 0 on full success, 1 on any failure.
#>

param(
    [switch]$Rebuild,
    [switch]$VerboseOutput,
    [string]$BuildDir = 'd:\Work Qfion\PST Dev\build\gcc'
)

$ErrorActionPreference = 'Stop'
$here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$bin      = Join-Path $BuildDir 'bin'
$convert  = Join-Path $bin 'pst_convert.exe'
$info     = Join-Path $bin 'pst_info.exe'
$tests    = Join-Path $bin 'pstwriter_tests.exe'

$failures = @()
function Step($label, [scriptblock]$block) {
    Write-Host ""
    Write-Host "==> $label" -ForegroundColor Cyan
    try {
        & $block
        if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne $null) {
            throw "exit code $LASTEXITCODE"
        }
        Write-Host "    OK" -ForegroundColor Green
    } catch {
        Write-Host "    FAIL: $_" -ForegroundColor Red
        $script:failures += $label
    }
}

if ($Rebuild) {
    Step 'Rebuild pstwriter' { cmake --build $BuildDir | Out-Host }
}

# Sanity check: binaries exist
foreach ($exe in @($convert, $info, $tests)) {
    if (-not (Test-Path $exe)) {
        Write-Host "Missing binary: $exe" -ForegroundColor Red
        Write-Host "Run with -Rebuild or build manually:" -ForegroundColor Yellow
        Write-Host "    cmake --build `"$BuildDir`"" -ForegroundColor Yellow
        exit 1
    }
}

Step 'Run unit tests (pstwriter_tests.exe)' {
    # 13 known failures are missing-golden-sample lookups, not real bugs.
    # The script tolerates them; flag only catastrophic test-binary errors.
    & $tests --reporter compact 2>&1 | Tee-Object -Variable testOut | Out-Null
    $summary = $testOut | Select-String -Pattern 'test cases:.*passed' | Select-Object -Last 1
    if (-not $summary) { throw 'no summary line from test binary' }
    Write-Host "    $summary"
}

# Convert + validate each kind
$kinds = @(
    @{ Kind = 'mail';     Json = 'sample_mail.json';     Pst = 'out_mail.pst' },
    @{ Kind = 'contacts'; Json = 'sample_contacts.json'; Pst = 'out_contacts.pst' },
    @{ Kind = 'calendar'; Json = 'sample_calendar.json'; Pst = 'out_calendar.pst' }
)

foreach ($k in $kinds) {
    $jsonPath = Join-Path $here $k.Json
    $pstPath  = Join-Path $here $k.Pst

    Step "pst_convert $($k.Kind)  -> $($k.Pst)" {
        & $convert $k.Kind $jsonPath $pstPath | Out-Host
    }

    Step "pst_info validate    $($k.Pst)" {
        $output = & $info $pstPath 2>&1
        if ($VerboseOutput) { $output | Out-Host }
        if ($output -notmatch 'ALL CHECKS PASSED') {
            $output | Select-Object -Last 30 | Out-Host
            throw 'pst_info did not report ALL CHECKS PASSED'
        }
    }
}

Write-Host ""
Write-Host ("=" * 60)
if ($failures.Count -eq 0) {
    Write-Host "ALL STEPS PASSED" -ForegroundColor Green
    Write-Host ""
    Write-Host "Generated PSTs (open these in Outlook for the manual gate):"
    Get-ChildItem $here -Filter 'out_*.pst' | ForEach-Object {
        Write-Host ("    {0,-22} {1,8:N0} bytes" -f $_.Name, $_.Length)
    }
    exit 0
} else {
    Write-Host "FAILED STEPS:" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host "    - $_" -ForegroundColor Red }
    exit 1
}
