#!/usr/bin/env python3
"""Phantasia Reimagined website + browser-client bridge.

Serves the static website from WEB_DIR and, at /ws, accepts WebSocket
connections from the browser client and bridges each one to the game
server's TCP port.  Uses only the Python standard library.

    server.py --port 8080 --dir /app/public_html --game-port 43302

/api/artifacts returns who holds each one-of-a-kind artifact, for the
website's Legends page; /api/world the state of the living realm; POST /api/autoplay/report takes an
autoplay record (sent on a death) and commits it to GitHub; /api/event the world event under way, for the
event banner on the website.

The bridge performs the game's connection handshake itself, sending the
player's real address (from X-Forwarded-For, set by Railway's proxy) so
the game's per-address limits apply to each player, not to the bridge.
"""

import argparse
import asyncio
import base64
import hashlib
import json
import mimetypes
import os
import struct
import sys
import time
import urllib.parse

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
CLIENT_VERSION = "1004"
MAX_HEADER = 16384
MAX_FRAME = 1 << 20

mimetypes.add_type("application/java-archive", ".jar")
mimetypes.add_type("text/javascript", ".js")
mimetypes.add_type("image/svg+xml", ".svg")


def log(message):
    print(message, flush=True)


# ---------------------------------------------------------------- HTTP

async def read_request(reader):
    data = await reader.readuntil(b"\r\n\r\n")
    if len(data) > MAX_HEADER:
        raise ValueError("header too large")
    lines = data.decode("latin-1").split("\r\n")
    method, target, _ = lines[0].split(" ", 2)
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            name, value = line.split(":", 1)
            headers[name.strip().lower()] = value.strip()
    return method, target, headers


def response_head(status, reason, headers):
    lines = ["HTTP/1.1 %d %s" % (status, reason)]
    for name, value in headers:
        lines.append("%s: %s" % (name, value))
    return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1")


async def send_simple(writer, status, reason, body=b""):
    writer.write(response_head(status, reason, [
        ("Content-Type", "text/plain; charset=utf-8"),
        ("Content-Length", str(len(body))),
        ("Connection", "close"),
    ]) + body)
    await writer.drain()


async def serve_file(writer, method, target, web_dir):
    path = urllib.parse.unquote(urllib.parse.urlsplit(target).path)
    full = os.path.realpath(os.path.join(web_dir, path.lstrip("/")))
    root = os.path.realpath(web_dir)

    if full != root and not full.startswith(root + os.sep):
        await send_simple(writer, 403, "Forbidden", b"Forbidden\n")
        return

    if os.path.isdir(full):
        if not path.endswith("/"):
            writer.write(response_head(301, "Moved Permanently", [
                ("Location", path + "/"), ("Content-Length", "0"),
                ("Connection", "close")]))
            await writer.drain()
            return
        full = os.path.join(full, "index.html")

    if not os.path.isfile(full):
        await send_simple(writer, 404, "Not Found", b"Not found\n")
        return

    ctype = mimetypes.guess_type(full)[0] or "application/octet-stream"
    # HTML pages declare their own charset (the old site is plain ASCII)
    if ctype.startswith("text/") and not full.endswith((".html", ".htm")):
        ctype += "; charset=utf-8"

    size = os.path.getsize(full)
    headers = [("Content-Type", ctype), ("Content-Length", str(size)),
               ("Connection", "close")]
    if full.endswith((".html", ".js", ".css")):
        headers.append(("Cache-Control", "no-cache"))
    writer.write(response_head(200, "OK", headers))

    if method != "HEAD":
        with open(full, "rb") as f:
            while True:
                chunk = f.read(65536)
                if not chunk:
                    break
                writer.write(chunk)
                await writer.drain()
    await writer.drain()


# ----------------------------------------------------------- WebSocket

