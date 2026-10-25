session_tty="$(tty 2>/dev/null || true)"
printf 'tty=%s\nXDG_VTNR=%s\n' "$session_tty" "${XDG_VTNR:-unset}" > /home/seyrix/.tty1-session-info
if [[ "$session_tty" == /dev/tty1 ]] || [[ "${XDG_VTNR:-}" == 1 ]]; then
  touch /home/seyrix/.tty1-ready
fi
printf '\nSeyrix development console. Start Wayland with: start-wayland\n'
printf 'Exit the graphical session by typing exit in Weston Terminal.\n\n'
