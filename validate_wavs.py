# scripts/validate_wavs_verbose.py
from pathlib import Path
import soundfile as sf
import numpy as np

def rms_dbfs(x):
    x = np.asarray(x, dtype=np.float32)
    if x.size == 0:
        return -999.0
    r = np.sqrt(np.mean(x**2))
    if r <= 1e-20:
        return -999.0
    return 20*np.log10(r + 1e-20)

def main(root="data_pilot"):
    root = Path(root).resolve()
    print(f"[info] scanning: {root}")
    wavs = list(root.rglob("*.wav"))
    if not wavs:
        print("[warn] no .wav files found")
        return
    bad = 0
    for w in wavs:
        y, sr = sf.read(w, always_2d=False)
        if y.ndim > 1:
            y = y.mean(axis=1)
        dur = len(y)/sr if sr else 0
        peak = float(np.max(np.abs(y))) if y.size else 0.0
        dbfs = rms_dbfs(y)
        first = y[:10]
        all_zero = np.allclose(y, 0.0, atol=1e-12)
        status = "OK"
        if len(y) == 0 or dur < 0.2 or all_zero:
            status = "SILENT"
        elif peak < 1e-6 or dbfs < -60:
            status = "VERY-LOW"
        print(f"{w} | {dur:.3f}s | sr {sr} | dtype {y.dtype} | peak {peak:.6f} | RMS {dbfs:.1f} dBFS | {status}")
        print(f"  first10: {np.array2string(first, precision=6, threshold=10)}")
        if status != "OK":
            bad += 1
    print(f"\nDone. {bad} file(s) need attention out of {len(wavs)}.")

if __name__ == "__main__":
    main()
