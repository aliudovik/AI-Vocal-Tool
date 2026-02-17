# src/stitch_from_compmap.py
#
# Read a compmap-*.json file and stitch the winner segments into
# a single comped WAV (48 kHz), with simple crossfades at boundaries.
#
# Assumptions:
#   - All takes for the phrase are time-aligned and same length (your pipeline).
#   - Audio is mono, or can be treated as mono.
#   - Take IDs in JSON (e.g. "take_1") correspond to "<take_id>.wav" files
#     in: base_dir / relative_path / "<take_id>.wav".
#
# Usage example:
#   python -m src.stitch_from_compmap \
#       --compmap outputs/compmap-01-02-50.json \
#       --out outputs/comped-01-02-50.wav

import argparse
import json
import os
from pathlib import Path

import numpy as np
import soundfile as sf

from src.io import load_wav # src there


from src.compmap_utils import compute_default_boundaries, validate_or_none # src there

PROJECT_ROOT = Path(__file__).resolve().parent.parent


PER_TAKE_NORMALIZE_DBFS = -3.0 # we need per take normalization, different segment volume bugs otherwise....

debug_emotion = False


def _load_compmap(path):
    """Load compmap JSON (resolve relative to project root if needed)."""
    p = Path(path)
    if not p.is_absolute():
        # Interpret relative paths from the project root
        p = PROJECT_ROOT / p
    with open(p, "r", encoding="utf-8") as f:
        return json.load(f)


def _load_comp_audio(compmap, take_ids, base_override=None):
    """
    Load WAVs for all provided take IDs referenced in the compmap phrase folder.
    Returns:
      sr (int),
      dict: take_id -> waveform (np.ndarray, mono float32)
    """
    base_dir = base_override if base_override is not None else compmap["base_dir"]
    rel = compmap["relative_path"]

    base_dir_path = Path(base_dir)
    if not base_dir_path.is_absolute():
        # Interpret base_dir (e.g. "data_pilot") from the project root
        base_dir_path = PROJECT_ROOT / base_dir_path

    phrase_dir = base_dir_path / rel

    take_ids = set(take_ids)

    audio = {}
    sr_ref = None

    for take_id in sorted(take_ids):
        wav_path = (phrase_dir / f"{take_id}.wav").as_posix()
        if not os.path.isfile(wav_path):
            raise FileNotFoundError(f"Expected WAV not found: {wav_path}")

        # Use project I/O helper to ensure 48 kHz, mono
        y, sr, _, _ = load_wav(wav_path, target_sr=48000, f0_sr=16000)
        if sr_ref is None:
            sr_ref = sr
        elif sr != sr_ref:
            raise RuntimeError(
                f"Sample rate mismatch for {take_id}: {sr} vs {sr_ref}"
            )

        audio[take_id] = y.astype(np.float32)

        if PER_TAKE_NORMALIZE_DBFS is not None:
            y = _peak_normalize(y, target_dbfs=PER_TAKE_NORMALIZE_DBFS)

        audio[take_id] = y

    if sr_ref is None:
        raise RuntimeError("No takes loaded from compmap; segments empty?")

    return sr_ref, audio


def _segment_ranked_take_ids(segment):
    """
    Return ordered unique take IDs for one segment: winner first, then candidates.
    """
    ranked = []

    winner = segment.get("winner", {})
    winner_take = winner.get("take")
    if isinstance(winner_take, str) and winner_take:
        ranked.append(winner_take)

    for cand in segment.get("candidates", []):
        if not isinstance(cand, dict):
            continue
        take_id = cand.get("take")
        if isinstance(take_id, str) and take_id:
            ranked.append(take_id)

    # Preserve order, remove duplicates
    out = []
    seen = set()
    for take_id in ranked:
        if take_id in seen:
            continue
        seen.add(take_id)
        out.append(take_id)
    return out


def _build_take_plan(segments_sorted, rank_index):
    """
    Build per-segment take selection for a given rank index.
    rank_index=0 => best; 1 => second-best fallback to best; etc.
    """
    take_plan = []
    for seg in segments_sorted:
        ranked = _segment_ranked_take_ids(seg)
        if not ranked:
            raise RuntimeError(f"Segment missing take choices: {seg}")
        idx = min(rank_index, len(ranked) - 1)
        take_plan.append(ranked[idx])
    return take_plan


