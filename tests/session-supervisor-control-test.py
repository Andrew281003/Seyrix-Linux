#!/usr/bin/env python3
"""Exercise the production supervisor control socket without a compositor."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time


def run_command(binary: str, command: str, env: dict[str, str],
        timeout: float = 3.0) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run([binary, command], env=env, timeout=timeout,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wait_for_ready(binary: str, env: dict[str, str],
        manager: subprocess.Popen[bytes]) -> None:
    for _ in range(100):
        if run_command(binary, "--ping", env).returncode == 0:
            return
        if manager.poll() is not None:
            raise AssertionError("manager exited before its control socket was ready")
        time.sleep(0.02)
    raise AssertionError("manager control socket did not become ready")


def manager_fd_count(pid: int) -> int:
    return len(os.listdir(f"/proc/{pid}/fd"))


def wait_for_fd_count(pid: int, expected: int) -> None:
    deadline = time.monotonic() + 1.0
    while time.monotonic() < deadline:
        if manager_fd_count(pid) == expected:
            return
        time.sleep(0.005)
    raise AssertionError("supervisor control request left an open descriptor")


def test_manager_control(binary: str) -> None:
    runtime = tempfile.mkdtemp(prefix="seyrix-supervisor-control.", dir="/tmp")
    os.chmod(runtime, 0o700)
    env = os.environ.copy()
    env["XDG_RUNTIME_DIR"] = runtime
    socket_path = os.path.join(runtime, "seyrix-settings-supervisor.sock")
    log_path = os.path.join(runtime, "manager.log")
    manager = None
    fake_peer = None
    wayland_listener = None
    wayland_peer = None
    try:
        with open(log_path, "wb") as log:
            manager = subprocess.Popen([binary, "--serve"], env=env,
                stdout=log, stderr=subprocess.STDOUT)
        wait_for_ready(binary, env, manager)

        duplicate = run_command(binary, "--serve", env)
        if duplicate.returncode != 0:
            raise AssertionError("second manager did not exit cleanly")

        baseline_fd_count = manager_fd_count(manager.pid)
        for _ in range(32):
            if run_command(binary, "--ping", env).returncode != 0:
                raise AssertionError("valid PING failed during descriptor check")
            wait_for_fd_count(manager.pid, baseline_fd_count)

        malformed = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        malformed.connect(socket_path)
        malformed.sendall(b"OPEN\0suffix")
        reply = malformed.recv(16)
        malformed.close()
        if reply != b"NO":
            raise AssertionError(f"malformed OPEN returned {reply!r}")
        wait_for_fd_count(manager.pid, baseline_fd_count)
        if b"event=open-request" in Path(log_path).read_bytes():
            raise AssertionError("malformed OPEN changed manager state")

        wayland_path = os.path.join(runtime, "wayland-0")
        wayland_listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        wayland_listener.bind(wayland_path)
        wayland_listener.listen(1)
        if run_command(binary, "--open", env).returncode != 0:
            raise AssertionError("OPEN request failed")
        wayland_listener.settimeout(3.0)
        wayland_peer, _ = wayland_listener.accept()
        wayland_peer.settimeout(1.0)
        if not wayland_peer.recv(4096):
            raise AssertionError("Wayland probe sent no sync request")
        started = time.monotonic()
        if run_command(binary, "--stop", env).returncode != 0:
            raise AssertionError("STOP failed during Wayland readiness probe")
        if manager.wait(timeout=3.0) != 0:
            raise AssertionError("manager did not exit cleanly after STOP")
        manager = None
        if time.monotonic() - started > 2.0:
            raise AssertionError("STOP was not serviced promptly during Wayland probe")
        log = Path(log_path).read_bytes()
        if b"event=launch" in log or b"event=wayland-ready" in log:
            raise AssertionError("Settings launched before Wayland sync completed")
        print("manager singleton and malformed packet: PASS")
        print("STOP during Wayland readiness probe: PASS")

        if os.path.exists(socket_path):
            raise AssertionError("manager did not remove its control socket")
        ready_read, ready_write = os.pipe()
        fake_peer = os.fork()
        if fake_peer == 0:
            os.close(ready_read)
            try:
                os.setsid()
            except OSError:
                os.write(ready_write, b"E")
                os.close(ready_write)
                os._exit(1)
            server = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            server.bind(socket_path)
            os.chmod(socket_path, 0o600)
            server.listen(1)
            os.write(ready_write, b"R")
            os.close(ready_write)
            connection, _ = server.accept()
            data = connection.recv(16)
            if data:
                connection.sendall(b"READY")
            connection.close()
            server.close()
            os._exit(0)
        os.close(ready_write)
        ready = os.read(ready_read, 1)
        os.close(ready_read)
        if ready != b"R":
            raise AssertionError("cross-session fake server did not start")
        spoof = run_command(binary, "--ping", env)
        _, child_status = os.waitpid(fake_peer, 0)
        fake_peer = None
        if spoof.returncode == 0:
            raise AssertionError("--ping trusted a control server in another session")
        if not os.WIFEXITED(child_status) or os.WEXITSTATUS(child_status) != 0:
            raise AssertionError("cross-session fake server failed")
        print("cross-session control peer: REJECTED")
    finally:
        if wayland_peer is not None:
            wayland_peer.close()
        if wayland_listener is not None:
            wayland_listener.close()
        if manager is not None:
            run_command(binary, "--stop", env)
            if manager.poll() is None:
                manager.terminate()
            try:
                manager.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                manager.kill()
                manager.wait()
        if fake_peer is not None:
            try:
                os.kill(fake_peer, 9)
            except ProcessLookupError:
                pass
            os.waitpid(fake_peer, 0)
        shutil.rmtree(runtime)


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 2:
        raise SystemExit("expected supervisor binary path")
    test_manager_control(sys.argv[1])
