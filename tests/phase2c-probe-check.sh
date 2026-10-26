#!/usr/bin/env bash
set -Eeuo pipefail

if (( $# != 2 )); then
  echo "Usage: $0 state|rapid|decoration|destroy|popup /path/to/probe.log" >&2
  exit 2
fi

mode="$1"
log_path="$2"
if [[ ! -r "$log_path" ]]; then
  echo "Probe log is missing or unreadable: $log_path" >&2
  exit 1
fi

require() {
  if ! grep -Fq -- "$1" "$log_path"; then
    echo "Missing probe evidence: $1" >&2
    exit 1
  fi
}

case "$mode" in
  state)
    require 'CONFIGURE_TOPLEVEL'
    require 'ACK_CONFIGURE surface=toplevel'
    require 'COMMIT surface=toplevel'
    require 'REQUEST_MAXIMIZE=1'
    require 'REQUEST_MAXIMIZE=0'
    require 'REQUEST_FULLSCREEN=1'
    require 'REQUEST_FULLSCREEN=0'
    require 'RESTORE_GEOMETRY=passed'
    require 'RESTORE_AFTER_FULLSCREEN=passed'
    require 'DECORATION_ASSERTION=passed mode=server_side'
    require 'STATE_CYCLE=passed'
    ;;
  rapid)
    require 'RAPID_STATE_REQUESTS=begin'
    require 'RAPID_STATE_CYCLE=passed'
    ;;
  decoration)
    require 'DECORATION_REQUEST=client_side'
    require 'DECORATION_MODE=client_side'
    require 'DECORATION_ASSERTION=passed mode=client_side'
    ;;
  destroy)
    require 'DESTROY_DURING_TRANSITION=sent'
    require 'PROBE_EXIT result=passed'
    ;;
  popup)
    require 'POPUP_CREATE kind=popup'
    require 'POPUP_CONFIGURE kind=popup'
    require 'POPUP_CREATE kind=nested-popup'
    require 'POPUP_CONFIGURE kind=nested-popup'
    require 'POPUP_DONE'
    ;;
  *)
    echo "Unknown probe mode: $mode" >&2
    exit 2
    ;;
esac

printf 'PROBE_CHECK=%s passed\n' "$mode"
