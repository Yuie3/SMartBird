#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
title_id="${VITA3K_TITLE_ID:-SMARTBIRD}"
vita3k_app="${VITA3K_APP:-/Applications/Vita3K.app}"
vita3k_root="${VITA3K_ROOT:-"$HOME/Library/Application Support/Vita3K/Vita3K"}"
log_level="${VITA3K_LOG_LEVEL:-6}"

"$repo_root/scripts/update-vita3k-app.sh" "${1:-}"

config_yml="$vita3k_root/config.yml"
settings_ini="$vita3k_root/gui-configs/CurrentSettings.ini"
if [[ -f "$config_yml" ]]; then
    /usr/bin/perl -0pi -e "s/^log-level: .*/log-level: $log_level/m" "$config_yml"
fi
if [[ -f "$settings_ini" ]]; then
    /usr/bin/perl -0pi -e 's/loggerVisible=true/loggerVisible=false/g' "$settings_ini"
fi

if [[ -d "$vita3k_app" ]]; then
    open -na "$vita3k_app" --args --installed-path "$title_id"
else
    echo "Vita3K app not found: $vita3k_app" >&2
    exit 1
fi
