#!/usr/bin/env bash
set -Eeuo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd -- "$script_dir/.." && pwd -P)"
if [[ "${SEYRIX_QEMU_LOCK_HELD:-0}" != 1 ]]; then
  exec bash "$script_dir/qemu-lock.sh" -- "$script_dir/qemu-graphical-readiness-campaign.sh" "$@"
fi
if (( $# != 1 )); then
  echo "Usage: $0 /path/to/seyrix-phase2d3b-candidate.iso" >&2
  exit 2
fi

source "$repo_root/build/retention.sh"
seyrix_retention_init
iso_path="$(realpath -- "$1")"
if [[ ! -f "$iso_path" ]]; then
  echo "ISO does not exist: $iso_path" >&2
  exit 2
fi

run_stamp="$(date -u +%Y%m%dT%H%M%SZ)"
campaign_root="$SEYRIX_ARTIFACT_ROOT/evidence/phase2d3b5-readiness-$run_stamp"
if [[ -e "$campaign_root" ]]; then
  echo "Campaign evidence already exists: $campaign_root" >&2
  exit 2
fi
mkdir -p "$campaign_root"
summary="$campaign_root/campaign.tsv"
printf 'boot\treadiness_result\tqemu_exit\tgraphical_boot_ms\tstarted_utc\tfinished_utc\tqemu_run_dir\tfull_acceptance_result\n' > "$summary"
{
  printf 'Campaign: Phase 2D.3B graphical readiness\n'
  printf 'Run count: 10 independent QEMU cold boots\n'
  printf 'Resolution: 640x480\n'
  printf 'ISO: %s\n' "$iso_path"
  printf 'ISO SHA-256: %s\n' "$(sha256sum "$iso_path" | awk '{print $1}')"
  printf 'Started UTC: %s\n' "$(date -u --iso-8601=seconds)"
  printf 'Scoped result: each boot requires GRAPHICAL_READY and clean guest shutdown; full Phase 2D.3B is INCOMPLETE by design.\n'
} > "$campaign_root/campaign-metadata.txt"

for boot in $(seq 1 10); do
  boot_id="$(printf 'boot-%02d' "$boot")"
  qemu_run_dir="$SEYRIX_ARTIFACT_ROOT/qemu-graphics-readiness-$run_stamp-$boot_id"
  launcher_log="$campaign_root/$boot_id-launcher.log"
  if [[ -e "$qemu_run_dir" || -e "$launcher_log" ]]; then
    echo "Run output already exists for $boot_id; refusing to overwrite." >&2
    exit 2
  fi
  started="$(date -u --iso-8601=seconds)"
  printf '[%d/10] cold boot start %s\n' "$boot" "$started"
  set +e
  SEYRIX_QEMU_WORKFLOW=phase2d3b \
    SEYRIX_QEMU_RESOLUTION_PROFILE=640x480 \
    SEYRIX_QEMU_READINESS_ONLY=1 \
    SEYRIX_QEMU_GRAPHICS_DIR="$qemu_run_dir" \
    SEYRIX_RETENTION_KEEP_ARTIFACTS=1 \
    SEYRIX_RETENTION_KEEP_EVIDENCE=1 \
    "$script_dir/qemu-graphics.sh" "$iso_path" > "$launcher_log" 2>&1
  qemu_exit=$?
  set -e
  finished="$(date -u --iso-8601=seconds)"
  test_log="$qemu_run_dir/test.log"
  graphical_ms="not-reached"
  full_result="missing"
  if [[ -f "$test_log" ]]; then
    graphical_ms="$(sed -n 's/^GRAPHICAL_BOOT_MS=//p' "$test_log" | tail -n 1)"
  fi
  if [[ -f "$qemu_run_dir/run-result.txt" ]]; then
    full_result="$(sed -n 's/^Run result: //p' "$qemu_run_dir/run-result.txt" | head -n 1)"
  fi
  readiness_result="INCOMPLETE"
  if [[ "$qemu_exit" -eq 0 && -f "$test_log" ]] &&
      grep -Fxq 'GRAPHICAL_READY=passed' "$test_log" &&
      grep -Fxq 'CLEAN_SHUTDOWN=passed' "$test_log" &&
      grep -Fq 'STAGE_RESULT name={Seyrix compositor and shell graphical readiness} result=PASS' "$test_log" &&
      grep -Fq 'STAGE_RESULT name={clean guest shutdown after graphical readiness} result=PASS' "$test_log" &&
      grep -Fq 'STAGE_RESULT name={full Phase 2D.3B acceptance} result=INCOMPLETE reason=readiness-only' "$test_log" &&
      awk '/^STAGE_BEGIN / {begun++} /^STAGE_RESULT .*result=PASS/ {passed++} /^STAGE_RESULT .*result=FAIL/ {bad++} /^STAGE_RESULT .*result=INCOMPLETE/ && $0 !~ /full Phase 2D.3B acceptance.*reason=readiness-only/ {bad++} END {exit !(begun > 0 && begun == passed && bad == 0)}' "$qemu_run_dir/stage-results.log" &&
      [[ "$full_result" == INCOMPLETE ]]; then
    readiness_result="PASS"
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$boot_id" "$readiness_result" "$qemu_exit" "$graphical_ms" \
    "$started" "$finished" "$qemu_run_dir" "$full_result" >> "$summary"
  printf '[%d/10] graphical readiness %s; full acceptance %s; evidence %s\n' \
    "$boot" "$readiness_result" "$full_result" "$qemu_run_dir"
  if [[ "$readiness_result" != PASS ]]; then
    printf 'COLD_BOOT_READINESS_CAMPAIGN=INCOMPLETE completed=%d expected=10\n' "$boot" >&2
    printf 'Campaign summary: %s\n' "$summary" >&2
    exit 1
  fi
done

printf 'COLD_BOOT_READINESS_CAMPAIGN=PASS count=10\n'
printf 'Campaign summary: %s\n' "$summary"
