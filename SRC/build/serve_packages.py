#!/usr/bin/env python3
"""NOS-DOS: Build system
serve_packages.py - Local HTTP server for package repository testing.

Serves the repository root on a plain HTTP port so that a QEMU DOS VM
(using user networking) can reach packages.idx and .npkg files via mTCP
HTGET — which requires HTTP, not HTTPS.

In QEMU user networking the host is reachable inside the VM at 10.0.2.2.
Configure NPKG to point at this server by editing C:\\NOS\\NPKG\\REPO.URL:

    http://10.0.2.2:8000/packages

Then in DOS:
    NNET DHCP
    NPKG UPDATE

URLs served from this machine:
    http://localhost:<port>/packages/packages.idx
    http://localhost:<port>/packages/games/DOOM.NPKG

Usage:
    python3 build/serve_packages.py           # serve on port 8000
    python3 build/serve_packages.py 9000      # serve on port 9000
    python3 build/serve_packages.py --bind 0.0.0.0 8000  # all interfaces
"""

import argparse
import http.server
import socket
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).parent.parent.resolve()


def get_local_ip() -> str:
    """Best-effort: return the LAN IP of this machine."""
    try:
        with socket.create_connection(("8.8.8.8", 80), timeout=2) as s:
            return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Serve the NOS-DOS package repository over plain HTTP."
    )
    parser.add_argument(
        "port", nargs="?", type=int, default=8000,
        help="TCP port to listen on (default: 8000)",
    )
    parser.add_argument(
        "--bind", default="127.0.0.1",
        help="Address to bind (default: 127.0.0.1; use 0.0.0.0 for LAN access)",
    )
    args = parser.parse_args()

    local_ip  = get_local_ip()
    qemu_note = f"http://10.0.2.2:{args.port}/packages"

    print(f"NOS-DOS package repository server")
    print(f"  Serving: {ROOT_DIR}")
    print(f"  URL:     http://{args.bind}:{args.port}/packages/")
    print()
    print(f"  In the DOS VM (QEMU user networking):")
    print(f"    packages.idx  ->  {qemu_note}/packages.idx")
    print(f"    DOOM.NPKG     ->  {qemu_note}/games/DOOM.NPKG")
    print()
    if args.bind == "127.0.0.1":
        print(f"  NOTE: bound to loopback only.")
        print(f"        Use --bind 0.0.0.0 for access from VirtualBox/VMware VMs.")
        print(f"        LAN IP appears to be: {local_ip}")
    print()
    print("  Press Ctrl+C to stop.")
    print()

    handler = http.server.SimpleHTTPRequestHandler
    handler.extensions_map = {
        ".npkg": "text/plain",
        ".idx":  "text/plain",
        ".txt":  "text/plain",
        "":      "application/octet-stream",
    }

    # Serve from the repo root so /packages/... URLs work
    import os
    os.chdir(ROOT_DIR)

    with http.server.HTTPServer((args.bind, args.port), handler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
