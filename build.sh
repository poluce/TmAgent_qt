#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PS_SCRIPT="${SCRIPT_DIR}/build.ps1"

if ! command -v wslpath >/dev/null 2>&1; then
  echo "wslpath not found. Run this script inside WSL." >&2
  exit 1
fi

if ! command -v powershell.exe >/dev/null 2>&1; then
  echo "powershell.exe not found. Windows interop must be enabled in WSL." >&2
  exit 1
fi

WIN_PS_SCRIPT="$(wslpath -w "${PS_SCRIPT}")"

exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "${WIN_PS_SCRIPT}" "$@"
