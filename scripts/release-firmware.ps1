# Her iki projeyi (esp32_master, esp8266_slave) PlatformIO ile derler, firmware/
# klasorune GITHUB_FIRMWARE_URL'in isaret ettigi SABIT isimli dosyayi (esp32.bin /
# esp8266.bin - OTA butonu hep bunu ceker) gunceller, AYRICA tarih-damgali bir
# surum kopyasi birakir ve o cihaz icin en yeni 5 surum disindakileri siler.
#
# Neden gerekli: OTA "GitHub'dan Guncelle" butonu kaynak koddan degil, repoya
# elle kopyalanmis .bin dosyasindan besleniyor. Sadece "git push" yapip bu
# script calistirilmazsa GitHub'daki .bin eski kalir ve cihaz eski firmware
# yukler (2026-08-21'de yasanan sorunun kok nedeni buydu).
#
# Kullanim: repo kokunden  ->  pwsh scripts/release-firmware.ps1

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

$targets = @(
    @{ Name = "esp32"; Dir = Join-Path $repoRoot "esp32_master"; Env = "esp32s3" },
    @{ Name = "esp8266"; Dir = Join-Path $repoRoot "esp8266_slave"; Env = "esp8266-12e" }
)

foreach ($t in $targets) {
    Write-Host "=== $($t.Name) deriliyor ($($t.Env)) ===" -ForegroundColor Cyan
    Push-Location $t.Dir
    try {
        pio run -e $t.Env
        if ($LASTEXITCODE -ne 0) { throw "$($t.Name) derlemesi basarisiz (exit $LASTEXITCODE)" }

        $builtBin = Join-Path $t.Dir ".pio\build\$($t.Env)\firmware.bin"
        if (-not (Test-Path $builtBin)) { throw "$builtBin bulunamadi" }

        $fwDir = Join-Path $t.Dir "firmware"
        New-Item -ItemType Directory -Force -Path $fwDir | Out-Null

        $stablePath = Join-Path $fwDir "$($t.Name).bin"
        Copy-Item $builtBin $stablePath -Force
        Write-Host "  -> $stablePath guncellendi (OTA butonu bunu ceker)"

        $version = Get-Date -Format "yyyyMMdd-HHmm"
        $versionedPath = Join-Path $fwDir "$($t.Name)_$version.bin"
        Copy-Item $builtBin $versionedPath -Force
        Write-Host "  -> $versionedPath (surum kopyasi)"

        $eski = Get-ChildItem -Path $fwDir -Filter "$($t.Name)_*.bin" | Sort-Object LastWriteTime -Descending | Select-Object -Skip 5
        foreach ($f in $eski) {
            Remove-Item $f.FullName -Force
            Write-Host "  -> silindi (5 surum siniri asildi): $($f.Name)"
        }
    } finally {
        Pop-Location
    }
}

Write-Host "`nTamamlandi. Simdi 'git add esp32_master/firmware esp8266_slave/firmware' + commit + push gerekiyor." -ForegroundColor Green
