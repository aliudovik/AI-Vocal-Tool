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
#       --out outputs/comped-01-02-50.wav \
#       --crossfade_ms 25

import argparse
import json
import os
from pathlib import Path

import numpy as np
import soundfile as sf

from src.io import load_wav

PROJECT_ROOT = Path(__file__).resolve().parent.parent


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

    if sr_ref is None:
        raise RuntimeError("No takes loaded from compmap; segments empty?")

    return sr_ref, audio


def _crossfade_concat(a, b, sr, crossfade_ms):
    """
    Concatenate arrays a and b with a linear crossfade of length crossfade_ms.
    Returns a new array (float32).
    """
    if a.size == 0:
        return b.astype(np.float32)
    if b.size == 0:
        return a.astype(np.float32)

    if crossfade_ms <= 0.0:
        # Simple butt splice, no crossfade
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


def stitch_from_compmap(
    compmap_path,
    out_path,
    crossfade_ms=25.0,
    base_override=None,
    verbose=True,
):
    """
    Main stitching routine.

    Args:
      compmap_path (str): path to compmap-*.json file.
      out_path (str): output WAV path.
      crossfade_ms (float): crossfade duration in ms (each side).
      base_override (str or None): override compmap["base_dir"] if provided.
      verbose (bool): print what is being stitched.
    """
    compmap = _load_compmap(compmap_path)
    phrase = compmap.get("phrase", "")
    segments = compmap.get("segments", [])

    if not segments:
        raise RuntimeError("Compmap has no segments.")

    sr, audio = _load_winner_audio(compmap, base_override=base_override)

    if verbose:
        print(f"Stitching phrase: {phrase}")
        print(f"Sample rate: {sr} Hz")
        print(f"Segments: {len(segments)}")
        print(f"Crossfade: {crossfade_ms} ms")
        print("")

    # Ensure segments are in time order
    segments_sorted = sorted(segments, key=lambda s: s["start_s"])

    out_wave = np.zeros(0, dtype=np.float32)

    for seg in segments_sorted:
        idx = seg["index"]
        start_s = float(seg["start_s"])
        end_s = float(seg["end_s"])
        take_id = seg["winner"]["take"]

        y = audio[take_id]
        start_sample = int(round(start_s * sr))
        end_sample = int(round(end_s * sr))
        start_sample = max(0, min(start_sample, y.size))
        end_sample = max(0, min(end_sample, y.size))

        if end_sample <= start_sample:
            if verbose:
                print(
                    f"Skipping empty segment idx={idx} start={start_s:.3f} end={end_s:.3f}"
                )
            continue

        seg_wave = y[start_sample:end_sample]

        if verbose:
            dur_seg = (end_sample - start_sample) / float(sr)
            print(
                f"Segment {idx:02d}: {start_s:7.3f}s -> {end_s:7.3f}s "
                f"({dur_seg:5.3f}s) from {take_id}"
            )

        out_wave = _crossfade_concat(out_wave, seg_wave, sr, crossfade_ms)

    if out_wave.size == 0:
        raise RuntimeError("Output wave is empty after stitching.")

    # Clip to [-1, 1] just in case
    out_wave = np.clip(out_wave, -1.0, 1.0).astype(np.float32)

    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    sf.write(out_path.as_posix(), out_wave, sr)

    if verbose:
        dur_out = out_wave.size / float(sr)
        print(f"\nWrote comped audio to: {out_path}")
        print(f"Final duration: {dur_out:.3f} s")


def main():
    # Prompt for compmap JSON path
    print("Enter path to compmap JSON (e.g., outputs/scoring-01-02-50/compmap-01-02-50.json):")
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

    # Prompt for crossfade length
    print("Enter crossfade duration in ms (e.g., 25):")
    while True:
        raw = input("> ").strip()
        try:
            crossfade_ms = float(raw)
            if crossfade_ms >= 0.0:
                break
        except ValueError:
            pass
        print("Please enter a non-negative number, e.g. 25")

    stitch_from_compmap(
        compmap_path=compmap_path,
        out_path=out_path,
        crossfade_ms=crossfade_ms,
        base_override=None,
        verbose=True,
    )



if __name__ == "__main__":
    main()
