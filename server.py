"""
FoundryVision — host/server.py
Entry point. Wires together: UDP receiver → LLM → WebSocket dashboard + logger.

Usage:
    python -m host.server                   # WiFi UDP mode
    python -m host.server --serial COM3     # Serial/USB mode
    python -m host.server --no-llm          # Skip LLM, raw telemetry only
"""

import argparse
import asyncio
import json
import threading
from collections import deque
from pathlib import Path

import websockets
import websockets.server

from . import config
from .core.receiver import UDPReceiver, SerialReceiver
from .core.llm      import narrate
from .core.logger   import SessionLogger


# ─── Shared state ─────────────────────────────────────────────────────────────

_ws_clients: set = set()
_frame_history: deque = deque(maxlen=10)  # for LLM trend context
_loop: asyncio.AbstractEventLoop = None


async def _broadcast(msg: dict):
    """Send a message to all connected dashboard clients."""
    if not _ws_clients:
        return
    payload = json.dumps(msg)
    dead = set()
    for ws in _ws_clients:
        try:
            await ws.send(payload)
        except Exception:
            dead.add(ws)
    _ws_clients -= dead


# ─── WebSocket handler ────────────────────────────────────────────────────────

async def _ws_handler(websocket):
    _ws_clients.add(websocket)
    try:
        await websocket.wait_closed()
    finally:
        _ws_clients.discard(websocket)


# ─── Dashboard file server ────────────────────────────────────────────────────

async def _http_handler(path, request_headers):
    """Minimal HTTP handler to serve the dashboard HTML."""
    if path in ("/", "/index.html"):
        html = (Path(__file__).parent / "dashboard" / "index.html").read_bytes()
        return (200, [("Content-Type", "text/html; charset=utf-8")], html)
    return (404, [], b"not found")


# ─── Frame processing (runs in a background thread) ──────────────────────────

def _process_frames(receiver, logger: SessionLogger, no_llm: bool):
    frame_count = 0
    while True:
        frame = receiver.queue.get()  # blocks until a frame arrives
        frame_count += 1

        # Push to dashboard
        if _loop:
            asyncio.run_coroutine_threadsafe(
                _broadcast({"type": "frame", "data": frame}), _loop
            )

        narration = None
        if not no_llm and frame_count % config.NARRATE_EVERY_N == 0:
            narration = narrate(frame, list(_frame_history))
            if _loop:
                asyncio.run_coroutine_threadsafe(
                    _broadcast({
                        "type":  "narration",
                        "data":  narration,
                        "frame": frame.get("frame"),
                    }),
                    _loop
                )

        _frame_history.append(frame)
        logger.log(frame, narration)

        # Pretty terminal output
        _print_frame(frame, narration)


def _print_frame(frame: dict, narration: str | None):
    scores  = frame.get("scores",  {})
    perf    = frame.get("perf",    {})
    session = frame.get("session", {})
    conf    = scores.get("confidence", "?")

    CONF_COLOUR = {
        "very_high": "\033[92m",
        "high":      "\033[32m",
        "uncertain": "\033[93m",
        "low":       "\033[33m",
        "very_low":  "\033[90m",
    }
    RESET = "\033[0m"
    DIM   = "\033[90m"
    CYAN  = "\033[96m"

    col = CONF_COLOUR.get(conf, "")
    print(
        f"{DIM}#{frame.get('frame', '?'):>6}{RESET}  "
        f"{col}{scores.get('person', 0):>6.1%}{RESET}  "
        f"{DIM}{perf.get('inference_ms', 0):>6.1f}ms{RESET}  "
        f"{col}{conf:<10}{RESET}"
    )
    if narration:
        print(f"\n  {CYAN}▸{RESET} {narration}\n")


# ─── Main ─────────────────────────────────────────────────────────────────────

async def _main(args):
    global _loop
    _loop = asyncio.get_running_loop()

    # Receiver
    if args.serial:
        receiver = SerialReceiver(args.serial)
    else:
        receiver = UDPReceiver()
    receiver.start()

    # Logger
    logger = SessionLogger()

    # Frame processor thread
    processor = threading.Thread(
        target=_process_frames,
        args=(receiver, logger, args.no_llm),
        daemon=True,
        name="frame-processor",
    )
    processor.start()

    # WebSocket + dashboard server
    server = await websockets.server.serve(
        _ws_handler,
        config.DASHBOARD_HOST,
        config.DASHBOARD_PORT,
        process_request=_http_handler,
    )

    mode = f"serial {args.serial}" if args.serial else f"UDP :{config.UDP_PORT}"
    llm  = f"off" if args.no_llm else config.FOUNDRY_MODEL

    print(
        f"\n  FoundryVision — host server\n"
        f"  Input:     {mode}\n"
        f"  LLM:       {llm}\n"
        f"  Dashboard: http://localhost:{config.DASHBOARD_PORT}\n"
        f"  Logs:      {logger.path()}\n"
    )

    try:
        await server.wait_closed()
    except (KeyboardInterrupt, asyncio.CancelledError):
        pass
    finally:
        receiver.stop()
        logger.close()
        print("\nStopped.")


def main():
    parser = argparse.ArgumentParser(description="FoundryVision host server")
    parser.add_argument("--serial", metavar="PORT",
                        help="Serial port instead of UDP (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--no-llm", action="store_true",
                        help="Disable LLM narration")
    parser.add_argument("--model", default=None,
                        help=f"Override LLM model (default: {config.FOUNDRY_MODEL})")
    args = parser.parse_args()

    if args.model:
        config.FOUNDRY_MODEL = args.model

    try:
        asyncio.run(_main(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
