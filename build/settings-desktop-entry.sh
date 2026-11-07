#!/usr/bin/env bash

seyrix_install_settings_desktop_entry() {
	if (( $# != 2 )); then
		printf 'Usage: seyrix_install_settings_desktop_entry source target\n' >&2
		return 2
	fi
	local source="$1" target="$2" field count lines
	if [[ ! -f "$source" || -L "$source" ]]; then
		printf 'Settings desktop entry source is not a regular file: %s\n' "$source" >&2
		return 1
	fi
	install -D -m 0644 -- "$source" "$target"
	# Windows checkouts may leave CRLF in staged data files. Normalize before parsing.
	sed -i 's/\r$//' "$target"
	if LC_ALL=C grep -q $'\r' -- "$target"; then
		printf 'Settings desktop entry still contains carriage returns: %s\n' "$target" >&2
		return 1
	fi
	for field in Exec TryExec; do
		lines="$(LC_ALL=C grep -c "^${field}=" "$target" || true)"
		count="$(LC_ALL=C grep -Fxc "${field}=seyrix-settings-launch" "$target" || true)"
		if [[ "$lines" != 1 || "$count" != 1 ]]; then
			printf 'Settings desktop entry must contain exactly one %s=seyrix-settings-launch line.\n' "$field" >&2
			return 1
		fi
	done
	if command -v desktop-file-validate >/dev/null 2>&1; then
		desktop-file-validate "$target"
	fi
}
