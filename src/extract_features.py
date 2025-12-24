# src/extract_features.py
# Phrase-level + segment-level feature extraction and comp map generation.
#
# Pipeline:
#   1) For a selected phrase (e.g. "singer01/phrase02"), and given alpha% + BPM:
#       - Load all takes.
#       - Compute whole-phrase Accuracy/Emotion scores per take.
#       - Pick the best overall take as reference.
#   2) Segment the reference take into sub-phrase chunks.
#   3) For each take and each segment:
#       - Compute Accuracy/Emotion features on that segment.
#       - Compute final blended score.
#   4) Write:
#       - features-<singer>-<phrase>-<alpha>.csv     (whole-take scores)
#       - segments-<singer>-<phrase>-<alpha>.csv     (per-segment per-take scores)
#       - compmap-<singer>-<phrase>-<alpha>.json    (winner + candidates per segment)

import argparse
import glob
import json
import os
import re
from pathlib import Path

import numpy as np
import pandas as pds
import yaml


from src.io import load_wav
from src.segmentation import one_phrase, segment_phrase_reference
from src.features import (
    f0_crepe_16k,
    pitch_rmse_vs_median,
    snr_simple,
    deesser_ratio,
    clip_count,
    voiced_ratio,
    mean_periodicity,
    vibrato_stability,
    dyn_shape,
    microtiming,
    vibrato_analysis,
    microtiming_analysis,
)
from src.scoring import norm_block, accuracy_score, emotion_score, final_blend

PROJECT_ROOT = Path(__file__).resolve().parent.parent


def parse_selection(sel):
    """
    Parse a selection string like:
      - "singer01/phrase02"    -> ("singer01/phrase02", "01",   "02")
      - "singer_user/phrase01" -> ("singer_user/phrase01", "user", "01")

    Returns (rel_path, singer_id, phrase_num) where:
      - rel_path is the normalized relative path (slashes, no leading/trailing slash)
      - singer_id is either a zero-padded numeric ID ("01") or a string label ("user")
      - phrase_num is a zero-padded 2-digit phrase index ("01", "02", ...)
    """
    # Normalized relative path
    rel = sel.replace("\\", "/").strip("/")

    # Phrase number (always numeric, zero-padded to 2)
    n = re.search(r"phrase\s*0*([0-9]+)", sel, flags=re.IGNORECASE)
    phrase_num = n.group(1).zfill(2) if n else "yy"

    # Singer: numeric or label (e.g. "user")
    m_num = re.search(r"singer\s*0*([0-9]+)", sel, flags=re.IGNORECASE)
    m_label = re.search(r"singer[_\s]*([A-Za-z]+)", sel, flags=re.IGNORECASE)

    if m_num:
        # e.g. "singer01" -> "01"
        singer_id = m_num.group(1).zfill(2)
    elif m_label:
        # e.g. "singer_user" -> "user"
        singer_id = m_label.group(1).lower()
    else:
        singer_id = "xx"

    return rel, singer_id, phrase_num



def prompt_if_missing(args):
    """
    If --select, --alpha_pct, or --bpm are not provided, prompt in the console.
    Returns (select_str, alpha_pct_int, bpm_float).
    """
    # Phrase selection
    sel = args.select
    if not sel:
        print("Enter the phrase to process (e.g., singer01/phrase02):")
        while True:
            sel = input("> ").strip()
            if sel:
                break
            print("Please enter a non-empty value like 'singer01/phrase02'.")

    # Accuracy % (0..100)
    alpha_pct = args.alpha_pct
    if alpha_pct is None:
        print("Enter Accuracy percentage (0..100). Emotion will be 100 - Accuracy:")
        while True:
            raw = input("> ").strip()
            try:
                val = int(raw)
                if 0 <= val <= 100:
                    alpha_pct = val
                    break
            except ValueError:
                pass
            print("Please enter an integer between 0 and 100 (e.g., 60).")

    # BPM (positive float)
    bpm = args.bpm
    if bpm is None:
        print("Enter BPM for this phrase (e.g., 90):")
        while True:
            raw = input("> ").strip()
            try:
                val = float(raw)
                if val > 0:
                    bpm = val
                    break
            except ValueError:
                pass
            print("Please enter a positive number for BPM (e.g., 90).")

    return sel, alpha_pct, bpm


