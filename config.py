"""
FoundryVision — host/config.py
All host-side configuration. Edit here, not scattered through the code.
"""

# ─── Network ──────────────────────────────────────────────────────────────────
UDP_HOST = "0.0.0.0"
UDP_PORT = 5005          # must match FV_HOST_PORT in esp/main/fv_config.h

# ─── LLM ──────────────────────────────────────────────────────────────────────
FOUNDRY_URL     = "http://localhost:5272/v1/chat/completions"
FOUNDRY_MODEL   = "phi3.5-mini-instruct"   # or qwen2.5-0.5b, mistral, etc.
LLM_MAX_TOKENS  = 180
LLM_TEMPERATURE = 0.7
LLM_TIMEOUT_S   = 8

# Narrate every N frames. Higher = less LLM load, larger gaps between narration.
NARRATE_EVERY_N = 3

# ─── Dashboard ────────────────────────────────────────────────────────────────
DASHBOARD_HOST = "0.0.0.0"
DASHBOARD_PORT = 8080     # open http://localhost:8080 in a browser

# How many frames to keep in the dashboard history chart
DASHBOARD_HISTORY = 60

# ─── Logging ──────────────────────────────────────────────────────────────────
LOG_DIR = "logs"          # relative to host/
