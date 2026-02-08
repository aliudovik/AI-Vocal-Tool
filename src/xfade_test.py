import json
import tempfile
from pathlib import Path

import numpy as np
import soundfile as sf

from src.stitch_from_compmap import stitch_from_compmap # src there

def _write_wav(path: Path, y: np.ndarray, sr: int):
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(path.as_posix(), y.astype(np.float32), sr)

def test_manual_xfade_window_changes_output():
    sr = 48000
    dur_s = 1.0
    n = int(sr * dur_s)
    t = np.arange(n) / sr

    # Two different aligned takes (so crossfade window position matters)
    take_a = 0.2 * np.sin(2 * np.pi * 220.0 * t)
    take_b = 0.2 * np.sin(2 * np.pi * 440.0 * t)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        base_dir = tmp / "data"
        rel = "singer01phrase01"
        phrase_dir = base_dir / rel
        _write_wav(phrase_dir / "take_a.wav", take_a, sr)
        _write_wav(phrase_dir / "take_b.wav", take_b, sr)

        # 2 segments with a boundary at 0.5s
        segments = [
            {"index": 0, "start_s": 0.0, "end_s": 0.5, "winner": {"take": "take_a"}, "candidates": []},
            {"index": 1, "start_s": 0.5, "end_s": 1.0, "winner": {"take": "take_b"}, "candidates": []},
        ]

        compmap1 = {
            "phrase": "test",
            "alpha": 0.6,
            "alpha_pct": 60,
            "bpm": 120.0,
            "base_dir": str(base_dir),
            "relative_path": rel,
            "reference_take": "take_a",
            "segments": segments,
            "boundaries": [{"xfadeStartSec": 0.45, "xfadeEndSec": 0.55}],
            "xfade_defaults": {"fade_fraction": 0.15, "min_sec": 0.030, "max_sec": 0.500},
        }

        compmap2 = dict(compmap1)
        compmap2["boundaries"] = [{"xfadeStartSec": 0.30, "xfadeEndSec": 0.52}]  # moved earlier + asymmetric

        c1 = tmp / "compmap1.json"
        c2 = tmp / "compmap2.json"
        c1.write_text(json.dumps(compmap1, indent=2), encoding="utf-8")
        c2.write_text(json.dumps(compmap2, indent=2), encoding="utf-8")

        out1 = tmp / "out1.wav"
        out2 = tmp / "out2.wav"

        stitch_from_compmap(str(c1), str(out1), fade_fraction=0.15, xfade_curve="linear", verbose=False)
        stitch_from_compmap(str(c2), str(out2), fade_fraction=0.15, xfade_curve="linear", verbose=False)

        y1, _ = sf.read(out1.as_posix(), dtype="float32")
        y2, _ = sf.read(out2.as_posix(), dtype="float32")

        # Deterministic + window affects output:
        assert y1.shape == y2.shape
        assert not np.allclose(y1, y2), "Moving xfadeStartSec/xfadeEndSec should change rendered output."

if __name__ == "__main__":
    test_manual_xfade_window_changes_output()
    print("OK")
