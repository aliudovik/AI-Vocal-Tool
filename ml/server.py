import os
import joblib
import numpy as np
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from typing import List

app = FastAPI(title="AI Vocal Emotion Server")


# Expecting features from OpenSMILE ComParE_2016
class FeaturePayload(BaseModel):
    features: List[float]


# --- CONFIG ---
MODEL_PATH = "ml/models/ExtraTrees-best.pkl"
clf = None


@app.on_event("startup")
def load_model():
    global clf
    if os.path.exists(MODEL_PATH):
        try:
            clf = joblib.load(MODEL_PATH)
            print(f"[SERVER] Model loaded: {MODEL_PATH}")
        except Exception as e:
            print(f"[SERVER] Failed to load model: {e}")
    else:
        print(f"[SERVER] No model found at {MODEL_PATH}. Running in DEV MODE.")


@app.post("/predict_emotion")
def predict(payload: FeaturePayload):
    # 1. Validate Input
    expected_features = clf.n_features_in_ if clf is not None else 88

    if len(payload.features) != expected_features:
        raise HTTPException(
            status_code=400,
            detail=f"Feature mismatch. Expected {expected_features}, got {len(payload.features)}"
        )

    # 2. DEV MODE (No model trained yet)
    if clf is None:
        return {
            "emotion_score": 0.85,  # Simulate a "High Emotion" response
            "status": "ML_DEV_NO_MODEL",
            "message": "OK. Features received correctly. ML system in development."
        }

    # 3. LIVE PREDICTION (After you run train_model.py)
    try:
        # Reshape to (1, 88) for scikit-learn
        X = np.array(payload.features).reshape(1, -1)

        # Get probability of Class 1 (Emotional)
        # Assumes classes are [0=Neutral/Bad, 1=Emotional]
        prob = clf.predict_proba(X)[0][1]

        return {
            "emotion_score": float(prob),
            "status": "LIVE"
            "ML success"
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


if __name__ == "__main__":
    import uvicorn

    # Will run it online later for prompt model updates
    uvicorn.run(app, host="127.0.0.1", port=7437)