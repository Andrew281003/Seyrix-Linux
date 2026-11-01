#!/usr/bin/env bash
# Shared production parsing and launch gates for the Settings diagnostic trace.

settings_trace_initial_appearance_pid() {
  local trace_file=$1
  [[ -r "$trace_file" ]] || return 1
  sed -n -E \
    's/.*SEYRIX_SETTINGS_TRACE .*pid=([0-9]+) event=gtk_page_map page=appearance selected=appearance([[:space:]].*)?$/\1/p' \
    "$trace_file" | head -n 1
}

settings_trace_gtk_lines_for_pid() {
  local trace_file=$1 pid=$2
  [[ "$pid" =~ ^[0-9]+$ && -r "$trace_file" ]] || return 1
  grep -F 'SEYRIX_SETTINGS_TRACE ' "$trace_file" |
    grep -F "pid=$pid event=gtk_" || true
}

settings_trace_client_surface_ops() {
  local pid=$1
  [[ "$pid" =~ ^[0-9]+$ ]] || return 0
  grep -F "source=client-stderr pid=$pid " |
    grep -E 'wl_surface[@#][0-9]+\.(attach|damage|damage_buffer|commit)\([^)]*\)[[:space:]]*$' || true
}

settings_trace_client_surface_ops_from_file() {
  local trace_file=$1 pid=$2
  [[ -r "$trace_file" ]] || return 0
  settings_trace_client_surface_ops "$pid" < "$trace_file"
}

settings_trace_client_surface_ids_from_stream() {
  local pid=$1
  settings_trace_client_surface_ops "$pid" |
    sed -n -E 's/.*wl_surface[@#]([0-9]+)\.(attach|damage|damage_buffer|commit)\([^)]*\)[[:space:]]*$/\1/p' |
    sort -un | paste -sd ' ' -
}

settings_trace_client_surface_ids_from_file() {
  local trace_file=$1 pid=$2
  [[ -r "$trace_file" ]] || return 0
  settings_trace_client_surface_ids_from_stream "$pid" < "$trace_file"
}

settings_trace_client_surface_event_count() {
  local trace_file=$1 pid=$2
  settings_trace_client_surface_ops_from_file "$trace_file" "$pid" |
    awk 'END { print NR + 0 }'
}

