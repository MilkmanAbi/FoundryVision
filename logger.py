"""
FoundryVision — host/core/logger.py
JSONL session logger. Each line is one frame + narration.
"""

import json
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

from .. import config


class SessionLogger:
    def __init__(self):
        log_dir = Path(__file__).parent.parent / config.LOG_DIR
        log_dir.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        self._path = log_dir / f"session_{ts}.jsonl"
        self._fh   = self._path.open("w", encoding="utf-8")

    def log(self, frame: dict, narration: Optional[str]):
        entry = {
            **frame,
            "narration":  narration,
            "logged_at":  time.time(),
        }
        self._fh.write(json.dumps(entry) + "\n")
        self._fh.flush()

    def path(self) -> Path:
        return self._path

    def close(self):
        self._fh.close()
