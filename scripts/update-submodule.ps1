# 更新 QChatWidget 子模块到远端当前分支最新提交
# 在项目根目录执行: .\scripts\update-submodule.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
Set-Location $root

Write-Host "更新子模块 QChatWidget ..." -ForegroundColor Cyan
git submodule update --init --recursive --remote
if ($LASTEXITCODE -ne 0) {
    Write-Host "子模块更新失败，请检查网络与 git 配置。" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "子模块已更新。若 QChatWidget API 有变更，请参考 docs/子模块更新与适配.md 做适配。" -ForegroundColor Green
