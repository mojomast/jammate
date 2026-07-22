# =============================================================================
# PedalForge NAM — instalador dos plugins VST3 recomendados
#
# Uso:  clique-direito > "Executar com o PowerShell"  (pede admin sozinho)
#       ou:  powershell -ExecutionPolicy Bypass -File instalar-plugins.ps1
#
# Ordem de fontes: 1) pasta offline\ ao lado deste script (vem no repo)
#                  2) release oficial no GitHub de cada projeto
# Destino: C:\Program Files\Common Files\VST3
# Depois de instalar, os plugins aparecem no menu CARREGAR VST3 do app.
# =============================================================================
param([switch]$SomenteListar)

$ErrorActionPreference = "Stop"

$plugins = @(
    @{ Nome = "Dragonfly Reverb 3.2.10 (4 reverbs)"; Licenca = "GPLv3"
       Offline = "dragonfly-reverb-3.2.10-vst3-win64.zip"
       Url = "https://github.com/michaelwillis/dragonfly-reverb/releases/download/3.2.10/dragonfly-reverb-3.2.10-win64.zip" },
    @{ Nome = "Airwindows Consolidated (~400 efeitos)"; Licenca = "MIT"
       Offline = "airwindows-consolidated-2026.07.19-vst3-win64.zip"
       Url = "https://github.com/baconpaul/airwin2rack/releases/download/DAWPlugin/AirwindowsConsolidated-2026-07-19-e4c4ca2-Windows.zip" },
    @{ Nome = "Zam Plugins 4.5 (ZamTube, ZamComp, ZamEQ...)"; Licenca = "GPLv2+"
       Offline = "zam-plugins-4.5-vst3-win64.zip"
       Url = "https://github.com/zamaudio/zam-plugins/releases/download/4.5/zam-plugins-4.5-win64.zip" }
)

if ($SomenteListar) {
    $plugins | ForEach-Object { "{0}  [{1}]" -f $_.Nome, $_.Licenca }
    "LSP Plugins  [LGPLv3]  (instalacao manual: https://lsp-plug.in)"
    return
}

# ---- eleva para admin (o destino e Program Files) ---------------------------
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "Elevando para administrador..." -ForegroundColor Yellow
    Start-Process powershell -Verb RunAs -ArgumentList "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`""
    return
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$destino = "C:\Program Files\Common Files\VST3"
New-Item -ItemType Directory -Force $destino | Out-Null
$offlineDir = Join-Path $PSScriptRoot "offline"
$tempDir = Join-Path $env:TEMP "guitarrig-plugins"
New-Item -ItemType Directory -Force $tempDir | Out-Null

# Extrai só os bundles *.vst3 de um zip (funciona com o formato offline —
# bundles na raiz — e com os zips oficiais, que tem pasta de prefixo).
function Instalar-Vst3DoZip($zipPath) {
    $instalados = @()
    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        foreach ($e in $zip.Entries) {
            if ($e.Name -eq "" -or $e.FullName -notmatch "(?i)\.vst3") { continue }
            $m = [regex]::Match($e.FullName, "(?i)([^/]+\.vst3(?:/.*)?)$")
            if (-not $m.Success) { continue }
            $rel = $m.Groups[1].Value -replace "/", "\"
            $alvo = Join-Path $destino $rel
            New-Item -ItemType Directory -Force (Split-Path $alvo) | Out-Null
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $alvo, $true)
            $bundle = ($rel -split "\\")[0]
            if ($instalados -notcontains $bundle) { $instalados += $bundle }
        }
    } finally { $zip.Dispose() }
    return $instalados
}

$resumo = @()
foreach ($p in $plugins) {
    Write-Host ""
    Write-Host ">> $($p.Nome)" -ForegroundColor Cyan

    $zipLocal = Join-Path $offlineDir $p.Offline
    if (Test-Path $zipLocal) {
        Write-Host "   usando copia offline do repo"
    } else {
        Write-Host "   baixando do release oficial..."
        $zipLocal = Join-Path $tempDir ($p.Offline)
        try {
            Invoke-WebRequest -Uri $p.Url -OutFile $zipLocal -UseBasicParsing
        } catch {
            Write-Host "   FALHOU: $($_.Exception.Message)" -ForegroundColor Red
            $resumo += "FALHOU  $($p.Nome)"
            continue
        }
    }

    try {
        $bundles = Instalar-Vst3DoZip $zipLocal
        foreach ($b in $bundles) { Write-Host "   instalado: $b" -ForegroundColor Green }
        $resumo += "OK      $($p.Nome)  ($($bundles.Count) plugin(s))"
    } catch {
        Write-Host "   ERRO ao extrair: $($_.Exception.Message)" -ForegroundColor Red
        $resumo += "FALHOU  $($p.Nome)"
    }
}

Write-Host ""
Write-Host "================ RESUMO ================" -ForegroundColor Yellow
$resumo | ForEach-Object { Write-Host $_ }
Write-Host ""
Write-Host "LSP Plugins nao tem binario Windows no GitHub - baixe em https://lsp-plug.in" -ForegroundColor Yellow
Write-Host "Pronto! Os plugins aparecem no menu CARREGAR VST3 do PedalForge NAM." -ForegroundColor Green
Write-Host ""
Read-Host "Enter para fechar"
