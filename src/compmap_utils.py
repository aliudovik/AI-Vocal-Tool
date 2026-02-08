# src/compmap_utils.py
"""
Utilities for compmap boundary/crossfade window computation and validation.
"""
from __future__ import annotations
from typing import Any, Dict, List, Optional


def compute_default_boundaries(
        segments: List[Dict[str, Any]],
        fade_fraction: float,
        min_fade_s: float = 0.030,
        max_fade_s: float = 0.500,
        eps: float = 1e-9,
) -> List[Dict[str, Any]]:
    """
    Compute default per-boundary crossfade windows matching the current stitcher logic.

    For each boundary b between segments[b] and segments[b+1]:
    - fade = fade_fraction * min(dur_left, dur_right)
    - clamp fade to [min_fade_s, max_fade_s]
    - fade cannot exceed either adjacent segment duration
    - window is centered at boundary (= segments[b].end_s)
    - returns xfadeStartSec and xfadeEndSec for the mix region

    Args:
        segments: list of segment dicts with 'start_s' and 'end_s' keys
        fade_fraction: fraction of shorter adjacent segment to use for crossfade
        min_fade_s: minimum crossfade duration (default 30 ms)
        max_fade_s: maximum crossfade duration (default 500 ms)
        eps: tolerance for floating-point comparisons

    Returns:
        list of boundary dicts with leftSegIndex, rightSegIndex, xfadeStartSec, xfadeEndSec
    """
    if not segments:
        return []

    segs = sorted(segments, key=lambda s: float(s["start_s"]))
    out: List[Dict[str, Any]] = []

    ff = float(fade_fraction)
    ff = max(0.0, ff)

    for i in range(len(segs) - 1):
        seg_l = segs[i]
        seg_r = segs[i + 1]

        s_l = float(seg_l["start_s"])
        e_l = float(seg_l["end_s"])
        s_r = float(seg_r["start_s"])
        e_r = float(seg_r["end_s"])

        boundary = e_l
        d1 = max(0.0, e_l - s_l)
        d2 = max(0.0, e_r - s_r)

        if d1 <= 0.0 or d2 <= 0.0 or ff <= 0.0:
            # Disabled crossfade (zero-width)
            xfade_start = boundary
            xfade_end = boundary
        else:
            fade = ff * min(d1, d2)
            fade = max(min_fade_s, fade)
            fade = min(fade, max_fade_s, d1, d2)
            half = 0.5 * fade

            xfade_start = boundary - half
            xfade_end = boundary + half

            # Safety clamps to respect segment extents
            xfade_start = max(s_l, min(xfade_start, e_l))
            xfade_end = max(s_r, min(xfade_end, e_r))

            # If clamps collapse window, disable it
            if xfade_end <= xfade_start + eps:
                xfade_start = boundary
                xfade_end = boundary

        out.append(
            {
                "leftSegIndex": int(seg_l.get("index", i)),
                "rightSegIndex": int(seg_r.get("index", i + 1)),
                "xfadeStartSec": float(xfade_start),
                "xfadeEndSec": float(xfade_end),
            }
        )

    return out


def validate_or_none(
        segments: List[Dict[str, Any]],
        boundaries: Any,
        eps: float = 1e-9,
) -> Optional[List[Dict[str, Any]]]:
    """
    Validate boundaries against segments. Return normalized boundaries if valid, else None.

    Validation rules:
    - boundaries must be a list with length = len(segments) - 1
    - each boundary must have xfadeStartSec and xfadeEndSec
    - xfadeStartSec must lie within left segment [start_s, end_s]
    - xfadeEndSec must lie within right segment [start_s, end_s]
    - xfadeStartSec <= boundary <= xfadeEndSec (boundary = left segment end_s)
    - xfadeStartSec < xfadeEndSec (or both equal at boundary for disabled crossfade)

    Args:
        segments: list of segment dicts with 'start_s' and 'end_s'
        boundaries: candidate boundaries list from JSON
        eps: floating-point tolerance

    Returns:
        normalized boundaries list if valid, else None
    """
    if not isinstance(boundaries, list):
        return None

    segs = sorted(segments, key=lambda s: float(s["start_s"]))
    if len(boundaries) != max(0, len(segs) - 1):
        return None

    norm: List[Dict[str, Any]] = []
    for i in range(len(segs) - 1):
        seg_l = segs[i]
        seg_r = segs[i + 1]
        s_l = float(seg_l["start_s"])
        e_l = float(seg_l["end_s"])
        s_r = float(seg_r["start_s"])
        e_r = float(seg_r["end_s"])
        boundary = e_l

        b = boundaries[i]
        if not isinstance(b, dict):
            return None
        if "xfadeStartSec" not in b or "xfadeEndSec" not in b:
            return None

        xs = float(b["xfadeStartSec"])
        xe = float(b["xfadeEndSec"])

        # Allow "disabled" window (xs == xe == boundary)
        if abs(xs - boundary) <= eps and abs(xe - boundary) <= eps:
            norm.append(
                {
                    "leftSegIndex": int(seg_l.get("index", i)),
                    "rightSegIndex": int(seg_r.get("index", i + 1)),
                    "xfadeStartSec": float(xs),
                    "xfadeEndSec": float(xe),
                }
            )
            continue

        # Validate non-zero crossfade window
        if not (xs + eps < xe):
            return None
        if not (s_l - eps <= xs <= e_l + eps):
            return None
        if not (s_r - eps <= xe <= e_r + eps):
            return None
        if not (xs - eps <= boundary <= xe + eps):
            return None

        norm.append(
            {
                "leftSegIndex": int(seg_l.get("index", i)),
                "rightSegIndex": int(seg_r.get("index", i + 1)),
                "xfadeStartSec": float(xs),
                "xfadeEndSec": float(xe),
            }
        )

    return norm
