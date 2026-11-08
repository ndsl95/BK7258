# 烧录固件到 WB5E（PowerShell 7）
# 用法: .\flash.ps1 [-Bin <路径>] [-Port COM11]
param(
    [string]$Bin  = 'H:\BK7258\firmware\all-app_wb5e_lvgl.bin',
    [string]$Port = 'COM11'
)
$loader = 'H:\BK7258\tools\BKFIL\windows\bk_loader\bk_loader.dist\bk_loader.exe'
if (-not (Test-Path $Bin))  { throw "找不到固件: $Bin" }
if (-not (Test-Path $loader)) { throw "找不到 bk_loader: $loader" }
Write-Host "烧录 $Bin -> $Port" -ForegroundColor Cyan
& $loader download -p $Port -b 921600 -i $Bin -s 0x0 -e 0 -r
if ($LASTEXITCODE -ne 0) { throw "烧录失败，exit=$LASTEXITCODE" }
Write-Host "烧录完成" -ForegroundColor Green