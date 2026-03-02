"""
FoundryVision — host/core/llm.py
Prompt engine and Foundry Local API call.
"""

from pathlib import Path
from typing import Optional
import requests

from .. import config

# ─── Prompt loading ───────────────────────────────────────────────────────────

def _load(name: str) -> str:
    p = Path(__file__).parent.parent / "prompts" / f"{name}.txt"
    return p.read_text(encoding="utf-8").strip() if p.exists() else ""

SYSTEM_PROMPT = _load("system")

# ─── Prompt construction ──────────────────────────────────────────────────────

def build_prompt(frame: dict, history: list[dict]) -> str:
    """
    Converts raw telemetry + recent history into a prompt for the LLM.
    Keeps numbers but contextualises them so the model can reason, not just read.
    """
    scores  = frame.get("scores", {})
    perf    = frame.get("perf", {})
    img     = frame.get("image", {})
    mem     = frame.get("memory", {})
    session = frame.get("session", {})

    # Trend detection across last 3 frames
    trend = ""
    if len(history) >= 3:
        recent = [h.get("scores", {}).get("person", 0) for h in history[-3:]]
        delta = recent[-1] - recent[0]
        if   delta >  0.10: trend = "Person score rising — something is entering frame."
        elif delta < -0.10: trend = "Person score falling — subject may be leaving frame."
        else:               trend = "Scores stable across recent frames."

    # Image health note
    img_note = ""
    if img.get("mean_brightness", 128) < 60:
        img_note = "Image is quite dark — lighting may be limiting the model."
    elif img.get("mean_brightness", 128) > 210:
        img_note = "Image is very bright — possible overexposure."
    elif img.get("contrast", 0.5) < 0.15:
        img_note = "Low contrast image — flat lighting, fewer edges for the model to use."
    elif img.get("clipped_pixels", 0) > 500:
        img_note = f"{img.get('clipped_pixels')} saturated pixels — some detail is lost."

    return (
        f"Frame #{frame.get('frame', '?')} telemetry from ESP32-S3:\n\n"
        f"Detection: person={scores.get('person', 0):.1%}  "
        f"no_person={scores.get('no_person', 0):.1%}  "
        f"confidence={scores.get('confidence', '?')}\n"
        f"Inference: {perf.get('inference_ms', 0):.1f} ms  "
        f"(session avg {perf.get('avg_ms', 0):.1f} ms)\n"
        f"Image: brightness={img.get('mean_brightness', 0):.0f}/255  "
        f"contrast={img.get('contrast', 0):.3f}  "
        f"dark={img.get('dark_ratio', 0):.1%}  "
        f"bright={img.get('bright_ratio', 0):.1%}  "
        f"clipped={img.get('clipped_pixels', 0)}px\n"
        f"Memory: arena={mem.get('arena_pct', 0):.1f}% used  "
        f"free_heap={mem.get('free_heap', 0)//1024}KB\n"
        f"Session: {session.get('frames', 0)} frames  "
        f"{session.get('detections', 0)} detections  "
        f"avg_person={session.get('avg_person', 0):.1%}\n"
        + (f"\nTrend: {trend}\n" if trend else "")
        + (f"Image note: {img_note}\n" if img_note else "")
        + "\nGive a short educational commentary on this frame."
    )

# ─── Foundry API call ─────────────────────────────────────────────────────────

def narrate(frame: dict, history: list[dict]) -> str:
    """
    Call Foundry Local and return narration text.
    Returns an error string (not an exception) on failure so callers don't need try/except.
    """
    prompt = build_prompt(frame, history)
    try:
        resp = requests.post(
            config.FOUNDRY_URL,
            json={
                "model":       config.FOUNDRY_MODEL,
                "messages":    [
                    {"role": "system", "content": SYSTEM_PROMPT},
                    {"role": "user",   "content": prompt},
                ],
                "max_tokens":  config.LLM_MAX_TOKENS,
                "temperature": config.LLM_TEMPERATURE,
            },
            timeout=config.LLM_TIMEOUT_S,
        )
        resp.raise_for_status()
        return resp.json()["choices"][0]["message"]["content"].strip()

    except requests.exceptions.ConnectionError:
        return (
            f"[LLM offline — run: foundry model run {config.FOUNDRY_MODEL}]"
        )
    except requests.exceptions.Timeout:
        return "[LLM timed out]"
    except Exception as e:
        return f"[LLM error: {e}]"
