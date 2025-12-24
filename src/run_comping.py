# src/run_comping.py
#
# High-level wrapper to:
#   1) Run feature extraction + compmap generation (non-interactive).
#   2) Stitch the comped audio from the generated compmap.
#
# Programmatic entry:
#   run_comping(base_dir, select, alpha_pct, bpm, fade_fraction,
#               out_dir="outputs", cfg="configs/weights.yaml")
#
# CLI usage example:
#   python -m src.run_comping \
#       --base data_pilot \
#       --select singer_user/phrase01 \
#       --alpha_pct 60 \
#       --bpm 90 \
#       --fade_fraction 0.15 \
#       --out_dir outputs

import argparse
import json
from pathlib import Path

from src.extract_features import (
    run_feature_extraction,
    parse_selection,
    PROJECT_ROOT,
)
from src.stitch_from_compmap import stitch_from_compmap


def run_comping(
    base_dir,
    select,
    alpha_pct,
    bpm,
    fade_fraction,
    out_dir="outputs",
    cfg="configs/weights.yaml",
    out_comped_path=None,
    out_compmap_path=None,
):
    """
    Run the full comping pipeline:
      1) feature extraction + compmap json
      2) stitching into a final comped WAV.

    Args:
        base_dir (str or Path):
            Base folder containing singer/phrase directories (e.g. "data_pilot").
        select (str):
            Phrase selection, e.g. "singer_user/phrase01".
        alpha_pct (int):
            Accuracy weight in percent (0..100). Emotion = 100 - alpha_pct.
        bpm (float):
            Tempo in BPM for this phrase.
        fade_fraction (float):
            Fraction of the shorter adjacent segment used to derive crossfade
            length. Passed through to stitch_from_compmap().
        out_dir (str or Path):
            Root outputs folder (same as in extract_features.py). Defaults to "outputs".
        cfg (str or Path):
            YAML config with sample_rate, weights, etc. Defaults to "configs/weights.yaml".
        out_comped_path (str or Path, optional):
            If given, the final comped WAV will be written exactly here.
        out_compmap_path (str or Path, optional):
            If given, the compmap JSON will be written exactly here.
    Returns:
        pathlib.Path: Path to the final comped WAV file.
    """
    base_str = str(base_dir)
    out_dir_str = str(out_dir)
    cfg_path = str(cfg)

    # ------------------------------------------------------------------
    # 1) Feature extraction + compmap generation (no interactive prompts)
    # ------------------------------------------------------------------
    compmap_path = run_feature_extraction(
        base=base_str,
        select=select,
        alpha_pct=alpha_pct,
        bpm=bpm,
        cfg_path=cfg_path,
        out_dir=out_dir_str,
        debug_emotion=False,
        explicit_compmap_path=out_compmap_path,
    )
    compmap_path = Path(compmap_path)


    # ------------------------------------------------------------------
    # 2) Decide output WAV name using singer/phrase/alpha from compmap
    # ------------------------------------------------------------------
    with open(compmap_path, "r", encoding="utf-8") as f:
        compmap = json.load(f)

    # Use the same selection semantics as extract_features
    rel = compmap.get("relative_path", select)
    _, singer_id, phrase_num = parse_selection(rel)

    # Prefer alpha from compmap, fall back to alpha_pct argument if missing
    alpha_from_map = compmap.get("alpha", None)
    if alpha_from_map is not None:
        alpha_int = int(round(float(alpha_from_map) * 100))
    else:
        alpha_int = int(alpha_pct)

    # Decide comped WAV location
    if out_comped_path is not None:
        out_wav = Path(out_comped_path)
        if not out_wav.is_absolute():
            out_wav = PROJECT_ROOT / out_wav
    else:
        out_root = Path(out_dir_str)
        if not out_root.is_absolute():
            out_root = PROJECT_ROOT / out_root
        # Example fallback: outputs/comped-user-01-60.wav
        out_wav = out_root / f"comped-{singer_id}-{phrase_num}-{alpha_int}.wav"


    # ------------------------------------------------------------------
    # 3) Stitch using the generated compmap
    # ------------------------------------------------------------------
    stitch_from_compmap(
        compmap_path=compmap_path,
        out_path=out_wav,
        fade_fraction=float(fade_fraction),
        base_override=base_str,
        verbose=True,
    )

    return out_wav


# ----------------------------------------------------------------------
# CLI
# ----------------------------------------------------------------------

def _parse_args():
    p = argparse.ArgumentParser(
        description=(
            "Run full AI vocal comping pipeline: feature extraction + stitching.\n"
            "This is a non-interactive wrapper around src.extract_features "
            "and src.stitch_from_compmap."
        )
    )
    p.add_argument(
        "--base",
        default="data_pilot",
        help="Base folder containing singer/phrase directories (default: data_pilot).",
    )
    p.add_argument(
        "--select",
        required=True,
        help='Phrase selection, e.g. "singer_user/phrase01".',
    )
    p.add_argument(
        "--alpha_pct",
        required=True,
        type=int,
        help="Accuracy weight in percent (0..100). Emotion gets the rest.",
    )
    p.add_argument(
        "--bpm",
        required=True,
        type=float,
        help="Tempo in BPM for this phrase.",
    )
    p.add_argument(
        "--fade_fraction",
        type=float,
        default=0.15,
        help=(
            "Crossfade fraction relative to the shorter adjacent segment "
            "(default: 0.15)."
        ),
    )
    p.add_argument(
        "--out_dir",
        default="outputs",
        help="Root output folder (default: outputs).",
    )
    p.add_argument(
        "--cfg",
        default="configs/weights.yaml",
        help="YAML config path (default: configs/weights.yaml).",
    )
    p.add_argument(
        "--out_comped_path",
        default=None,
        help="Optional explicit path for the final comped WAV.",
    )
    p.add_argument(
        "--out_compmap_path",
        default=None,
        help="Optional explicit path for the compmap JSON.",
    )
    return p.parse_args()


if __name__ == "__main__":
    args = _parse_args()
    out_path = run_comping(
        base_dir=args.base,
        select=args.select,
        alpha_pct=args.alpha_pct,
        bpm=args.bpm,
        fade_fraction=args.fade_fraction,
        out_dir=args.out_dir,
        cfg=args.cfg,
        out_comped_path=args.out_comped_path,
        out_compmap_path=args.out_compmap_path,
    )
    print(f"\n[RUN COMPING] Wrote final comped file to: {out_path}\n")

