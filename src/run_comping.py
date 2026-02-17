# src/run_comping.py
#
# High-level wrapper to:
#   1) Run feature extraction + compmap generation (non-interactive).
#   2) Stitch the comped audio from the generated compmap.
#
# Programmatic entry:
#   run_comping(base_dir, select, alpha_pct, bpm, fade_fraction,
#               key_mode="chromatic", key_root="C", key_tolerance_cents=65.0,
#               out_dir="outputs", cfg="configs/weights.yaml")
#
# CLI usage example:
#   python -m src.run_comping \
#       --base data_pilot \
#       --select singer_user/phrase01 \
#       --alpha_pct 60 \
#       --bpm 90 \
#       --fade_fraction 0.15 \
#       --key_mode major \
#       --key_root C#/Db \
#       --out_dir outputs

import argparse
import json
import os
import tempfile
import time
from pathlib import Path

from src.extract_features import (
    run_feature_extraction,
    parse_selection,
    PROJECT_ROOT,
)
from src.stitch_from_compmap import stitch_from_compmap

# ----------------------------------------------------------------------
# Logging to the same file as segmentation
# ----------------------------------------------------------------------

LOG_PATH = os.environ.get(
    "AIVOCAL_SEG_LOG",
    os.path.join(tempfile.gettempdir(), "aivocal_segmentation_debug.log"),
)

# Make sure env vars are set for segmentation too
os.environ["AIVOCAL_SEG_DEBUG"] = "1"
os.environ["AIVOCAL_SEG_TRACE"] = "1"
os.environ["AIVOCAL_SEG_TRACE_EVERY"] = "10"
os.environ["AIVOCAL_SEG_TIME_BUDGET"] = "2.0"
os.environ["AIVOCAL_SEG_LOG"] = LOG_PATH


def _plog(msg: str) -> None:
    try:
        with open(LOG_PATH, "a", encoding="utf-8") as f:
            f.write(f"[PIPE {time.strftime('%H:%M:%S')}] {msg}\n")
            f.flush()
    except Exception:
        pass


# Touch the log so you know path is correct
with open(LOG_PATH, "a", encoding="utf-8") as f:
    f.write("run_comping: started\n")


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
    use_ml=False,
    key_mode="chromatic",
    key_root="C",
    key_tolerance_cents=65.0,
):
    """
    Run the full comping pipeline:
      1) feature extraction + compmap json
      2) stitching into a final comped WAV.
    """
    base_str = str(base_dir)
    out_dir_str = str(out_dir)
    cfg_path = str(cfg)

    _plog("=== COMPING START ===")
    _plog(
        f"base={base_str} select={select} bpm={bpm} alpha={alpha_pct} "
        f"key_mode={key_mode} key_root={key_root} key_tol={key_tolerance_cents}"
    )

    # ------------------------------------------------------------------
    # 1) Feature extraction + compmap generation (no interactive prompts)
    # ------------------------------------------------------------------
    _plog("run_feature_extraction: START")

    t0 = time.time()
    compmap_path = run_feature_extraction(
        base=base_str,
        select=select,
        alpha_pct=alpha_pct,
        bpm=bpm,
        cfg_path=cfg_path,
        out_dir=out_dir_str,
        debug_emotion=False,
        explicit_compmap_path=out_compmap_path,
        fade_fraction=float(fade_fraction),
        use_ml=use_ml,
        key_mode=key_mode,
        key_root=key_root,
        key_tolerance_cents=float(key_tolerance_cents),
    )
    t1 = time.time()

    compmap_path = Path(compmap_path)
    _plog(
        f"run_feature_extraction: END elapsed={t1 - t0:.3f}s "
        f"compmap={compmap_path}"
    )

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
    _plog("stitch_from_compmap: START")
    t2 = time.time()

    stitched_paths = stitch_from_compmap(
        compmap_path=compmap_path,
        out_path=out_wav,
        fade_fraction=float(fade_fraction),
        base_override=base_str,
        verbose=True,
        num_alternatives=3,
    )

    t3 = time.time()
    stitched_str = ", ".join(str(p) for p in stitched_paths)
    _plog(f"stitch_from_compmap: END elapsed={t3 - t2:.3f}s out=[{stitched_str}]")
    _plog("=== COMPING END ===")

    return Path(stitched_paths[0]) if stitched_paths else out_wav


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
        "--key_mode",
        default="chromatic",
        help="Key mode: major, minor, or chromatic (default: chromatic).",
    )
    p.add_argument(
        "--key_root",
        default="C",
        help="Key root note (e.g. C, C#, Db).",
    )
    p.add_argument(
        "--key_tolerance_cents",
        type=float,
        default=65.0,
        help="Tolerance in cents for in-key frame decision (default: 65).",
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
    p.add_argument(
        "--use_ml",
        action="store_true",
        help="Use ML server for emotion scoring.",
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
        use_ml=args.use_ml,
        key_mode=args.key_mode,
        key_root=args.key_root,
        key_tolerance_cents=args.key_tolerance_cents,
    )
    print(f"\n[RUN COMPING] Wrote final comped file to: {out_path}\n")