def _derive_output_paths(out_path, num_outputs):
    out_path = Path(out_path)
    if num_outputs <= 1:
        return [out_path]

    paths = [out_path]
    for i in range(1, num_outputs):
        paths.append(out_path.with_name(f"{out_path.stem}-alt{i}{out_path.suffix}"))
    return paths


def _crossfade_concat(a, b, sr, crossfade_ms):
    """
    Concatenate arrays a and b with a linear crossfade of length crossfade_ms.
    Returns a new array (float32).

    NOTE: kept for reference but NOT used in the current time-aligned comping
    implementation, which works on a fixed global timeline instead of
    concatenating segments.
    """
    if a.size == 0:
        return b.astype(np.float32)
    if b.size == 0:
        return a.astype(np.float32)

    if crossfade_ms <= 0.0:
        # Hard splice, no crossfade
        return np.concatenate([a, b]).astype(np.float32)

    fade_len = int(round((crossfade_ms / 1000.0) * sr))
    if fade_len <= 0:
        return np.concatenate([a, b]).astype(np.float32)

    # Don't exceed segment lengths
    fade_len = min(fade_len, a.size, b.size)
    if fade_len <= 0:
        return np.concatenate([a, b]).astype(np.float32)

    a_main = a[:-fade_len]
    a_tail = a[-fade_len:]
    b_head = b[:fade_len]
    b_rest = b[fade_len:]

    fade_out = np.linspace(1.0, 0.0, fade_len, dtype=np.float32)
    fade_in = 1.0 - fade_out

    cross = a_tail * fade_out + b_head * fade_in

    out = np.concatenate([a_main, cross, b_rest]).astype(np.float32)
    return out


def _peak_normalize(x, target_dbfs=-1.0):
    """
    Peak-normalize signal to target dBFS.
    Example: -1.0 dBFS ≈ 0.89 linear peak.
    """
    # Convert dBFS to linear
    target_linear = 10.0 ** (target_dbfs / 20.0)
    peak = np.max(np.abs(x))
    if peak <= 0.0:
        return x.astype(np.float32)

    gain = target_linear / peak
    return (x * gain).astype(np.float32)


def _compute_crossfade_weights(length, curve="linear"):
    """
    Compute crossfade weight arrays for prev and next takes.

    Args:
        length: number of samples in crossfade window
        curve: "linear" or "equal_power"

    Returns:
        (w_prev, w_next): numpy arrays of shape (length,) summing to 1.0
    """
    if length <= 0:
        return np.array([], dtype=np.float32), np.array([], dtype=np.float32)

    t = np.linspace(0.0, 1.0, length, dtype=np.float32)

    if curve == "equal_power":
        # Equal-power crossfade: sin/cos curves that preserve energy
        # prev: cos(t * π/2)  (1 -> 0)
        # next: sin(t * π/2)  (0 -> 1)
        w_prev = np.cos(t * np.pi / 2.0)
        w_next = np.sin(t * np.pi / 2.0)
    else:
        # Linear crossfade (default)
        w_next = t
        w_prev = 1.0 - t

    return w_prev.astype(np.float32), w_next.astype(np.float32)


