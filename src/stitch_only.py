# src/stitch_only.py
#
# Rebuild ONLY the comped WAV from an existing compmap JSON.
# This is intended for JUCE manual crossfade editing:
#   - JUCE edits compmap["boundaries"][b]["xfadeStartSec"/"xfadeEndSec"]
#   - Then calls: python -m src.stitch_only --compmap_path ... --out_comped_path ...
#
# We preserve the compmap JSON (no rewrite). We only re-render the WAV.
#
# Stitching behavior:
# - If compmap has valid boundaries -> stitcher uses them.
# - Else -> stitcher computes defaults using fade_fraction.
#
# Atomic output:
# - Writes to a temp WAV in the same directory, then os.replace to final path.

import argparse
import json
import os
import sys
from pathlib import Path

from src.stitch_from_compmap import stitch_from_compmap


def _read_fade_fraction_from_compmap(compmap_path: str) -> float | None:
    """
    Try to read a default fade_fraction stored in the compmap.
    Returns None if not present.
    """
    try:
        p = Path(compmap_path)
        with p.open("r", encoding="utf-8") as f:
            compmap = json.load(f)

        # Your C++ loader already checks this path:
        #   root["xfade_defaults"]["fade_fraction"]
        xfade_defaults = compmap.get("xfade_defaults", None)
        if isinstance(xfade_defaults, dict):
            ff = xfade_defaults.get("fade_fraction", None)
            if ff is not None:
                return float(ff)

        # Optional: tolerate older layouts if you ever used these keys
        for k in ("fade_fraction", "crossfade_fraction"):
            if k in compmap:
                return float(compmap[k])

    except Exception:
        pass

    return None


def _atomic_out_path(final_out: Path) -> Path:
    """
    Produce a temp file path in the same directory for atomic replace.
    """
    return final_out.parent / f"{final_out.stem}.tmp{final_out.suffix}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Rebuild comped WAV from an existing compmap JSON (stitch-only).\n"
            "Uses explicit compmap boundaries if present, otherwise falls back to defaults."
        )
    )

    parser.add_argument(
        "--compmap_path",
        required=True,
        help="Path to compmap JSON (absolute or relative).",
    )
    parser.add_argument(
        "--out_comped_path",
        required=True,
        help="Output WAV path to write (absolute or relative).",
    )
    parser.add_argument(
        "--base_override",
        default=None,
        help="Optional override for compmap['base_dir'] (e.g. 'data_pilot').",
    )
    parser.add_argument(
        "--fade_fraction",
        type=float,
        default=None,
        help=(
            "Fallback fade_fraction to use ONLY if compmap has no valid boundaries. "
            "If omitted, tries compmap['xfade_defaults']['fade_fraction'], else defaults to 0.15."
        ),
    )
    parser.add_argument(
        "--xfade_curve",
        default="linear",
        choices=["linear", "equal_power", "equalpower", "equal-power", "ep", "lin"],
        help="Crossfade curve (passed through to stitcher).",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Reduce console output.",
    )
    parser.add_argument(
        "--no_atomic",
        action="store_true",
        help="Write directly to out_comped_path (not recommended).",
    )

    args = parser.parse_args(argv)

    compmap_path = str(args.compmap_path)
    out_path = Path(args.out_comped_path)

    # Resolve output path relative to current working dir (JUCE sets CWD to projectRoot)
    if not out_path.is_absolute():
        out_path = (Path.cwd() / out_path).resolve()

    # Determine fade_fraction for default fallback behavior
    ff = args.fade_fraction
    if ff is None:
        ff_from_map = _read_fade_fraction_from_compmap(compmap_path)
        ff = ff_from_map if ff_from_map is not None else 0.15

    verbose = not args.quiet

    try:
        out_path.parent.mkdir(parents=True, exist_ok=True)

        if args.no_atomic:
            # Direct write
            stitch_from_compmap(
                compmap_path=compmap_path,
                out_path=out_path,
                fade_fraction=float(ff),
                base_override=args.base_override,
                verbose=verbose,
                xfade_curve=args.xfade_curve,
            )
        else:
            # Atomic write: render to tmp then replace
            tmp_path = _atomic_out_path(out_path)
            if tmp_path.exists():
                try:
                    tmp_path.unlink()
                except Exception:
                    pass

            stitch_from_compmap(
                compmap_path=compmap_path,
                out_path=tmp_path,
                fade_fraction=float(ff),
                base_override=args.base_override,
                verbose=verbose,
                xfade_curve=args.xfade_curve,
            )

            # Replace final output atomically
            os.replace(tmp_path.as_posix(), out_path.as_posix())

        if verbose:
            print(f"stitch_only: wrote {out_path}")

        return 0

    except Exception as e:
        print("stitch_only: ERROR:", str(e), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
