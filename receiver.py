"""
FoundryVision — host/core/receiver.py
Listens for UDP frames from the ESP32-S3 and puts parsed dicts on a queue.
Also supports serial (USB) mode as a drop-in alternative.
"""

import json
import queue
import socket
import threading
from typing import Optional

from .. import config


class UDPReceiver(threading.Thread):
    """
    Listens on UDP, parses JSON frames, puts them on self.queue.
    Non-blocking — runs as a daemon thread.
    """

    def __init__(self):
        super().__init__(daemon=True, name="udp-receiver")
        self.queue: queue.Queue[dict] = queue.Queue(maxsize=64)
        self._stop = threading.Event()

    def run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.settimeout(1.0)
        sock.bind((config.UDP_HOST, config.UDP_PORT))

        buf = ""
        while not self._stop.is_set():
            try:
                data, _ = sock.recvfrom(4096)
                buf += data.decode("utf-8", errors="replace")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        frame = json.loads(line)
                        self.queue.put_nowait(frame)
                    except (json.JSONDecodeError, queue.Full):
                        pass
            except socket.timeout:
                continue
            except Exception:
                if not self._stop.is_set():
                    raise

        sock.close()

    def stop(self):
        self._stop.set()


class SerialReceiver(threading.Thread):
    """
    Reads JSON frames from a serial port (USB). Same API as UDPReceiver.
    Requires: pip install pyserial
    """

    def __init__(self, port: str, baud: int = 115200):
        super().__init__(daemon=True, name="serial-receiver")
        self.queue: queue.Queue[dict] = queue.Queue(maxsize=64)
        self._stop = threading.Event()
        self._port = port
        self._baud = baud

    def run(self):
        import serial  # optional dependency
        buf = ""
        with serial.Serial(self._port, self._baud, timeout=1.0) as ser:
            while not self._stop.is_set():
                chunk = ser.readline().decode("utf-8", errors="replace")
                buf += chunk
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        frame = json.loads(line)
                        self.queue.put_nowait(frame)
                    except (json.JSONDecodeError, queue.Full):
                        pass

    def stop(self):
        self._stop.set()
