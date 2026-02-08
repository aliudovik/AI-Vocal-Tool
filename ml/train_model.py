import os, glob, joblib
import opensmile
import pandas as pd

from sklearn.ensemble import RandomForestClassifier, ExtraTreesClassifier
from sklearn.model_selection import GroupShuffleSplit
from sklearn.metrics import classification_report, roc_auc_score
from sklearn.svm import SVC

from imblearn.ensemble import BalancedRandomForestClassifier

# XGBoost (optional)
try:
    import xgboost as xgb
    HAS_XGB = True
except ImportError:
    HAS_XGB = False

DATA_ROOT = "data"
MODEL_DIR = "models"
RESULTS_CSV = os.path.join(MODEL_DIR, "model_results.csv")

smile = opensmile.Smile(
    feature_set=opensmile.FeatureSet.ComParE_2016,
    feature_level=opensmile.FeatureLevel.Functionals,
)

def extract_folder(folder, label):
    wavs = glob.glob(os.path.join(DATA_ROOT, folder, "**", "*.wav"), recursive=True)
    rows = []
    for w in wavs:
        try:
            df = smile.process_file(w)
            if df.empty:
                continue
            feats = df.iloc[0].tolist()
            base = os.path.basename(w)
            group = base.split("_part")[0]
            rows.append([*feats, label, group])
        except Exception as e:
            print("skip", w, e)
    return rows

def main():
    rows = []
    rows += extract_folder("emotion", 1)
    rows += extract_folder("bad", 0)
    rows += extract_folder("perfect", 0)

    if not rows:
        print("No data found")
        return

    X = [r[:-2] for r in rows]
    y = [r[-2] for r in rows]
    groups = [r[-1] for r in rows]

    splitter = GroupShuffleSplit(test_size=0.2, random_state=42)
    train_idx, test_idx = next(splitter.split(X, y, groups=groups))

    X_train = [X[i] for i in train_idx]
    y_train = [y[i] for i in train_idx]
    X_test  = [X[i] for i in test_idx]
    y_test  = [y[i] for i in test_idx]

    models = {
        "RandomForest": RandomForestClassifier(
            n_estimators=100, class_weight="balanced", random_state=42, max_depth=8,
        ),

        "BalancedRandomForest": BalancedRandomForestClassifier(
            n_estimators=100, random_state=42, max_depth=8,
        ),

        "ExtraTrees": ExtraTreesClassifier(
            n_estimators=100, class_weight="balanced", random_state=42, max_depth=8,
        ),

        "SVM_RBF": SVC(
            kernel="rbf", probability=True, class_weight="balanced", random_state=42, C=5.0, gamma=0.1
        )
    }

    if HAS_XGB:
        models["XGBoost"] = xgb.XGBClassifier(
            n_estimators=100,
            max_depth=5,
            learning_rate=0.1,
            subsample=0.7,
            colsample_bytree=0.8,
            eval_metric="logloss",
            scale_pos_weight=(len(y_train) / sum(y_train)) if sum(y_train) > 0 else 1,
            random_state=42,
        )
    else:
        print("⚠️ XGBoost not installed. Skipping XGB model.")

    os.makedirs(MODEL_DIR, exist_ok=True)

    results = []
    best_auc = -1
    best_model = None
    best_name = None

    for name, clf in models.items():
        print(f"\n=== Training {name} ===")
        clf.fit(X_train, y_train)

        preds = clf.predict(X_test)
        try:
            probas = clf.predict_proba(X_test)[:, 1]
        except:
            probas = preds

        report = classification_report(y_test, preds, output_dict=True)
        auc = roc_auc_score(y_test, probas)

        print(classification_report(y_test, preds))
        print("ROC-AUC:", auc)

        # Save each model
        model_path = os.path.join(MODEL_DIR, f"{name}.pkl")
        joblib.dump(clf, model_path)
        print(f"Saved {name} -> {model_path}")

        # Store metrics for CSV
        results.append({
            "model": name,
            "roc_auc": auc,
            "accuracy": report["accuracy"],
            "precision_emotion": report["1"]["precision"],
            "recall_emotion": report["1"]["recall"],
            "f1_emotion": report["1"]["f1-score"],
            "macro_f1": report["macro avg"]["f1-score"],
        })

        if auc > best_auc:
            best_auc = auc
            best_model = clf
            best_name = name

    # Save best model separately
    best_path = os.path.join(MODEL_DIR, "emotion_best.pkl")
    joblib.dump(best_model, best_path)

    # Save CSV log
    df = pd.DataFrame(results)
    df.to_csv(RESULTS_CSV, index=False)

    print(f"\n✅ Best model: {best_name} (AUC={best_auc:.3f})")
    print(f"Saved BEST -> {best_path}")
    print(f"Saved results log -> {RESULTS_CSV}")

if __name__ == "__main__":
    main()