def ws_frame(opcode, payload=b""):
    head = bytes([0x80 | opcode])
    n = len(payload)
    if n < 126:
        head += bytes([n])
    elif n < 65536:
        head += bytes([126]) + struct.pack("!H", n)
    else:
        head += bytes([127]) + struct.pack("!Q", n)
    return head + payload


async def ws_read_frame(reader):
    b1, b2 = await reader.readexactly(2)
    fin, opcode = b1 & 0x80, b1 & 0x0F
    masked, n = b2 & 0x80, b2 & 0x7F
    if n == 126:
        n = struct.unpack("!H", await reader.readexactly(2))[0]
    elif n == 127:
        n = struct.unpack("!Q", await reader.readexactly(8))[0]
    if n > MAX_FRAME:
        raise ValueError("frame too large")
    mask = await reader.readexactly(4) if masked else None
    payload = await reader.readexactly(n)
    if mask:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return fin, opcode, payload


def client_address(headers, writer):
    forwarded = headers.get("x-forwarded-for", "")
    if forwarded:
        return forwarded.split(",")[0].strip()
    peer = writer.get_extra_info("peername")
    return peer[0] if peer else ""


async def bridge(reader, writer, headers, game_port):
    key = headers.get("sec-websocket-key", "")
    accept = base64.b64encode(
        hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
    writer.write(response_head(101, "Switching Protocols", [
        ("Upgrade", "websocket"), ("Connection", "Upgrade"),
        ("Sec-WebSocket-Accept", accept)]))
    await writer.drain()

    address = client_address(headers, writer)

    try:
        game_reader, game_writer = await asyncio.open_connection(
            "127.0.0.1", game_port)
    except OSError as e:
        log("Browser client %s: game server unavailable (%s)" % (address, e))
        writer.write(ws_frame(0x1, b"8\nThe game server is not running right now.  Please try again later.\n"))
        writer.write(ws_frame(0x8, struct.pack("!H", 1011)))
        await writer.drain()
        return

    # do the game's handshake here: wait for HANDSHAKE_PACKET ("2\n")
    try:
        first = await asyncio.wait_for(game_reader.readuntil(b"\n"), 30)
    except Exception:
        first = b""
    if first.strip() == b"2":
        hello = ""
        for field in (CLIENT_VERSION, "0", "ip:" + address, str(int(time.time()))):
            hello += "1\0" + field + "\0"
        game_writer.write(hello.encode("latin-1"))
        await game_writer.drain()
    elif first:
        writer.write(ws_frame(0x2, first))

    async def game_to_browser():
        while True:
            data = await game_reader.read(65536)
            if not data:
                break
            writer.write(ws_frame(0x2, data))
            await writer.drain()

    async def browser_to_game():
        while True:
            fin, opcode, payload = await ws_read_frame(reader)
            if opcode == 0x8:                       # close
                break
            if opcode == 0x9:                       # ping
                writer.write(ws_frame(0xA, payload))
                await writer.drain()
            elif opcode in (0x0, 0x1, 0x2):         # data
                game_writer.write(payload)
                await game_writer.drain()

    async def keepalive():
        while True:
            await asyncio.sleep(25)
            writer.write(ws_frame(0x9))
            await writer.drain()

    tasks = [asyncio.ensure_future(t()) for t in
             (game_to_browser, browser_to_game, keepalive)]
    try:
        await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
    finally:
        for t in tasks:
            t.cancel()
        game_writer.close()
        try:
            writer.write(ws_frame(0x8, struct.pack("!H", 1000)))
            await writer.drain()
        except Exception:
            pass


# ------------------------------------------------------------- live data

def artifact_holders(data_dir):
    """[{id, holder}] from the game's artifact file (holder "" if lost)."""
    holders = {i: "" for i in range(1, 12)}  # artifacts 1-11
    try:
        with open(os.path.join(data_dir, "artifacts"), encoding="latin-1") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 2 and parts[0].isdigit():
                    n = int(parts[0])
                    if n in holders and parts[1] != "-":
                        holders[n] = parts[1]
    except OSError:
        pass
    return [{"id": n, "holder": h} for n, h in sorted(holders.items())]


EVENT_EFFECTS = {
    1: "monsters roam more often and give double experience",
    2: "treasure is far more common and gold finds are doubled",
    3: "champions stalk the realm in great numbers",
    4: "a monster army marches on the throne: slay monsters anywhere to push it back; deadlier, bigger monsters push hardest",
}


def world_event(data_dir):
    """The world event under way, from the game's worldevent file."""
    none = {"event": 0}
    try:
        with open(os.path.join(data_dir, "worldevent"), encoding="latin-1") as f:
            parts = f.read().split(None, 2)
        event, ends = int(parts[0]), int(parts[1])
        name = parts[2].strip() if len(parts) > 2 else ""
    except (OSError, ValueError, IndexError):
        return none
    left = ends - int(time.time())
    if event <= 0 or left <= 0:
        return none
    return {"event": event, "name": name, "secondsLeft": left,
            "effect": EVENT_EFFECTS.get(event, "")}


def world_state(data_dir):
    """The living realm (regions, towns, sieges, projects, nemeses, news),
    as the game publishes it in world.json."""
    try:
        with open(os.path.join(data_dir, "world.json"), encoding="latin-1") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


# ---------------------------------------------------------------- autoplay records

MAX_RECORD = 6 * 1024 * 1024
LOG_BRANCH_DEFAULT = "autoplay-logs"
recent_reports = {}


def autoplay_key_owner(data_dir, key):
    """The account and character a session's autoplay key was given to
    (the game lists each in data/autoplay-keys), or None."""
    if not key or len(key) != 32 or not all(ch in "0123456789abcdef" for ch in key):
        return None
    try:
        with open(os.path.join(data_dir, "autoplay-keys"), encoding="latin-1") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 3 and parts[0] == key:
                    return parts[1], parts[2]
    except OSError:
        pass
    return None


def github_request(method, url, token, body=None):
    import urllib.request
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={
        "Authorization": "Bearer " + token,
        "Accept": "application/vnd.github+json",
        "User-Agent": "phantasia-autoplay-records",
        "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read() or b"{}")


def push_record(path, content):
    """Commit an autoplay record to the log branch of PHANTASIA_LOG_REPO
    with PHANTASIA_GITHUB_TOKEN (the branch is made from the default
    branch the first time).  Returns where it went, or why not."""
    import urllib.error
    token = os.environ.get("PHANTASIA_GITHUB_TOKEN", "")
    repo = os.environ.get("PHANTASIA_LOG_REPO", "")
    branch = os.environ.get("PHANTASIA_LOG_BRANCH", LOG_BRANCH_DEFAULT)
    if not token or not repo:
        return "not sent: PHANTASIA_GITHUB_TOKEN and PHANTASIA_LOG_REPO are not set"
    api = "https://api.github.com/repos/" + repo
    try:
        try:
            github_request("GET", api + "/git/ref/heads/" + urllib.parse.quote(branch), token)
        except urllib.error.HTTPError as e:
            if e.code != 404:
                raise
            info = github_request("GET", api, token)
            head = github_request("GET", api + "/git/ref/heads/" + info["default_branch"], token)
            github_request("POST", api + "/git/refs", token,
                           {"ref": "refs/heads/" + branch, "sha": head["object"]["sha"]})
        github_request("PUT", api + "/contents/" + urllib.parse.quote(path), token, {
            "message": "Autoplay record: " + path.split("/")[-1],
            "content": base64.b64encode(content).decode(),
            "branch": branch})
        return "sent to %s on %s" % (path, branch)
    except Exception as e:                      # report, don't crash
        return "not sent: %r" % e


async def autoplay_report(reader, writer, headers, data_dir):
    """POST /api/autoplay/report: a character's autoplay record (sent when
    it dies).  Kept in data/autoplay-logs, and committed to GitHub when a
    token is configured."""
    try:
        length = int(headers.get("content-length", "0"))
    except ValueError:
        length = 0
    if length <= 0 or length > MAX_RECORD:
        await send_simple(writer, 413, "Too Large")
        return
    body = await asyncio.wait_for(reader.readexactly(length), 60)
    try:
        report = json.loads(body)
    except ValueError:
        await send_simple(writer, 400, "Bad Request")
        return
    owner = autoplay_key_owner(data_dir, str(report.get("key", "")))
    if not owner:
        await send_simple(writer, 403, "Forbidden")
        return
    now = time.time()
    if now - recent_reports.get(report.get("key"), 0) < 30:
        await send_simple(writer, 429, "Too Many Requests")
        return
    recent_reports[report.get("key")] = now

    character = "".join(ch for ch in str(report.get("character", owner[1])).lower()
                        if ch.isalnum())[:24] or "unknown"
    reason = "".join(ch for ch in str(report.get("reason", "record")).lower() if ch.isalnum())[:16]
    stamp = time.strftime("%Y-%m-%d/%H%M%S", time.gmtime(now))
    path = "autoplay-logs/%s-%s-%s.jsonl" % (stamp, character, reason or "record")
    lines = [json.dumps({"account": owner[0], "character": character, "reason": reason,
                         "summary": report.get("summary", {}), "at": int(now)})]
    for entry in report.get("log", [])[:50000]:
        lines.append(json.dumps(entry, separators=(",", ":")))
    content = ("\n".join(lines) + "\n").encode()

    local = os.path.join(data_dir, path)
    try:
        os.makedirs(os.path.dirname(local), exist_ok=True)
        with open(local, "wb") as f:
            f.write(content)
    except OSError:
        pass
    result = await asyncio.to_thread(push_record, path, content)
    log("autoplay record for %s: %s" % (character, result))
    await send_json(writer, {"ok": True, "result": result})


async def send_json(writer, data):
    body = json.dumps(data).encode()
    writer.write(response_head(200, "OK", [
        ("Content-Type", "application/json"),
        ("Content-Length", str(len(body))),
        ("Cache-Control", "no-cache"),
        ("Connection", "close")]) + body)
    await writer.drain()


# ---------------------------------------------------------------- main

async def handle(reader, writer, args):
    try:
        method, target, headers = await asyncio.wait_for(read_request(reader), 30)
        path = urllib.parse.urlsplit(target).path

        if path == "/ws" and headers.get("upgrade", "").lower() == "websocket":
            await bridge(reader, writer, headers, args.game_port)
        elif path == "/api/artifacts":
            await send_json(writer, artifact_holders(args.data_dir))
        elif path == "/api/event":
            await send_json(writer, world_event(args.data_dir))
        elif path == "/api/world":
            await send_json(writer, world_state(args.data_dir))
        elif path == "/api/autoplay/report" and method == "POST":
            await autoplay_report(reader, writer, headers, args.data_dir)
        elif method in ("GET", "HEAD"):
            await serve_file(writer, method, target, args.dir)
        else:
            await send_simple(writer, 405, "Method Not Allowed")
    except (asyncio.IncompleteReadError, asyncio.TimeoutError,
            ConnectionError, ValueError, asyncio.LimitOverrunError):
        pass
    except Exception as e:                          # keep serving others
        log("web server error: %r" % e)
    finally:
        try:
            writer.close()
        except Exception:
            pass


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--dir", default="/app/public_html")
    parser.add_argument("--game-port", type=int, default=43302)
    parser.add_argument("--data-dir", default="/app/phantasia/data")
    args = parser.parse_args()

    server = await asyncio.start_server(
        lambda r, w: handle(r, w, args), "0.0.0.0", args.port,
        limit=MAX_HEADER)
    log("Website listening on port %d" % args.port)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        sys.exit(0)
