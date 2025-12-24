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

from src.io import load_wav

PROJECT_ROOT = Path(__file__).resolve().parent.parent


PER_TAKE_NORMALIZE_DBFS = -3.0 # we need per take normalization, different segment volume bugs otherwise....

def _load_compmap(path):
    """Load compmap JSON (resolve relative to project root if needed)."""
    p = Path(path)
    if not p.is_absolute():
        # Interpret relative paths from the project root
        p = PROJECT_ROOT / p
    with open(p, "r", encoding="utf-8") as f:
        return json.load(f)


def _load_winner_audio(compmap, base_override=None):
    """
    Load WAVs for all winner takes referenced in compmap["segments"].
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

    segments = compmap.get("segments", [])
    take_ids = {seg["winner"]["take"] for seg in segments}

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


def stitch_from_compmap(
    compmap_path,
    out_path,
    fade_fraction=0.15,
    base_override=None,
    verbose=True,
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
    """
    compmap = _load_compmap(compmap_path)
    phrase = compmap.get("phrase", "")
    segments = compmap.get("segments", [])

    if not segments:
        raise RuntimeError("Compmap has no segments.")

    sr, audio = _load_winner_audio(compmap, base_override=base_override)

    # Sort segments by time
    segments_sorted = sorted(segments, key=lambda s: s["start_s"])
    num_segs = len(segments_sorted)

    # Collect segment times and winners
    start_s = np.array([float(s["start_s"]) for s in segments_sorted], dtype=float)
    end_s = np.array([float(s["end_s"]) for s in segments_sorted], dtype=float)
    take_ids = [s["winner"]["take"] for s in segments_sorted]
    dur_s = end_s - start_s

    phrase_end_s = float(end_s.max())
    # Base on compmap end time; also ensure we don't go past the actual take length
    first_take = audio[take_ids[0]]
    max_samples_from_takes = first_take.size
    target_samples = int(round(phrase_end_s * sr))
    target_samples = min(target_samples, max_samples_from_takes)

    if verbose:
        print(f"Stitching phrase: {phrase}")
        print(f"Sample rate: {sr} Hz")
        print(f"Segments: {num_segs}")
        print(f"Crossfade coeff: {fade_fraction} (min 30 ms, max 500 ms)")
        print("")

    # Step 1: hard comp (no crossfades) on a fixed global timeline
    out_wave = np.zeros(target_samples, dtype=np.float32)

    for idx, seg in enumerate(segments_sorted):
        take_id = take_ids[idx]
        y = audio[take_id]

        start_sample = int(round(start_s[idx] * sr))
        end_sample = int(round(end_s[idx] * sr))

        # Clamp to valid range
        start_sample = max(0, min(start_sample, target_samples))
        end_sample = max(start_sample, min(end_sample, target_samples, y.size))

        if end_sample <= start_sample:
            if verbose:
                print(
                    f"Skipping empty segment idx={seg['index']} "
                    f"start={start_s[idx]:.3f} end={end_s[idx]:.3f}"
                )
            continue

        seg_wave = y[start_sample:end_sample]

        if verbose:
            dur_seg = (end_sample - start_sample) / float(sr)
            print(
                f"Segment {seg['index']:02d}: {start_s[idx]:7.3f}s -> {end_s[idx]:7.3f}s "
                f"({dur_seg:5.3f}s) from {take_id}"
            )

        out_wave[start_sample:end_sample] = seg_wave.astype(np.float32)

    # Step 2: apply time-aligned crossfades around boundaries
    # We work per-boundary, mixing the two relevant takes in a window
    # centered on the boundary, without changing the overall timeline length.
    min_fade_s = 0.030  # 30 ms
    max_fade_s = 0.500  # 500 ms

    for b in range(num_segs - 1):
        # Boundary time in seconds
        boundary_s = end_s[b]
        # Adjacent segment durations
        d1 = dur_s[b]
        d2 = dur_s[b + 1]
        base_dur_s = min(d1, d2)

        if base_dur_s <= 0.0 or fade_fraction <= 0.0:
            continue

        # Desired fade duration
        fade_s_desired = base_dur_s * fade_fraction
        # Clamp: at least 30 ms, at most 500 ms, and not longer than d1 + d2
        fade_s = max(min_fade_s, fade_s_desired)
        fade_s = min(fade_s, max_fade_s, d1 + d2)

        if fade_s <= 0.0:
            continue

        half_s = fade_s / 2.0

        # Crossfade region [boundary - half, boundary + half]
        start_s_cf = boundary_s - half_s
        end_s_cf = boundary_s + half_s

        start_sample_cf = int(round(start_s_cf * sr))
        end_sample_cf = int(round(end_s_cf * sr))

        # Clamp to buffer
        start_sample_cf = max(0, min(start_sample_cf, target_samples))
        end_sample_cf = max(start_sample_cf, min(end_sample_cf, target_samples))

        length_cf = end_sample_cf - start_sample_cf
        if length_cf <= 1:
            continue

        prev_take_id = take_ids[b]
        next_take_id = take_ids[b + 1]

        y_prev = audio[prev_take_id]
        y_next = audio[next_take_id]

        # Clamp to available samples in each take
        end_sample_cf_prev = min(end_sample_cf, y_prev.size)
        end_sample_cf_next = min(end_sample_cf, y_next.size)
        # Adjust length if needed (should be the same for both)
        length_cf = min(
            length_cf,
            end_sample_cf_prev - start_sample_cf,
            end_sample_cf_next - start_sample_cf,
        )
        if length_cf <= 1:
            continue

        end_sample_cf = start_sample_cf + length_cf

        prev_slice = y_prev[start_sample_cf:end_sample_cf].astype(np.float32)
        next_slice = y_next[start_sample_cf:end_sample_cf].astype(np.float32)

        # Linear crossfade weights across the region
        t = np.linspace(0.0, 1.0, length_cf, dtype=np.float32)
        w_next = t          # 0 -> 1
        w_prev = 1.0 - t    # 1 -> 0

        cross = prev_slice * w_prev + next_slice * w_next

        # Overwrite the region in the output with the crossfaded mix
        out_wave[start_sample_cf:end_sample_cf] = cross

        if verbose:
            dur_cf = length_cf / float(sr)
            print(
                f"Boundary between seg {segments_sorted[b]['index']:02d} "
                f"and {segments_sorted[b+1]['index']:02d}: "
                f"crossfade {fade_s*1000.0:5.1f} ms "
                f"around {boundary_s:7.3f}s (actual {dur_cf*1000.0:5.1f} ms)"
            )

    if out_wave.size == 0:
        raise RuntimeError("Output wave is empty after stitching.")

    # NORMALIZE ANTHONYYYY!!!
    out_wave = _peak_normalize(out_wave, target_dbfs=-1.0)

    # Safety clip to [-1, 1] after normalization
    out_wave = np.clip(out_wave, -1.0, 1.0).astype(np.float32)

    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    # Actually write the file
    sf.write(out_path.as_posix(), out_wave, sr)

    if verbose:
        dur_out = out_wave.size / float(sr)
        print(f"\nWrote comped audio to: {out_path}")
        print(f"Final duration: {dur_out:.3f} s "
              f"(compmap end: {phrase_end_s:.3f} s)")


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
        verbose=True,
    )


if __name__ == "__main__":
    main()
