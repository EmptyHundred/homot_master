#!/usr/bin/env python3
"""
Test harness for homot-lspa.
Sends JSON-RPC messages over stdin/stdout and prints responses.
"""

import json
import subprocess
import sys
import os
import struct

LSPA_EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "bin", "linter", "homot-lspa.windows.template_debug.x86_64.exe")
DB_PATH = "D:/HolyMolly/HMClaw/homot-lsp-bin/linterdb.json"


def send_message(proc, msg):
    """Send a JSON-RPC message with Content-Length header."""
    body = json.dumps(msg).encode("utf-8")
    header = f"Content-Length: {len(body)}\r\n\r\n".encode("ascii")
    proc.stdin.write(header + body)
    proc.stdin.flush()


def read_message(proc):
    """Read a JSON-RPC response with Content-Length header."""
    # Read headers
    content_length = -1
    while True:
        line = b""
        while True:
            ch = proc.stdout.read(1)
            if not ch:
                return None
            if ch == b"\r":
                next_ch = proc.stdout.read(1)
                if next_ch == b"\n":
                    break
                line += ch + next_ch
            else:
                line += ch
        line_str = line.decode("ascii", errors="replace")
        if not line_str:
            break  # Empty line = end of headers
        if line_str.startswith("Content-Length:"):
            content_length = int(line_str[15:].strip())

    if content_length <= 0:
        return None

    body = proc.stdout.read(content_length)
    return json.loads(body.decode("utf-8"))


def rpc_request(proc, id, method, params=None):
    """Send a request and return the response."""
    msg = {"jsonrpc": "2.0", "id": id, "method": method}
    if params is not None:
        msg["params"] = params
    send_message(proc, msg)
    return read_message(proc)


def print_result(label, response):
    """Pretty-print a response."""
    print(f"\n{'='*60}")
    print(f"  {label}")
    print(f"{'='*60}")
    if response is None:
        print("  (no response)")
        return
    if "error" in response:
        print(f"  ERROR: {response['error']}")
        return
    result = response.get("result", response)
    print(json.dumps(result, indent=2, ensure_ascii=False)[:3000])
    if len(json.dumps(result, ensure_ascii=False)) > 3000:
        print("  ... (truncated)")


def main():
    exe = LSPA_EXE
    if not os.path.exists(exe):
        print(f"ERROR: {exe} not found")
        sys.exit(1)
    if not os.path.exists(DB_PATH):
        print(f"ERROR: {DB_PATH} not found")
        sys.exit(1)

    print(f"Starting: {os.path.basename(exe)}")
    print(f"DB: {DB_PATH}")

    proc = subprocess.Popen(
        [exe, "--db", DB_PATH],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    try:
        # 1. Initialize
        resp = rpc_request(proc, 1, "initialize", {})
        print_result("initialize", resp)

        # 2. api/class — query CharacterBody3D (standard)
        resp = rpc_request(proc, 2, "api/class", {
            "name": "CharacterBody3D",
            "detail": "standard"
        })
        print_result("api/class CharacterBody3D (standard)", resp)

        # 3. api/class — names_only
        resp = rpc_request(proc, 3, "api/class", {
            "name": "Node3D",
            "detail": "names_only"
        })
        print_result("api/class Node3D (names_only)", resp)

        # 4. api/classes — batch query
        resp = rpc_request(proc, 4, "api/classes", {
            "names": ["Area3D", "RigidBody3D", "NonExistentClass"],
            "detail": "names_only"
        })
        print_result("api/classes batch", resp)

        # 5. api/search — keyword search
        resp = rpc_request(proc, 5, "api/search", {
            "query": "body entered",
            "limit": 5
        })
        print_result("api/search 'body entered'", resp)

        # 6. api/hierarchy — up
        resp = rpc_request(proc, 6, "api/hierarchy", {
            "name": "CharacterBody3D",
            "direction": "up"
        })
        print_result("api/hierarchy CharacterBody3D (up)", resp)

        # 7. api/hierarchy — down
        resp = rpc_request(proc, 7, "api/hierarchy", {
            "name": "Node3D",
            "direction": "down"
        })
        print_result("api/hierarchy Node3D (down)", resp)

        # 8. api/globals — singletons
        resp = rpc_request(proc, 8, "api/globals", {
            "category": "singletons"
        })
        print_result("api/globals singletons", resp)

        # 9. api/globals — utility_functions
        resp = rpc_request(proc, 9, "api/globals", {
            "category": "utility_functions"
        })
        print_result("api/globals utility_functions", resp)

        # 10. api/catalog — 3d domain
        resp = rpc_request(proc, 10, "api/catalog", {
            "domain": "3d"
        })
        print_result("api/catalog (3d)", resp)

        # 11. verify/check — inline code with error
        resp = rpc_request(proc, 11, "verify/check", {
            "content": 'extends CharacterBody3D\n\nfunc _ready():\n\tvar x: int = "hello"\n',
            "filename": "test.gd",
            "severity": "all"
        })
        print_result("verify/check (type error)", resp)

        # 12. verify/check — clean code
        resp = rpc_request(proc, 12, "verify/check", {
            "content": 'extends Node3D\n\nfunc _ready():\n\tprint("hello")\n',
            "filename": "clean.gd",
            "severity": "all"
        })
        print_result("verify/check (clean code)", resp)

        # 13. api/search — case insensitive class lookup
        resp = rpc_request(proc, 13, "api/class", {
            "name": "characterbody3d",
            "detail": "names_only"
        })
        print_result("api/class case-insensitive lookup", resp)

        # 14. Shutdown
        resp = rpc_request(proc, 14, "shutdown", {})
        print_result("shutdown", resp)

        # 15. Exit
        send_message(proc, {"jsonrpc": "2.0", "method": "exit"})

    finally:
        proc.wait(timeout=5)
        stderr = proc.stderr.read().decode("utf-8", errors="replace")
        if stderr.strip():
            print(f"\n--- stderr ---\n{stderr[:1000]}")
        print(f"\nProcess exited with code: {proc.returncode}")


if __name__ == "__main__":
    main()