def _load_cfg(cfg_path_str):
    """Load YAML config, resolving relative path if needed."""
    cfg_path = Path(cfg_path_str)
    if not cfg_path.exists():
        candidate = Path(__file__).resolve().parent.parent / cfg_path_str
        if candidate.exists():
            cfg_path = candidate
    cfg = yaml.safe_load(open(cfg_path, "r"))
    return cfg


def _map_f0_to_times(f0, y_len, sr):
    """
    Approximate time stamp per F0 frame by evenly spreading frames
    over the phrase duration. This avoids hard-coding CREPE hop size.
    """
    if f0 is None or len(f0) == 0:
        return np.zeros(0, dtype=np.float32)
    dur = float(y_len) / float(sr)
    n = len(f0)
    # frames from [0, dur) with equal spacing
    return np.linspace(0.0, dur, num=n, endpoint=False, dtype=np.float32)

def run_feature_extraction(
    base,
    select,
    alpha_pct,
    bpm,
    cfg_path="configs/weights.yaml",
    out_dir="outputs",
    debug_emotion=False,
    explicit_compmap_path=None,
):

    """
    Programmatic entry point for feature extraction + comp map generation.

    This encapsulates the logic that was previously in main(), but without
    any interactive prompts or argparse dependency.

    Args:
        base (str or Path):
            Base folder containing singer/phrase dirs (e.g. "data_pilot").
        select (str):
            Phrase selection string, e.g. "singer01/phrase02".
        alpha_pct (int):
            Accuracy weight in percent; Emotion gets the remainder.
        bpm (float):
            Tempo in BPM.
        cfg_path (str or Path):
            YAML configuration path.
        out_dir (str or Path):
            Root folder to write CSV/JSON outputs into.
        debug_emotion (bool):
            If True, prints detailed vibrato/microtiming stats per segment.

    Returns:
        pathlib.Path: Path to the generated compmap-*.json file.

    explicit_compmap_path (str or Path, optional):
        If given, compmap JSON will be written exactly to this path
        instead of the default scoring-*/compmap-*.json.
    """
    base_str = str(base)
    out_dir_str = str(out_dir)
    cfg_path_str = str(cfg_path)

    # Config
    cfg = _load_cfg(cfg_path_str)
    sr_proc = int(cfg.get("sample_rate", 48000))
    sr_f0 = int(cfg.get("f0_sr", 16000))
    alpha = float(alpha_pct) / 100.0

    # Optional custom weights from YAML
    weights_cfg = cfg.get("weights", {}) or {}
    weights_acc = weights_cfg.get("accuracy", None)
    weights_emo = weights_cfg.get("emotion", None)

    diversity_delta = float(cfg.get("diversity_delta", 0.07))
    top_k = int(cfg.get("top_k", 3))

    # Selection (phrase directory)
    select_str = str(select)
    rel, singer_id, phrase_num = parse_selection(select_str)

    # Resolve base dir relative to project root if needed
    base_dir = Path(base_str)
    if not base_dir.is_absolute():
        base_dir = PROJECT_ROOT / base_dir

    phrase_dir = base_dir / rel
    if not phrase_dir.is_dir():
        raise FileNotFoundError(f"Phrase directory not found: {phrase_dir}")

    # Collect all .wav files in that phrase directory
    wavs = sorted(
        [
            p.as_posix()
            for p in phrase_dir.glob("*.wav")
            if p.suffix.lower() == ".wav"
        ]
    )
    if not wavs:
        raise FileNotFoundError(f"No .wav files in {phrase_dir}")

    phrase_name = rel
    alpha_int = int(round(alpha * 100))

    # Root outputs folder (e.g. <project_root>/outputs)
    out_root = Path(out_dir_str)
    if not out_root.is_absolute():
        out_root = PROJECT_ROOT / out_root

    # Phrase/run-specific scoring folder, e.g. outputs/scoring-01-02-60
    scoring_dir = out_root / f"scoring-{singer_id}-{phrase_num}-{alpha_int}"
    scoring_dir.mkdir(parents=True, exist_ok=True)

    # ------------------------------------------------------------------
    # PASS 1: Whole-phrase scoring for each take (to pick ref take).
    # ------------------------------------------------------------------
    takes = []
    global_rows = []

    for wav in wavs:
        take_id = os.path.splitext(os.path.basename(wav))[0]

        # Load audio: y @ sr_proc (48k), y_f0 @ sr_f0 (16k)
        y, sr, y_f016, sr_f0_actual = load_wav(wav, target_sr=sr_proc, f0_sr=sr_f0)

        # Single phrase per file
        (s0, e0) = one_phrase(y, sr)[0]
        yph = y[int(s0 * sr): int(e0 * sr)]

        # F0 + periodicity on the 16k signal (whole phrase)
        f0, periodicity = f0_crepe_16k(y_f016, sr16=sr_f0_actual, mask_thresh=0.5)

        # Accuracy-side features
        row = {
            "phrase": phrase_name,
            "take": take_id,
            "start_s": s0,
            "end_s": e0,
            "f0_rmse_c": pitch_rmse_vs_median(f0, periodicity),
            "voiced_ratio": voiced_ratio(periodicity),
            "mean_periodicity": mean_periodicity(periodicity),
            "snr_db": snr_simple(yph),
            "deess_ratio": deesser_ratio(yph, sr),
            "clip_n": clip_count(yph),
        }

        n = norm_block(row)
        row["acc_score"] = accuracy_score(n, weights=weights_acc)

        # Emotion-side features (whole phrase)
        row["vibrato_stability"] = vibrato_stability(
            f0, periodicity, sr16=sr_f0_actual, hop=256, pd_thresh=0.6
        )
        row["dyn_shape"] = dyn_shape(yph, sr)
        row["microtiming"] = microtiming(yph, sr)
        row["emo_score"] = emotion_score(row, weights=weights_emo)

        row["alpha"] = alpha
        row["final_score"] = final_blend(row["acc_score"], row["emo_score"], alpha)

        global_rows.append(row)

        # Store data needed for pass 2
        takes.append(
            {
                "take_id": take_id,
                "wav_path": wav,
                "y": y,
                "sr": sr,
                "y_f0": y_f016,
                "sr_f0": sr_f0_actual,
                "f0": f0,
                "pd": periodicity,
            }
        )

    if not global_rows:
        raise RuntimeError("No takes processed in pass 1—check your input files.")

    # Save whole-phrase ranking
    df_whole = pds.DataFrame(global_rows).sort_values("final_score", ascending=False)
    out_csv_whole = scoring_dir / f"features-{singer_id}-{phrase_num}-{alpha_int}.csv"
    df_whole.to_csv(out_csv_whole, index=False)

    print(f"\n[PASS 1] Whole-phrase scores written to {out_csv_whole}")
    print(df_whole[["phrase", "take", "acc_score", "emo_score", "final_score"]].head(10))

    # ------------------------------------------------------------------
    # PASS 2: Segment reference (best take) and then score each segment.
    # ------------------------------------------------------------------

    # Pick best overall take as reference for segmentation
    best_idx = int(np.argmax([r["final_score"] for r in global_rows]))
    ref_take = takes[best_idx]
    ref_id = ref_take["take_id"]

    print(f"\n[PASS 2] Using take '{ref_id}' as reference for segmentation.")

    ref_y = ref_take["y"]
    ref_sr = ref_take["sr"]

    # Segment the reference take
    segments = segment_phrase_reference(ref_y, ref_sr, bpm=bpm)
    print(f"[PASS 2] Detected {len(segments)} segments in reference take.")

    # Segment-level rows
    seg_rows = []

    for take, glob_row in zip(takes, global_rows):
        take_id = take["take_id"]
        y = take["y"]
        sr = take["sr"]
        f0 = take["f0"]
        pd = take["pd"]

        dur = len(y) / float(sr)
        f0_times = _map_f0_to_times(f0, len(y), sr)

        for seg_idx, (s, e) in enumerate(segments):
            # Safety clamp to phrase duration
            s_clamp = max(0.0, min(float(s), dur))
            e_clamp = max(0.0, min(float(e), dur))
            if e_clamp <= s_clamp:
                continue

            y_seg = y[int(s_clamp * sr): int(e_clamp * sr)]
            if len(y_seg) <= 0:
                continue

            # F0/Pd inside this segment
            if len(f0_times) > 0:
                mask = (f0_times >= s_clamp) & (f0_times <= e_clamp)
                f0_seg = f0[mask]
                pd_seg = pd[mask]
            else:
                f0_seg = np.asarray([], dtype=np.float32)
                pd_seg = np.asarray([], dtype=np.float32)

            # Accuracy features on segment
            row = {
                "phrase": phrase_name,
                "take": take_id,
                "segment_idx": seg_idx,
                "seg_start_s": s_clamp,
                "seg_end_s": e_clamp,
                "f0_rmse_c": pitch_rmse_vs_median(f0_seg, pd_seg),
                "voiced_ratio": voiced_ratio(pd_seg),
                "mean_periodicity": mean_periodicity(pd_seg),
                "snr_db": snr_simple(y_seg),
                "deess_ratio": deesser_ratio(y_seg, sr),
                "clip_n": clip_count(y_seg),
            }

            n = norm_block(row)
            row["acc_score"] = accuracy_score(n, weights=weights_acc)

            # Emotion features on segment
            row["vibrato_stability"] = vibrato_stability(
                f0_seg, pd_seg, sr16=take["sr_f0"], hop=256, pd_thresh=0.6
            )
            row["dyn_shape"] = dyn_shape(y_seg, sr)
            row["microtiming"] = microtiming(y_seg, sr)
            row["emo_score"] = emotion_score(row, weights=weights_emo)

            row["alpha"] = alpha
            row["final_score"] = final_blend(row["acc_score"], row["emo_score"], alpha)

            seg_rows.append(row)

            if debug_emotion:
                vib_dbg = vibrato_analysis(
                    f0_seg, pd_seg, sr16=take["sr_f0"], hop=256, pd_thresh=0.6
                )
                mt_dbg = microtiming_analysis(y_seg, sr)
                print(f"[DEBUG] take={take_id}, seg_idx={seg_idx}")
                print(
                    f"  Vibrato: frames_voiced={vib_dbg['frames_voiced']}, "
                    f"peak_hz={vib_dbg['peak_hz']:.2f}, depth_cents={vib_dbg['depth_cents']:.1f}, "
                    f"rate_score={vib_dbg['rate_score']:.2f}, depth_score={vib_dbg['depth_score']:.2f}, "
                    f"vib_score={vib_dbg['vib_score']:.2f}"
                )
                print(
                    f"  Microtiming: tempo={mt_dbg['tempo']:.1f}, n_onsets={mt_dbg['n_onsets']}, "
                    f"n_beats={mt_dbg['n_beats']}, jitter_ms={mt_dbg['jitter_ms']}, "
                    f"jt_score={mt_dbg['jt_score']:.2f}, note={mt_dbg['note']}"
                )

    if not seg_rows:
        raise RuntimeError("No segment rows produced—check segmentation or audio files.")

    # Segment-level CSV
    df_seg = pds.DataFrame(seg_rows)
    df_seg = df_seg.sort_values(["segment_idx", "final_score"], ascending=[True, False])

    out_csv_segments = scoring_dir / f"segments-{singer_id}-{phrase_num}-{alpha_int}.csv"
    df_seg.to_csv(out_csv_segments, index=False)

    print(f"\n[PASS 2] Segment-level scores written to {out_csv_segments}")
    print(
        df_seg[
            ["segment_idx", "phrase", "take", "seg_start_s", "seg_end_s", "final_score"]
        ]
        .head(15)
        .to_string(index=False)
    )

    # ------------------------------------------------------------------
    # Build comp map JSON: winner + near-equal candidates per segment.
    # ------------------------------------------------------------------

    segments_summary = []
    for seg_idx in sorted(df_seg["segment_idx"].unique()):
        seg_df = df_seg[df_seg["segment_idx"] == seg_idx].sort_values(
            "final_score", ascending=False
        )
        if seg_df.empty:
            continue

        best = seg_df.iloc[0]
        best_score = float(best["final_score"])

        winner = {
            "take": str(best["take"]),
            "final_score": best_score,
            "acc_score": float(best["acc_score"]),
            "emo_score": float(best["emo_score"]),
            "snr_db": float(best["snr_db"]),
            "f0_rmse_c": float(best["f0_rmse_c"]),
        }

        # Alternative candidates close in quality
        candidates = []
        for i in range(1, len(seg_df)):
            row = seg_df.iloc[i]
            score = float(row["final_score"])
            if best_score - score <= diversity_delta and len(candidates) < (top_k - 1):
                candidates.append(
                    {
                        "take": str(row["take"]),
                        "final_score": score,
                        "acc_score": float(row["acc_score"]),
                        "emo_score": float(row["emo_score"]),
                        "snr_db": float(row["snr_db"]),
                        "f0_rmse_c": float(row["f0_rmse_c"]),
                    }
                )

        segments_summary.append(
            {
                "index": int(seg_idx),
                "start_s": float(best["seg_start_s"]),

                "end_s": float(best["seg_end_s"]),
                "winner": winner,
                "candidates": candidates,
            }
        )

    compmap = {
        "phrase": phrase_name,
        "alpha": alpha,
        "alpha_pct": int(round(alpha * 100)),
        "bpm": float(bpm),
        "base_dir": base_str,
        "relative_path": rel,
        "reference_take": ref_id,
        "segments": segments_summary,
    }

    # Decide final compmap path
    if explicit_compmap_path is not None:
        out_json = Path(explicit_compmap_path)
        if not out_json.is_absolute():
            out_json = PROJECT_ROOT / out_json
        out_json.parent.mkdir(parents=True, exist_ok=True)
    else:
        out_json = scoring_dir / f"compmap-{singer_id}-{phrase_num}-{alpha_int}.json"
        out_json.parent.mkdir(parents=True, exist_ok=True)

    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(compmap, f, indent=2)

    print(f"\n[COMP MAP] Written comp map JSON to {out_json}\n")

    return out_json