def stitch_from_compmap(
    compmap_path,
    out_path,
    fade_fraction=0.15,
    base_override=None,
    verbose=False,
    xfade_curve="linear",
    num_alternatives=1,
):
    """
    Main stitching routine.

    Args:
      compmap_path (str): path to compmap-*.json file.
      out_path (str): output WAV path.
      fade_fraction (float): fraction of the shorter adjacent segment duration
         used to derive crossfade length, clamped between 30 ms and 500 ms.
      base_override (str or None): override compmap["base_dir"] if provided.
      verbose (bool): print what is being stitched.
      xfade_curve (str): "linear" (default) or "equal_power".
      num_alternatives (int): target number of outputs (max practical 3).
         Output count automatically falls back if alternatives collapse.

    Returns:
      list[pathlib.Path]: written output WAV paths (1..num_alternatives)
    """
    compmap = _load_compmap(compmap_path)
    phrase = compmap.get("phrase", "")
    segments = compmap.get("segments", [])

    if not segments:
        raise RuntimeError("Compmap has no segments.")

    requested_alts = int(num_alternatives) if num_alternatives is not None else 1
    requested_alts = max(1, requested_alts)

    # Normalize/validate curve name (keep linear default for minimal risk)
    curve = (xfade_curve or "linear").strip().lower()
    if curve in ("equalpower", "equal_power", "equal-power", "ep"):
        curve = "equal_power"
    elif curve in ("linear", "lin"):
        curve = "linear"
    else:
        raise ValueError(
            f"Unsupported xfade_curve={curve!r}. Use 'linear' or 'equal_power'."
        )




    # Sort segments by time
    segments_sorted = sorted(segments, key=lambda s: s["start_s"])
    num_segs = len(segments_sorted)

    # Build ranked take plans and keep unique plans only
    take_plans = []
    seen_plans = set()
    for rank_idx in range(requested_alts):
        plan = tuple(_build_take_plan(segments_sorted, rank_idx))
        if plan in seen_plans:
            continue
        seen_plans.add(plan)
        take_plans.append(plan)

    # Collect segment times
    start_s = np.array([float(s["start_s"]) for s in segments_sorted], dtype=float)
    end_s = np.array([float(s["end_s"]) for s in segments_sorted], dtype=float)
    dur_s = end_s - start_s

    all_take_ids = {take_id for plan in take_plans for take_id in plan}
    sr, audio = _load_comp_audio(compmap, all_take_ids, base_override=base_override)

    phrase_end_s = float(end_s.max())
    # Use explicit compmap boundaries if present/valid, otherwise compute defaults at runtime.
    boundaries = validate_or_none(segments_sorted, compmap.get("boundaries", None))
    if boundaries is None:
        boundaries = compute_default_boundaries(segments_sorted, fade_fraction)

    def _render_from_take_plan(take_ids):
        # Base on compmap end time; also ensure we don't go past actual take length
        first_take = audio[take_ids[0]]
        max_samples_from_takes = first_take.size
        target_samples_local = int(round(phrase_end_s * sr))
        target_samples_local = min(target_samples_local, max_samples_from_takes)

        out_wave_local = np.zeros(target_samples_local, dtype=np.float32)

        # Step 1: hard comp (no crossfades) on a fixed global timeline
        for idx, seg in enumerate(segments_sorted):
            take_id = take_ids[idx]
            y = audio[take_id]

            start_sample = int(round(start_s[idx] * sr))
            end_sample = int(round(end_s[idx] * sr))

            # Clamp to valid range
            start_sample = max(0, min(start_sample, target_samples_local))
            end_sample = max(start_sample, min(end_sample, target_samples_local, y.size))

            if end_sample <= start_sample:
                if verbose:
                    print(
                        f"Skipping empty segment idx={seg['index']} "
                        f"start={start_s[idx]:.3f} end={end_s[idx]:.3f}"
                    )
                continue

            seg_wave = y[start_sample:end_sample]
            out_wave_local[start_sample:end_sample] = seg_wave.astype(np.float32)

            if verbose:
                dur_seg = (end_sample - start_sample) / float(sr)
                print(
                    f"Segment {seg['index']:02d}: {start_s[idx]:7.3f}s -> {end_s[idx]:7.3f}s "
                    f"({dur_seg:5.3f}s) from {take_id}"
                )

        # Step 2: apply time-aligned crossfades around boundaries
        for b in range(num_segs - 1):
            seg_l = segments_sorted[b]
            seg_r = segments_sorted[b + 1]

            boundary_s = float(end_s[b])
            xfade_start_s = float(boundaries[b]["xfadeStartSec"])
            xfade_end_s = float(boundaries[b]["xfadeEndSec"])

            # Safety constraints: clamp crossfade window to segment bounds
            xfade_start_s = max(float(seg_l["start_s"]), xfade_start_s)
            xfade_end_s = min(float(seg_r["end_s"]), xfade_end_s)
            xfade_start_s = min(xfade_start_s, boundary_s)
            xfade_end_s = max(xfade_end_s, boundary_s)

            if xfade_end_s <= xfade_start_s:
                if verbose:
                    print(f"Boundary {b}: skipping crossfade (window collapsed to zero)")
                continue

            start_sample_cf = int(round(xfade_start_s * sr))
            end_sample_cf = int(round(xfade_end_s * sr))
            start_sample_cf = max(0, min(start_sample_cf, target_samples_local))
            end_sample_cf = max(start_sample_cf, min(end_sample_cf, target_samples_local))

            length_cf = end_sample_cf - start_sample_cf
            if length_cf <= 1:
                if verbose:
                    print(
                        f"Boundary {b}: skipping crossfade (window too short: {length_cf} samples)"
                    )
                continue

            prev_take_id = take_ids[b]
            next_take_id = take_ids[b + 1]
            y_prev = audio[prev_take_id]
            y_next = audio[next_take_id]

            # Clamp to available samples in each take
            end_sample_cf_prev = min(end_sample_cf, y_prev.size)
            end_sample_cf_next = min(end_sample_cf, y_next.size)

            length_cf = min(
                length_cf,
                end_sample_cf_prev - start_sample_cf,
                end_sample_cf_next - start_sample_cf,
            )
            if length_cf <= 1:
                if verbose:
                    print(
                        f"Boundary {b}: skipping crossfade (insufficient audio in takes)"
                    )
                continue

            end_sample_cf = start_sample_cf + length_cf
            prev_slice = y_prev[start_sample_cf:end_sample_cf].astype(np.float32)
            next_slice = y_next[start_sample_cf:end_sample_cf].astype(np.float32)
            w_prev, w_next = _compute_crossfade_weights(length_cf, curve=curve)
            cross = prev_slice * w_prev + next_slice * w_next
            out_wave_local[start_sample_cf:end_sample_cf] = cross

            if verbose:
                dur_cf = length_cf / float(sr)
                print(
                    f"Boundary between seg {seg_l['index']:02d} "
                    f"and {seg_r['index']:02d}: "
                    f"crossfade [{xfade_start_s:.3f}, {xfade_end_s:.3f}]s "
                    f"({dur_cf * 1000.0:.1f} ms, {curve} curve)"
                )

        if out_wave_local.size == 0:
            raise RuntimeError("Output wave is empty after stitching.")

        out_wave_local = _peak_normalize(out_wave_local, target_dbfs=-1.0)
        out_wave_local = np.clip(out_wave_local, -1.0, 1.0).astype(np.float32)
        return out_wave_local

    output_paths = _derive_output_paths(out_path, len(take_plans))

    if verbose:
        print(f"Stitching phrase: {phrase}")
        print(f"Sample rate: {sr} Hz")
        print(f"Segments: {num_segs}")
        print(f"Crossfade coeff: {fade_fraction} (min 30 ms, max 500 ms)")
        print(f"Alternatives requested: {requested_alts}, produced: {len(take_plans)}")
        print("")

    written_paths = []
    for idx, plan in enumerate(take_plans):
        rendered = _render_from_take_plan(plan)
        path = output_paths[idx]
        path.parent.mkdir(parents=True, exist_ok=True)
        sf.write(path.as_posix(), rendered, sr)
        written_paths.append(path)

        if verbose:
            dur_out = rendered.size / float(sr)
            tag = "primary" if idx == 0 else f"alt{idx}"
            print(f"\nWrote {tag} comped audio to: {path}")
            print(
                f"Final duration: {dur_out:.3f} s "
                f"(compmap end: {phrase_end_s:.3f} s)"
            )

    return written_paths


def main():
    # Prompt for compmap JSON path
    print(
        "Enter path to compmap JSON "
        "(e.g., outputs/scoring-01-02-50/compmap-01-02-50.json):"
    )
    while True:
        compmap_path = input("> ").strip()
        if compmap_path:
            break
        print("Please enter a non-empty path.")

    # Prompt for output WAV path
    print("Enter output WAV path (e.g., comped/comped-01-02-50.wav):")
    while True:
        out_path = input("> ").strip()
        if out_path:
            break
        print("Please enter a non-empty path.")

    # Prompt for crossfade coefficient
    print("Enter crossfade coefficient (fraction of shorter segment, e.g., 0.15):")
    while True:
        raw = input("> ").strip()
        try:
            fade_fraction = float(raw)
            if fade_fraction >= 0.0:
                break
        except ValueError:
            pass
        print("Please enter a non-negative number, e.g. 0.15")

    stitch_from_compmap(
        compmap_path=compmap_path,
        out_path=out_path,
        fade_fraction=fade_fraction,
        base_override=None,
        verbose=False,
        xfade_curve="linear",  # x fade curve
    )


if __name__ == "__main__":
    main()
