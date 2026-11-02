#!/usr/bin/env python3
"""Show that renaming a Unix listener path does not disconnect accepted clients."""

from __future__ import annotations

import os
from pathlib import Path
import socket
import tempfile


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="seyrix-socket-path-") as directory:
        path = Path(directory) / "settings-v1.sock"
        moved_path = Path(directory) / "settings-v1.sock.qemu-offline"
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        listener.bind(str(path))
        listener.listen(2)
        client = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        client.settimeout(1.0)
        listener.settimeout(0.05)
        try:
            client.connect(str(path))
            accepted, _ = listener.accept()
            accepted.settimeout(1.0)
            os.replace(path, moved_path)

            client.sendall(b"existing-stream-survives-path-move")
            if accepted.recv(128) != b"existing-stream-survives-path-move":
                raise AssertionError("the existing client stream did not survive the path move")

            replacement = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            try:
                try:
                    replacement.connect(str(path))
                except FileNotFoundError:
                    pass
                else:
                    raise AssertionError("a new client unexpectedly connected through the missing path")
            finally:
                replacement.close()

            os.replace(moved_path, path)
            client.sendall(b"restoring-the-path-does-not-reconnect")
            if accepted.recv(128) != b"restoring-the-path-does-not-reconnect":
                raise AssertionError("the accepted stream did not survive path restoration")
            try:
                listener.accept()
            except TimeoutError:
                pass
            else:
                raise AssertionError("path restoration created an unobserved new client connection")
        finally:
            client.close()
            listener.close()
            if "accepted" in locals():
                accepted.close()

    print("UNIX_SOCKET_PATH_RENAME_PRESERVES_CONNECTED_STREAM=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
