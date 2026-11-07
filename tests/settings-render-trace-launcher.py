#!/usr/bin/env python3
"""Request a normal Settings open from the production session supervisor."""
import subprocess
import sys

result = subprocess.run(
    ["/usr/bin/seyrix-session-supervisor", "--open"],
    check=False,
)
sys.exit(result.returncode)
