#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
vpk="${1:-"$repo_root/releases/SMartBird.vpk"}"
vita3k_root="${VITA3K_ROOT:-"$HOME/Library/Application Support/Vita3K/Vita3K"}"
title_id="${VITA3K_TITLE_ID:-SMARTBIRD}"
app_dir="$vita3k_root/fs/ux0/app/$title_id"

if [[ ! -f "$vpk" ]]; then
    echo "VPK not found: $vpk" >&2
    exit 1
fi

if [[ ! -d "$app_dir" ]]; then
    echo "Vita3K app directory not found: $app_dir" >&2
    echo "Install the VPK once in Vita3K first, then run this script for updates." >&2
    exit 1
fi

echo "Updating $title_id in Vita3K"
echo "VPK: $vpk"
echo "Target: $app_dir"
unzip -oq "$vpk" -d "$app_dir"
echo "Done"