def main():
    ap = argparse.ArgumentParser(
        description=(
            "Extract features for a selected phrase, compute Accuracy/Emotion scores, "
            "segment the best take, and produce per-segment rankings + comp map. "
            "If --select/--alpha_pct/--bpm are omitted, you'll be prompted."
        )
    )
    ap.add_argument(
        "--base",
        default="data_pilot",
        help="Base folder containing singer/phrase directories (default: data_pilot)",
    )
    ap.add_argument(
        "--select",
        default=None,
        help='Phrase selection, e.g. "singer01/phrase02". If omitted, will prompt.',
    )
    ap.add_argument(
        "--alpha_pct",
        type=int,
        default=None,
        help="Accuracy weight in percent (Emotion gets the rest). If omitted, will prompt.",
    )
    ap.add_argument(
        "--bpm",
        type=float,
        default=None,
        help="Tempo in BPM for this phrase. If omitted, will prompt.",
    )
    ap.add_argument(
        "--cfg",
        default="configs/weights.yaml",
        help="YAML with sample_rate, f0_sr, weights, etc.",
    )
    ap.add_argument(
        "--out_dir",
        default="outputs",
        help="Folder to write CSV/JSON outputs into",
    )
    ap.add_argument(
        "--debug_emotion",
        action="store_true",
        help="Print vibrato/microtiming debug stats per segment",
    )
    args = ap.parse_args()

    # Interactive fallback (same behaviour as before)
    select_str, alpha_pct, bpm = prompt_if_missing(args)

    # Delegate to the programmatic helper
    run_feature_extraction(
        base=args.base,
        select=select_str,
        alpha_pct=alpha_pct,
        bpm=bpm,
        cfg_path=args.cfg,
        out_dir=args.out_dir,
        debug_emotion=args.debug_emotion,
    )



if __name__ == "__main__":
    main()