# Wayland object IDs are connection-local. Only join a client wl_surface ID to
# the compositor's wl_surface resource field for the same Settings PID; never
# infer that identity from a wl_pointer or another compositor pointer ID.
settings_trace_correlate_surface_id() {
  local pid=$1 client_ids=$2 server_surface_lines=$3 candidate
  [[ "$pid" =~ ^[0-9]+$ ]] || return 1
  for candidate in $client_ids; do
    if printf '%s\n' "$server_surface_lines" |
        grep -F 'SETTINGS_RENDER_TRACE event=surface_commit ' |
        grep -F "pid=$pid " |
        grep -F "wl_surface=$candidate " >/dev/null; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

settings_trace_same_session_gate() {
  local settings_sid=$1 compositor_sid=$2
  [[ "$settings_sid" =~ ^[0-9]+$ && "$compositor_sid" =~ ^[0-9]+$ &&
    "$settings_sid" == "$compositor_sid" ]]
}

settings_trace_endpoint_connection_gate() {
  local ss_output=$1 pid=$2
  [[ "$pid" =~ ^[0-9]+$ ]] || return 1
  printf '%s\n' "$ss_output" |
    grep -E '^[[:space:]]*u_seq[[:space:]]+ESTAB[[:space:]]' |
    grep -F "pid=$pid," >/dev/null
}

settings_trace_page_selection_timestamp_ns() {
  local trace_file=$1 pid=$2
  [[ "$pid" =~ ^[0-9]+$ && -r "$trace_file" ]] || return 1
  grep -F 'SEYRIX_SETTINGS_TRACE ' "$trace_file" |
    grep -F "pid=$pid event=gtk_page_selection page=desktop selected=desktop" |
    tail -n 1 |
    sed -n -E 's/.*SEYRIX_SETTINGS_TRACE monotonic_ns=([0-9]+) pid=.*/\1/p'
}

_settings_trace_first_event_ns_after() {
  local records=$1 event=$2 after_ns=$3 pid=$4 surface_id=$5 surface_seq=$6 line ns
  while IFS= read -r line; do
    [[ "$line" == *"SETTINGS_RENDER_TRACE event=$event "* &&
      "$line" == *"pid=$pid "* && "$line" == *"wl_surface=$surface_id "* ]] || continue
    if [[ "$event" == surface_commit ]]; then
      [[ "$line" == *"seq=$surface_seq "* && "$line" == *"buffer_attached=1 "* ]] || continue
      [[ "$line" == *"surface_damage=1:"* || "$line" == *"buffer_damage=1:"* ]] || continue
    else
      [[ "$line" =~ (^|[[:space:]])surface_seq=${surface_seq}([[:space:]]|$) ]] || continue
      if [[ "$event" == output_scene_commit ]]; then
        [[ "$line" == *"result=success "* ]] || continue
      elif [[ "$event" == output_present ]]; then
        [[ "$line" == *"presented=1 "* ]] || continue
      fi
    fi
    ns="$(printf '%s\n' "$line" | sed -n -E 's/^.*monotonic_ns=([0-9]+) .*/\1/p')"
    [[ "$ns" =~ ^[0-9]+$ ]] || continue
    if (( 10#$ns > 10#$after_ns )); then
      printf '%s\n' "$ns"
      return 0
    fi
  done <<< "$records"
  return 1
}

# Correlate client requests and compositor output records using the guest
# monotonic clock, Settings PID, and wl_surface resource ID. The compositor's
# pointer_id namespace is intentionally never consulted.
settings_trace_render_sequence_gate() {
  local client_lines=$1 surface_lines=$2 scene_lines=$3 present_lines=$4
  local pid=$5 surface_id=$6 surface_seq=$7 selection_ns=$8
  local line ns attach_ns= damage_ns= commit_ns= surface_ns scene_ns present_ns
  [[ "$pid" =~ ^[0-9]+$ && "$surface_id" =~ ^[0-9]+$ &&
    "$surface_seq" =~ ^[0-9]+$ && "$selection_ns" =~ ^[0-9]+$ ]] || return 1

  while IFS= read -r line; do
    [[ "$line" == *"source=client-stderr pid=$pid "* &&
      "$line" == *"wl_surface#$surface_id."* ||
      "$line" == *"source=client-stderr pid=$pid "* &&
      "$line" == *"wl_surface@$surface_id."* ]] || continue
    ns="$(printf '%s\n' "$line" | sed -n -E 's/^monotonic_ns=([0-9]+) .*/\1/p')"
    [[ "$ns" =~ ^[0-9]+$ ]] || continue
    if [[ -z "$attach_ns" && "$line" == *".attach("* ]] &&
        (( 10#$ns > 10#$selection_ns )); then
      attach_ns=$ns
    elif [[ -n "$attach_ns" && -z "$damage_ns" &&
        ( "$line" == *".damage("* || "$line" == *".damage_buffer("* ) ]] &&
        (( 10#$ns > 10#$attach_ns )); then
      damage_ns=$ns
    elif [[ -n "$damage_ns" && -z "$commit_ns" && "$line" == *".commit("* ]] &&
        (( 10#$ns > 10#$damage_ns )); then
      commit_ns=$ns
      break
    fi
  done <<< "$client_lines"
  [[ -n "$attach_ns" && -n "$damage_ns" && -n "$commit_ns" ]] || return 1

  surface_ns="$(_settings_trace_first_event_ns_after \
    "$surface_lines" surface_commit "$commit_ns" "$pid" "$surface_id" "$surface_seq")" || return 1
  scene_ns="$(_settings_trace_first_event_ns_after \
    "$scene_lines" output_scene_commit "$surface_ns" "$pid" "$surface_id" "$surface_seq")" || return 1
  present_ns="$(_settings_trace_first_event_ns_after \
    "$present_lines" output_present "$scene_ns" "$pid" "$surface_id" "$surface_seq")" || return 1

  printf 'SETTINGS_TRACE_TIME_CORRELATION=passed\n'
  printf 'SETTINGS_TRACE_TIMESTAMPS pid=%s wl_surface_id=%s surface_seq=%s gtk_selection_ns=%s client_attach_ns=%s client_damage_ns=%s client_commit_ns=%s compositor_surface_commit_ns=%s scene_commit_ns=%s output_present_ns=%s\n' \
    "$pid" "$surface_id" "$surface_seq" "$selection_ns" "$attach_ns" \
    "$damage_ns" "$commit_ns" "$surface_ns" "$scene_ns" "$present_ns"
}

# This launch gate deliberately does not require a Wayland protocol text line:
# GTK page state and the traced process environment are sufficient to proceed
# with real input. Protocol events are parsed and reported independently.
settings_trace_launch_gate() {
  local trace_pid=$1 checked_pid=$2 uid=$3 native_exe=$4 trace_owner=$5
  local page_before=$6 trace_readable=$7 launcher_executable=$8
  local binary_executable=${9} trace_environment=${10} wayland_environment=${11}
  local runtime_environment=${12} display_environment=${13} wayland_socket=${14}
  [[ "$trace_pid" =~ ^[0-9]+$ && "$trace_pid" == "$checked_pid" &&
    "$uid" == 1000 && "$native_exe" == /usr/bin/seyrix-settings &&
    "$trace_owner" == seyrix:seyrix && "$page_before" == appearance &&
    "$trace_readable" == yes && "$launcher_executable" == yes &&
    "$binary_executable" == yes && "$trace_environment" == 1 &&
    "$wayland_environment" == client &&
    "$runtime_environment" == /run/user/1000 &&
    -n "$display_environment" && "$display_environment" == "$wayland_socket" ]]
}

settings_trace_input_gate() {
  local mouse_x=$1 mouse_y=$2 left_press=$3 left_release=$4 keyboard_sequence=$5
  [[ "$mouse_x" =~ ^[0-9]+$ && "$mouse_y" =~ ^[0-9]+$ &&
    "$left_press" =~ ^[0-9]+$ && "$left_release" =~ ^[0-9]+$ &&
    "$keyboard_sequence" == passed ]] || return 1
  (( 10#$mouse_x > 0 && 10#$mouse_y > 0 &&
    10#$left_press > 0 && 10#$left_release > 0 ))
}

settings_trace_gtk_transition_gate() {
  local trace_file=$1 pid=$2
  [[ "$pid" =~ ^[0-9]+$ ]] || return 1
  local gtk_events
  gtk_events=$(settings_trace_gtk_lines_for_pid "$trace_file" "$pid") || return 1
  local appearance_line selection_line desktop_map_line appearance_unmap_line
  local appearance_ns selection_ns desktop_map_ns appearance_unmap_ns
  appearance_line=$(printf '%s\n' "$gtk_events" |
    grep -E 'event=gtk_page_map page=appearance selected=appearance([[:space:]]|$)' | head -n 1) || return 1
  selection_line=$(printf '%s\n' "$gtk_events" |
    grep -E 'event=gtk_page_selection page=desktop selected=desktop([[:space:]]|$)' | head -n 1) || return 1
  desktop_map_line=$(printf '%s\n' "$gtk_events" |
    grep -E 'event=gtk_page_map page=desktop selected=desktop([[:space:]]|$)' | head -n 1) || return 1
  appearance_unmap_line=$(printf '%s\n' "$gtk_events" |
    grep -E 'event=gtk_page_unmap page=appearance selected=desktop([[:space:]]|$)' | head -n 1) || return 1
  appearance_ns=$(printf '%s\n' "$appearance_line" | sed -n -E 's/.*monotonic_ns=([0-9]+).*/\1/p')
  selection_ns=$(printf '%s\n' "$selection_line" | sed -n -E 's/.*monotonic_ns=([0-9]+).*/\1/p')
  desktop_map_ns=$(printf '%s\n' "$desktop_map_line" | sed -n -E 's/.*monotonic_ns=([0-9]+).*/\1/p')
  appearance_unmap_ns=$(printf '%s\n' "$appearance_unmap_line" | sed -n -E 's/.*monotonic_ns=([0-9]+).*/\1/p')
  [[ "$appearance_ns" =~ ^[0-9]+$ && "$selection_ns" =~ ^[0-9]+$ &&
    "$desktop_map_ns" =~ ^[0-9]+$ && "$appearance_unmap_ns" =~ ^[0-9]+$ ]] || return 1
  (( 10#$appearance_ns < 10#$selection_ns &&
     10#$appearance_ns < 10#$desktop_map_ns &&
     10#$selection_ns < 10#$appearance_unmap_ns &&
     10#$desktop_map_ns < 10#$appearance_unmap_ns ))
}
