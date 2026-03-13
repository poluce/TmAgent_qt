# 更新仓库中仍保留的子模块（当前主要是 qtkeychain）
# 在项目根目录执行: .\scripts\update-submodule.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
Set-Location $root

Write-Host "QChatWidget 已改为随主仓库直接提交，不再通过子模块更新。" -ForegroundColor Yellow
Write-Host "更新剩余子模块（如 qtkeychain）..." -ForegroundColor Cyan
git submodule update --init --recursive
if ($LASTEXITCODE -ne 0) {
    Write-Host "子模块更新失败，请检查网络与 git 配置。" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "剩余子模块已更新。QChatWidget 如需同步上游，请按 docs/10_方案/12-子模块更新与适配.md 中的方式显式合并。" -ForegroundColor Green
