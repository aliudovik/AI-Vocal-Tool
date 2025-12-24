# Scoring utilities:
# - norm_block: normalize accuracy scale to 0..1
# - accuracy_score: weighted sum for "Accuracy" persona
# - emotion_score: weighted sum of MVP emotion features (already 0..1)
# - final_blend: alpha * accuracy + (1 - alpha) * emotion

import numpy as np
import math

def linmap(x, a, b):
    """Linearly map x in [a,b] -> [0,1] with clamping."""
    return float(np.clip((x - a) / (b - a + 1e-9), 0.0, 1.0))

def _safe(v, fallback):
    """Guard against None/NaN/inf."""
    if v is None:
        return fallback
    if isinstance(v, float) and (math.isnan(v) or math.isinf(v)):
        return fallback
    return v

def norm_block(row):
    """
    Normalize accuracy to 0..1 scales.
    Inputs expected in 'row':
      - f0_rmse_c (cents, lower better)
      - snr_db (dB, higher better)
      - deess_ratio (hi/mid ratio, moderate best -> handled in extractor or switch to triangle later)
      - clip_n (int, 0 best)
    """
    # pitch: widen mapping range so differences show up; reward lower RMSE
    pitch = 1.0 - linmap(float(_safe(row.get('f0_rmse_c'), 300.0)), 0.0, 300.0)

    # SNR: cap so extremely clean doesn't dominate
    snr_val = min(float(_safe(row.get('snr_db'), 0.0)), 40.0)
    snr = linmap(snr_val, 0.0, 40.0)

    # De-ess: simple inverse map
    deess = 1.0 - linmap(float(_safe(row.get('deess_ratio'), 0.30)), 0.03, 0.30)

    # Clipping penalty
    clip_ok = 1.0 if int(_safe(row.get('clip_n'), 0)) == 0 else 0.6

    return {
        'pitch_rmse': pitch,
        'snr': snr,
        'deess': deess,
        'clip_ok': clip_ok,
    }

def accuracy_score(n, weights=None):
    """
    Weighted sum for "Accuracy".
    Default weights: pitch=0.5, snr=0.2, deess=0.2, clip=0.1
    """
    w = weights or {'pitch_rmse': 0.5, 'snr': 0.2, 'deess': 0.2, 'clip_ok': 0.1}
    return (w['pitch_rmse'] * n['pitch_rmse'] +
            w['snr']       * n['snr'] +
            w['deess']     * n['deess'] +
            w['clip_ok']   * n['clip_ok'])

def emotion_score(row, weights=None):
    """
    Weighted sum of MVP emotion features (all expected 0..1 scores):
      - vibrato_stability
      - dyn_shape
      - microtiming
    """
    vib = float(_safe(row.get('vibrato_stability'), 0.0))
    dyn = float(_safe(row.get('dyn_shape'), 0.0))
    mic = float(_safe(row.get('microtiming'), 0.0))
    w = weights or {'vibrato_stability': 0.34, 'dyn_shape': 0.33, 'microtiming': 0.33}
    return w['vibrato_stability'] * vib + w['dyn_shape'] * dyn + w['microtiming'] * mic

def final_blend(acc, emo, alpha):
    """
    Alpha blend of Accuracy vs Emotion.
      alpha in [0,1] = weight on Accuracy. Emotion gets (1 - alpha).
    """
    a = float(np.clip(alpha, 0.0, 1.0))
    return float(a * acc + (1.0 - a) * emo)
