import numpy as np
import math

def linmap(x, a, b): return float(np.clip((x-a)/(b-a+1e-9), 0, 1)) # every value into comparable 0..1 scale

def _safe(v, fallback):
    # guard against None/NaN/inf
    if v is None: return fallback
    if isinstance(v, float) and (math.isnan(v) or math.isinf(v)): return fallback
    return v

def norm_block(row):
    # pitch: widen to 0–300 cents → less zero-clamping
    pitch_rmse_c = _safe(row.get('f0_rmse_c'), 300.0)
    pitch = 1.0 - linmap(pitch_rmse_c, 0.0, 300.0)

    # snr: cap at 40 dB, then map 0–40
    snr_db = _safe(row.get('snr_db'), 0.0)
    snr_val = min(float(snr_db), 40.0)
    snr = linmap(snr_val, 0.0, 40.0)

    # de-ess: expand low end so sub-0.05 isn’t all 1.0
    deess_ratio = _safe(row.get('deess_ratio'), 0.30)
    deess = 1.0 - linmap(deess_ratio, 0.03, 0.30)

    # clipping: simple penalty
    clip_n = int(_safe(row.get('clip_n'), 0))
    clip_ok = 1.0 if clip_n == 0 else 0.6

    return {
        'pitch_rmse': pitch,
        'snr': snr,
        'deess': deess,
        'clip_ok': clip_ok,
    }


def accuracy_score(n):
    # quick sum; tune later
    return 0.5*n['pitch_rmse'] + 0.2*n['snr'] + 0.2*n['deess'] + 0.1*n['clip_ok']
# scoring (like in weights, accuracy only for now)