import argparse
import glob
import os
from pathlib import Path

import yaml
import pandas as pd

from src.io import load_wav
from src.segmentation import one_phrase
from src.features import (
    f0_crepe_16k,
    pitch_rmse_vs_median,
    snr_simple,
    deesser_ratio,
    clip_count,
    voiced_ratio,
    mean_periodicity,
)
from src.scoring import norm_block, accuracy_score


def main():
    # ---- args ----
    ap = argparse.ArgumentParser(description="Extract per-take features and rank by accuracy score.")
    ap.add_argument("--root", default="data_pilot/songA", help="Folder containing phrase subfolders or wavs")
    ap.add_argument("--cfg", default="configs/weights.yaml", help="YAML with sample_rate, f0_sr, ranges, weights")
    ap.add_argument("--out_csv", default="outputs/features.csv", help="Where to write the features table")
    args = ap.parse_args()

    # ---- config ----
    cfg_path = Path(args.cfg)
    if not cfg_path.exists():
        # Try resolving relative to project root if running from inside src/
        candidate = Path(__file__).resolve().parent.parent / args.cfg
        cfg_path = candidate if candidate.exists() else Path(args.cfg)
    cfg = yaml.safe_load(open(cfg_path, "r"))

    sr_proc = int(cfg.get("sample_rate", 48000))
    sr_f0 = int(cfg.get("f0_sr", 16000))

    rows = []

    # We expect: root/phraseXX/*.wav  (but also support loose wavs under root)
    phrase_entries = sorted(glob.glob(os.path.join(args.root, "*")))
    if not phrase_entries:
        raise FileNotFoundError(f"No files or folders found under {args.root}. Check your --root path.")

    for entry in phrase_entries:
        if os.path.isdir(entry):
            phrase_name = os.path.basename(entry)
            wavs = sorted(
                [p for p in glob.glob(os.path.join(entry, "*")) if os.path.splitext(p)[1].lower() == ".wav"]
            )
        else:
            # loose wav under root: treat the root name as the phrase
            if os.path.splitext(entry)[1].lower() != ".wav":
                continue
            phrase_name = os.path.basename(args.root)
            wavs = [entry]

        if not wavs:
            print(f"[warn] no .wav files in {entry}")
            continue

        for wav in wavs:
            take_id = os.path.splitext(os.path.basename(wav))[0]

            # Load audio: y @ sr_proc (48k), y_f0 @ sr_f0 (16k)
            y, sr, y_f0, sr_f0_actual = load_wav(wav, target_sr=sr_proc, f0_sr=sr_f0)

            # MVP: one phrase per file
            for (s, e) in one_phrase(y, sr):
                yph = y[int(s * sr) : int(e * sr)]

                # --- F0 + periodicity on the 16k signal (whole file for MVP) ---
                f0, periodicity = f0_crepe_16k(y_f0, sr16=sr_f0_actual, mask_thresh=0.5)

                # --- build feature row ---
                row = {
                    "phrase": phrase_name,
                    "take": take_id,
                    "start_s": s,
                    "end_s": e,
                    "f0_rmse_c": pitch_rmse_vs_median(f0, periodicity),
                    "voiced_ratio": voiced_ratio(periodicity),
                    "mean_periodicity": mean_periodicity(periodicity),
                    "snr_db": snr_simple(yph),
                    "deess_ratio": deesser_ratio(yph, sr),
                    "clip_n": clip_count(yph),
                }

                # normalize + score
                n = norm_block(row)
                row["acc_score"] = accuracy_score(n)
                rows.append(row)

    if not rows:
        raise RuntimeError("No rows produced—check your input paths / WAV placement.")

    import pandas as pd
    df = pd.DataFrame(rows).sort_values(["phrase", "acc_score"], ascending=[True, False])
    out_path = Path(args.out_csv)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(out_path, index=False)
    print(df.head(10))


if __name__ == "__main__":
    main()
