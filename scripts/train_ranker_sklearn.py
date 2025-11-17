import argparse, os
import pandas as pd
import numpy as np
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import make_pipeline
from joblib import dump

# Features and their "direction": +1 means higher=better, -1 means lower=better
FEATURES = [
    ("f0_rmse_c", -1.0),        # lower pitch RMSE is better
    ("snr_db", +1.0),           # higher SNR is better
    ("deess_ratio", -1.0),      # lower extreme harshness is better
    ("clip_n", -1.0),           # fewer clips is better
    ("voiced_ratio", +1.0),     # more voiced is better
    ("mean_periodicity", +1.0), # higher periodicity is better
]

def row_to_vec(row):
    """Build delta vector = sign * (A - B) so that positive means A is better."""
    vec = []
    for name, sgn in FEATURES:
        a = row[f"{name}_a"]
        b = row[f"{name}_b"]
        vec.append(float(sgn) * (float(a) - float(b)))
    return np.array(vec, dtype=np.float32)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pairs_csv", default="outputs/pairs.csv")
    ap.add_argument("--out_model", default="models/ranker_sklearn.joblib")
    args = ap.parse_args()

    pairs = pd.read_csv(args.pairs_csv)
    if pairs.empty:
        raise SystemExit("No pairs found. Increase takes or lower --delta in build_pairs.py")

    # Build design matrix
    X = np.stack([row_to_vec(r) for _, r in pairs.iterrows()], axis=0)
    y = pairs["label"].values.astype(np.int32)

    # If labels are single-class or heavily imbalanced, add flipped copies
    uniq, counts = np.unique(y, return_counts=True)
    if len(uniq) < 2 or min(counts) / max(counts) < 0.2:
        X = np.vstack([X, -X])
        y = np.concatenate([y, 1 - y])
        flipped = True
    else:
        flipped = False

    # Pipeline: standardize -> logistic regression
    clf = make_pipeline(
        StandardScaler(with_mean=True),
        LogisticRegression(max_iter=1000, class_weight="balanced")  # robust on small/imbalanced sets
    )
    clf.fit(X, y)

    os.makedirs(os.path.dirname(args.out_model), exist_ok=True)
    dump(clf, args.out_model)

    acc = clf.score(X, y)
    print(
        f"Saved model to {args.out_model} | "
        f"train acc: {acc:.3f} | n_pairs_used={len(y)} "
        f"| flipped_aug={'yes' if flipped else 'no'}"
    )

if __name__ == "__main__":
    main()
