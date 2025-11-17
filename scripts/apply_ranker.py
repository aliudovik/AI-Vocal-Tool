import argparse, os
import pandas as pd
import numpy as np
from joblib import load

# Same list and directions used in training, but now as single-take features.
FEATURES = [
    ("f0_rmse_c", -1.0),
    ("snr_db", +1.0),
    ("deess_ratio", -1.0),
    ("clip_n", -1.0),
    ("voiced_ratio", +1.0),
    ("mean_periodicity", +1.0),
]

def take_vector(row):
    # Direction-aware features so higher is better for all components
    vec = []
    for name, sgn in FEATURES:
        vec.append(float(sgn * row[name]))
    return np.array(vec, dtype=np.float32)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--features_csv", default="outputs/features.csv")
    ap.add_argument("--model", default="models/ranker_sklearn.joblib")
    ap.add_argument("--out_csv", default="outputs/scores_learned.csv")
    ap.add_argument("--blend", type=float, default=0.7, help="weight for learned score vs 1-blend for acc_score")
    args = ap.parse_args()

    df = pd.read_csv(args.features_csv)
    model = load(args.model)

    # StandardScaler in the pipeline expects pairwise-delta scale,
    # but for a quick MVP, we can still use the linear weights via relative ranking:
    # Trick: compute "pseudo-delta" against zero; the scaler will center/scale each feature dimension.
    X = np.stack([take_vector(r) for _, r in df.iterrows()], axis=0)
    # Use the pipeline's final estimator decision_function if available; else predict_proba
    if hasattr(model[-1], "decision_function"):
        # scikit pipeline: transform then decision
        learned = model.decision_function(X)
    else:
        learned = model.predict_proba(X)[:, 1]

    # Min-max normalize learned per phrase (keeps ranks but 0..1 range)
    df["learned_raw"] = learned
    df["learned_norm"] = df.groupby("phrase")["learned_raw"].transform(
        lambda s: (s - s.min()) / (s.max() - s.min() + 1e-9)
    )

    # Blend with your heuristic accuracy (already 0..1-ish)
    w = float(args.blend)
    df["final_score"] = w * df["learned_norm"] + (1.0 - w) * df["acc_score"]

    # Sort best→worst per phrase and save
    out = df.sort_values(["phrase", "final_score"], ascending=[True, False])
    os.makedirs(os.path.dirname(args.out_csv), exist_ok=True)
    out.to_csv(args.out_csv, index=False)
    print(out.groupby("phrase")[["take","final_score"]].head(3))

if __name__ == "__main__":
    main()
