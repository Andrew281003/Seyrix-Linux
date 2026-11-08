#!/usr/bin/env python3
import base64
import hashlib
import os
import re
import sys
from pathlib import Path


def fail(message):
    raise SystemExit(f"SETTINGS_WAYLAND_TRACE_COLLECTION=failed reason={message}")


if len(sys.argv) != 3:
    fail("usage")

capture_path = Path(sys.argv[1])
output_dir = Path(sys.argv[2])
try:
    lines = capture_path.read_bytes().splitlines()
except OSError as error:
    fail(f"capture-unreadable:{error}")

begin_re = re.compile(rb"SETTINGS_WAYLAND_TRACE_EXPORT_BEGIN pid=([1-9][0-9]*) bytes=([1-9][0-9]*) sha256=([0-9a-f]{64})")
end_re = re.compile(rb"SETTINGS_WAYLAND_TRACE_EXPORT_END pid=([1-9][0-9]*)")
record_re = re.compile(rb"SEYRIX_SETTINGS_TRACE monotonic_ns=[0-9]+ source=client-stderr pid=([1-9][0-9]*) .+\n")
exports = []
index = 0
while index < len(lines):
    begin = begin_re.fullmatch(lines[index])
    if begin is None:
        index += 1
        continue
    pid, expected_size, expected_hash = begin.groups()
    index += 1
    encoded = []
    while index < len(lines) and end_re.fullmatch(lines[index]) is None:
        encoded.append(lines[index])
        index += 1
    if index == len(lines):
        fail(f"unterminated-pid-{pid.decode()}")
    end = end_re.fullmatch(lines[index])
    if end.group(1) != pid:
        fail(f"end-pid-mismatch-{pid.decode()}")
    if not encoded or any(not line for line in encoded):
        fail(f"empty-payload-pid-{pid.decode()}")
    try:
        payload = base64.b64decode(b"".join(encoded), validate=True)
    except ValueError:
        fail(f"invalid-base64-pid-{pid.decode()}")
    if len(payload) != int(expected_size) or hashlib.sha256(payload).hexdigest().encode() != expected_hash:
        fail(f"checksum-mismatch-pid-{pid.decode()}")
    trace_lines = payload.splitlines(keepends=True)
    if not trace_lines or not payload.endswith(b"\n"):
        fail(f"truncated-pid-{pid.decode()}")
    for trace_line in trace_lines:
        match = record_re.fullmatch(trace_line)
        if match is None:
            fail(f"unattributed-line-pid-{pid.decode()}")
    target_lines = [line for line in trace_lines if b"source=client-stderr pid=" + pid + b" " in line]
    if not any(b"wl_display" in line for line in target_lines) or not any(b"wl_surface" in line for line in target_lines):
        fail(f"missing-wayland-activity-pid-{pid.decode()}")
    exports.append((pid, payload, expected_hash))
    index += 1

if not exports:
    fail("missing-export")

os.umask(0o077)
try:
    for pid, payload, digest in exports:
        destination = output_dir / f"settings-wayland-trace-pid-{pid.decode()}.log"
        descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(descriptor, "wb", closefd=True) as output:
                output.write(payload)
                output.flush()
                os.fsync(output.fileno())
        except BaseException:
            destination.unlink(missing_ok=True)
            raise
        print(f"SETTINGS_WAYLAND_TRACE_COLLECTION=passed pid={pid.decode()} bytes={len(payload)} sha256={digest.decode()}")
except OSError as error:
    fail(f"host-write-failed:{error}")
