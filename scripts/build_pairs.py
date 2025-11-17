import argparse, os
import pandas as pd
from itertools import combinations

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--features_csv", default="outputs/features.csv")
    ap.add_argument("--out_csv", default="outputs/pairs.csv")
    ap.add_argument("--delta", type=float, default=0.08, help="min |accA-accB| for auto-label")
    args = ap.parse_args()

    df = pd.read_csv(args.features_csv)
    need = {"phrase","take","acc_score","f0_rmse_c","snr_db","deess_ratio","clip_n","voiced_ratio","mean_periodicity"}
    missing = need - set(df.columns)
    if missing:
        raise SystemExit(f"features.csv missing columns: {missing}")

    rows = []
    for phrase, g in df.groupby("phrase"):
        takes = g.to_dict("records")
        if len(takes) < 2:
            continue
        for a, b in combinations(takes, 2):
            accA, accB = a["acc_score"], b["acc_score"]
            d = abs(accA - accB)
            auto = d >= args.delta
            label = 1 if accA > accB else 0  # provisional label by heuristic
            rows.append({
                "phrase": phrase,
                "take_a": a["take"], "take_b": b["take"],
                "label": label,
                "is_auto": int(auto),
                "delta": d,
                # stash features
                "f0_rmse_c_a": a["f0_rmse_c"], "snr_db_a": a["snr_db"], "deess_ratio_a": a["deess_ratio"],
                "clip_n_a": a["clip_n"], "voiced_ratio_a": a["voiced_ratio"], "mean_periodicity_a": a["mean_periodicity"],
                "f0_rmse_c_b": b["f0_rmse_c"], "snr_db_b": b["snr_db"], "deess_ratio_b": b["deess_ratio"],
                "clip_n_b": b["clip_n"], "voiced_ratio_b": b["voiced_ratio"], "mean_periodicity_b": b["mean_periodicity"],
            })
    out = pd.DataFrame(rows)
    os.makedirs(os.path.dirname(args.out_csv), exist_ok=True)
    out.to_csv(args.out_csv, index=False)
    print(f"Wrote {len(out)} pairs to {args.out_csv} "
          f"(auto-labeled: {out['is_auto'].sum()}, ambiguous: {(out['is_auto']==0).sum()})")

if __name__ == "__main__":
    main()
