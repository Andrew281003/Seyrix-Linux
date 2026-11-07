#!/usr/bin/env bash
set -Eeuo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "$repo_root/build/settings-desktop-entry.sh"
fixture_root="$(mktemp -d)"
trap 'rm -rf -- "$fixture_root"' EXIT

source_entry="$repo_root/desktop/settings/data/seyrix-settings.desktop"
crlf_entry="$fixture_root/seyrix-settings-crlf.desktop"
crlf_target="$fixture_root/crlf/usr/share/applications/seyrix-settings.desktop"
lf_target="$fixture_root/lf/usr/share/applications/seyrix-settings.desktop"

tr -d '\r' < "$source_entry" | sed 's/$/\r/' > "$crlf_entry"
seyrix_install_settings_desktop_entry "$crlf_entry" "$crlf_target"
seyrix_install_settings_desktop_entry "$source_entry" "$lf_target"

for entry in "$crlf_target" "$lf_target"; do
	if LC_ALL=C grep -q $'\r' "$entry"; then
		printf 'FAIL: carriage return remains in %s\n' "$entry" >&2
		exit 1
	fi
	for field in Exec TryExec; do
		if [[ "$(grep -Fxc "${field}=seyrix-settings-launch" "$entry")" != 1 ]]; then
			printf 'FAIL: supervised %s missing or duplicated in %s\n' "$field" "$entry" >&2
			exit 1
		fi
	done
	if [[ "$(stat -c '%a' "$entry")" != 644 ]]; then
		printf 'FAIL: unexpected mode for %s\n' "$entry" >&2
		exit 1
	fi
done

printf 'SETTINGS_DESKTOP_ENTRY_LINE_ENDINGS=PASS cases=CRLF,LF\n'
