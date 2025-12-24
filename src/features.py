import numpy as np
import librosa
import torch
import torchcrepe

EPS = 1e-9

# -------------------- generic utils --------------------

def cents(f2, f1):
    """Convert frequency ratio to musical cents (1 octave = 1200 cents)."""
    f2 = np.maximum(f2, EPS)
    f1 = np.maximum(f1, EPS)
    return 1200.0 * np.log2(f2 / f1)

def band_score(x, low, mid, high):
    """
    Triangular score peaking at 'mid'. 0 at/below 'low' and at/above 'high'.
    Good for "target band" metrics (e.g., sibilance, vibrato depth).
    """
    x = float(x)
    if x <= low or x >= high:
        return 0.0
    if x < mid:
        return (x - low) / (mid - low + EPS)
    return (high - x) / (high - mid + EPS)

# -------------------- pitch / periodicity --------------------

def f0_crepe_16k(y16k, sr16=16000, hop=320, device="cpu", model="tiny", mask_thresh=None):
    """
    Pitch track with torchcrepe at 16 kHz. Returns (f0_hz, periodicity) as numpy arrays.
    """
    # Expect shape [1, T]
    x = torch.tensor(y16k, dtype=torch.float32, device=device)[None, :]

    with torch.no_grad():
        try:
            f0, pd = torchcrepe.predict(
                x, sr16, hop, 50, 1100,
                model=model, batch_size=2048, device=device, return_periodicity=True
            )
        except TypeError:
            f0 = torchcrepe.predict(
                x, sr16, hop, 50, 1100,
                model=model, batch_size=2048, device=device
            )
            pd = torch.ones_like(f0)

        # light median smoothing
        f0 = torchcrepe.filter.median(f0, 3).squeeze().detach().cpu().numpy()
        pd = torchcrepe.filter.median(pd, 3).squeeze().detach().cpu().numpy()

    if mask_thresh is not None:
        mask = pd >= float(mask_thresh)
        f0 = f0.copy()
        f0[~mask] = 0.0

    return f0, pd

def pitch_rmse_vs_median(f0, pd=None, pd_thresh=0.6):
    """
    RMSE of pitch error (in cents) vs phrase median, using voiced frames only.
    Returns 120.0 if too few voiced frames.
    """
    if pd is None:
        voiced = f0 > 0
        f0v = f0[voiced]
        if len(f0v) < 5:
            return 120.0
        med = np.median(f0v)
        err_c = cents(f0v, med)
        return float(np.sqrt(np.mean(err_c ** 2)))

    voiced = (f0 > 0) & (pd >= pd_thresh)
    f0v = f0[voiced]
    pdv = pd[voiced]
    if len(f0v) < 5:
        return 120.0
    med = np.median(f0v)
    err_c = cents(f0v, med)
    w = pdv / (pdv.sum() + EPS)
    return float(np.sqrt(np.sum(w * (err_c ** 2))))

def voiced_ratio(pd, thresh=0.6):
    """Fraction of frames with periodicity >= thresh (confidence of voicing)."""
    if pd is None or len(pd) == 0:
        return 0.0
    return float((pd >= thresh).mean())

def mean_periodicity(pd):
    """Average periodicity/confidence across frames."""
    if pd is None or len(pd) == 0:
        return 0.0
    return float(np.mean(pd))

# -------------------- clarity / harshness / clipping --------------------

def snr_simple(y):
    """
    Crude SNR proxy from percentile RMS:
      - 'noise' ~ 10th percentile
      - 'signal' ~ 90th percentile
    Returns dB (higher ~ cleaner).
    """
    rms = librosa.feature.rms(y=y, frame_length=2048, hop_length=512).squeeze()
    noise = np.percentile(rms, 10)
    signal = np.percentile(rms, 90)
    return float(20 * np.log10((signal + EPS) / (noise + EPS)))

def deesser_ratio(y, sr):
    """Sibilance proxy: power(5–10 kHz) / power(1–5 kHz). Higher → more sibilant/harsh."""
    S = np.abs(librosa.stft(y, n_fft=2048, hop_length=512)) ** 2
    freqs = librosa.fft_frequencies(sr=sr, n_fft=2048)
    hi = S[(freqs >= 5000) & (freqs <= 10000)].mean()
    mid = S[(freqs >= 1000) & (freqs <= 5000)].mean()
    return float(hi / (mid + EPS))

def clip_count(y):
    """Count samples at/near full scale (|y| >= 0.999). Non-zero suggests clipping/distortion."""
    return int(np.sum(np.abs(y) >= 0.999))

# -------------------- MVP emotion features --------------------

def _frame_rate(sr16, hop):
    """CREPE frame rate in Hz."""
    return float(sr16) / float(hop)

def vibrato_stability(f0, pd, sr16=16000, hop=256, pd_thresh=0.6, return_details=False):
    """
    Tolerant vibrato score in [0..1] with soft maps (no hard zero unless unvoiced/insufficient data).

    Heuristic:
      1) Take confident voiced frames (pd >= pd_thresh).
      2) Detrend cents sequence (remove slow drift ~200 ms).
      3) FFT on detrended cents to find peak modulation rate in 3–9 Hz.
      4) Depth = RMS (cents) of detrended vibrato; clamp to sane range.
      5) Score with triangle maps (soft floor), then average rate/depth.

    Args:
      f0 (np.ndarray): Hz per frame.
      pd (np.ndarray): periodicity/confidence [0..1] per frame.
      sr16 (int): sample rate of f0 signal (your downsampled 16 kHz).
      hop (int): hop size used to compute f0.
      pd_thresh (float): min periodicity to consider voiced.
      return_details (bool): if True, returns (score, details_dict); else just score.

    Returns:
      float or (float, dict)
    """
    import numpy as np

    def _frame_rate(sr, hop):  # frames per second for f0 stream
        return float(sr) / float(hop)

    def _tri(x, low, mid, high, floor=0.1):
        # Triangle/bell map with soft floor
        if x <= low or x >= high:
            return float(floor)
        if x < mid:
            s = (x - low) / (mid - low + 1e-9)
        else:
            s = (high - x) / (high - mid + 1e-9)
        return float(max(s, floor))

    fps = _frame_rate(sr16, hop)
    f0 = np.asarray(f0)
    pd = np.asarray(pd)

    # Confident voiced frames
    voiced = (f0 > 0.0) & (pd >= float(pd_thresh))
    frames_voiced = int(voiced.sum())
    if frames_voiced < max(16, int(0.25 * fps)):  # ~0.25 s minimum voiced content
        return (0.0, {
            "frames_voiced": frames_voiced,
            "peak_hz": 0.0, "depth_cents": 0.0,
            "rate_score": 0.0, "depth_score": 0.0, "vib_score": 0.0
        }) if return_details else 0.0

    f0v = f0[voiced]

    # Convert to cents relative to median (remove absolute pitch)
    med = float(np.median(f0v))
    # guard against degenerate median
    med = max(med, 1e-6)
    cents_rel = 1200.0 * np.log2(f0v / med)

    # Detrend with ~200 ms moving average to isolate modulation
    win = max(3, int(round(0.20 * fps)))
    kernel = np.ones(win, dtype=np.float32) / float(win)
    trend = np.convolve(cents_rel, kernel, mode="same")
    vib = cents_rel - trend

    n = len(vib)
    if n < 16:
        return (0.0, {
            "frames_voiced": frames_voiced,
            "peak_hz": 0.0, "depth_cents": 0.0,
            "rate_score": 0.0, "depth_score": 0.0, "vib_score": 0.0
        }) if return_details else 0.0

    # FFT to find dominant vibrato rate (focus 3–9 Hz)
    freqs = np.fft.rfftfreq(n, d=1.0 / fps)
    mag = np.abs(np.fft.rfft(vib))
    band = (freqs >= 3.0) & (freqs <= 9.0)
    if not np.any(band):
        return (0.0, {
            "frames_voiced": frames_voiced,
            "peak_hz": 0.0, "depth_cents": 0.0,
            "rate_score": 0.0, "depth_score": 0.0, "vib_score": 0.0
        }) if return_details else 0.0

    peak_idx = int(np.argmax(mag[band]))
    peak_hz = float(freqs[band][peak_idx])

    # Depth (RMS of detrended cents), clamp to a sane range for scoring
    depth_cents = float(np.sqrt(np.mean(vib ** 2)))
    depth_cents = float(np.clip(depth_cents, 0.0, 150.0))

    # Tolerant scoring with soft floors:
    #   - rate: target ~5.5 Hz, acceptable ~3.5–8.0 Hz
    #   - depth: target ~45 cents, acceptable ~10–80 cents
    rate_score  = _tri(peak_hz,     low=3.5,  mid=5.5,  high=8.0,  floor=0.1)
    depth_score = _tri(depth_cents, low=10.0, mid=45.0, high=80.0, floor=0.1)

    # Combine (mean is more forgiving than product)
    vib_score = float(0.5 * rate_score + 0.5 * depth_score)

    return vib_score

def dyn_shape(y, sr):
    """
    Dynamics shape score (0..1).
    Prefers moderate dynamics: not flat (boring), not pumping/clipping.
    Heuristic: std of smoothed RMS in dB → triangle score.
    """
    rms = librosa.feature.rms(y=y, frame_length=2048, hop_length=512).squeeze()
    # smooth a bit
    win = max(3, int(round(0.10 * len(rms))))
    if win > 3:
        kernel = np.ones(win) / float(win)
        rms = np.convolve(rms, kernel, mode="same")
    db = 20.0 * np.log10(rms + EPS)
    std_db = float(np.std(db))
    # prefer around ~4 dB std (2..8 acceptable)
    return float(band_score(std_db, low=1.0, mid=4.0, high=10.0))

def microtiming(y, sr):
    """
    Microtiming score (0..1): how consistently onsets sit relative to a beat grid.
    Improvements:
      - Neutral fallback (0.5) if the beat tracker is unreliable
      - Looser jitter mapping (20..100 ms -> 1..0) to avoid collapsing to 0
    """
    hop = 512

    # Onset detection
    oenv = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    onset_frames = librosa.onset.onset_detect(onset_envelope=oenv, sr=sr, hop_length=hop)

    # If we hardly have onsets, we can't judge microtiming → neutral
    if len(onset_frames) < 3:
        return 0.5

    # Beat tracking (may be unreliable on a cappella)
    tempo, beat_frames = librosa.beat.beat_track(y=y, sr=sr, hop_length=hop)

    # Sanity checks: too few beats or weird tempo → neutral
    if tempo <= 0 or len(beat_frames) < 4 or tempo < 50 or tempo > 180:
        return 0.5

    # Convert to seconds
    times_onsets = librosa.frames_to_time(onset_frames, sr=sr, hop_length=hop)
    times_beats  = librosa.frames_to_time(beat_frames, sr=sr, hop_length=hop)

    # Another guard: if beat spacing looks degenerate
    if len(times_beats) < 2 or np.median(np.diff(times_beats)) <= 0:
        return 0.5

    # For each onset, find the nearest beat and measure offset
    offsets = []
    for t in times_onsets:
        idx = np.argmin(np.abs(times_beats - t))
        offsets.append(t - times_beats[idx])
    offsets = np.asarray(offsets)

    # Jitter = std of absolute offsets (seconds)
    jitter_s = float(np.std(np.abs(offsets)))
    jitter_ms = 1000.0 * jitter_s

    # Map jitter → score with a looser, more forgiving curve:
    #   <=20 ms → 1.0   |   >=100 ms → 0.0   | linear between
    if jitter_ms <= 20.0:
        jt_score = 1.0
    elif jitter_ms >= 100.0:
        jt_score = 0.0
    else:
        jt_score = (100.0 - jitter_ms) / (100.0 - 20.0 + EPS)

    return float(np.clip(jt_score, 0.0, 1.0))


def vibrato_analysis(
    f0,
    pd,
    sr16=16000,
    hop=256,
    pd_thresh=0.6,
    sustain_min_ms=250.0,
    sustain_slope_cents=5.0,
):
    """
    Analyze vibrato on sustained portions of the note.

    Returns a dict with:
      - frames_voiced
      - fps
      - peak_hz
      - depth_cents
      - rate_score
      - depth_score
      - vib_score              (rate_score * depth_score, using chosen region)
      - sustain_frames         (# frames considered "sustained")
      - sustain_pct            (sustain_frames / frames_voiced)
      - sustain_segments       (number of sustained segments)
      - sustain_score          (0..1, how "sustained" the note is)
      - vib_score_weighted     (vib_score * sustain_score)
    """
    fps = float(sr16) / float(hop)
    voiced = (f0 > 0) & (pd >= pd_thresh)

    out = {
        "frames_voiced": int(voiced.sum()),
        "fps": fps,
        "peak_hz": 0.0,
        "depth_cents": 0.0,
        "rate_score": 0.0,
        "depth_score": 0.0,
        "vib_score": 0.0,
        "sustain_frames": 0,
        "sustain_pct": 0.0,
        "sustain_segments": 0,
        "sustain_score": 0.0,
        "vib_score_weighted": 0.0,
    }

    if voiced.sum() < 16:
        return out

    f0v = f0[voiced]
    cents_rel = cents(f0v, np.median(f0v))

    # ---------------------------------------------------------------------
    # 1) Detect sustained regions within voiced frames
    # ---------------------------------------------------------------------
    min_sustain_frames = max(3, int(round((sustain_min_ms / 1000.0) * fps)))

    stable = np.zeros_like(f0v, dtype=bool)
    if len(f0v) > 1:
        dc = np.abs(np.diff(cents_rel))  # cents change frame-to-frame
        stable[1:] = dc < sustain_slope_cents
        stable[0] = stable[1] if len(stable) > 1 else False

    # Run-length encode "stable" TRUE segments to find sustained chunks
    sustain_segments = []
    if stable.any():
        idx = np.flatnonzero(stable)
        # Split where indices are not consecutive
        splits = np.where(np.diff(idx) > 1)[0] + 1
        groups = np.split(idx, splits)
        for g in groups:
            if len(g) >= min_sustain_frames:
                sustain_segments.append(g)

    sustain_frames = int(sum(len(g) for g in sustain_segments))
    sustain_pct = float(sustain_frames) / float(len(f0v)) if len(f0v) > 0 else 0.0
    sustain_segments_count = len(sustain_segments)

    # Simple sustain score: more sustained fraction = > higher score
    sustain_score = float(band_score(sustain_pct, low=0.1, mid=0.4, high=0.8))

    out.update({
        "sustain_frames": sustain_frames,
        "sustain_pct": sustain_pct,
        "sustain_segments": sustain_segments_count,
        "sustain_score": sustain_score,
    })

    # ---------------------------------------------------------------------
    # 2) Detrend and compute vibrato
    #    - Prefer longest sustained segment, fall back to all voiced frames
    # ---------------------------------------------------------------------
    # Detrend with ~200 ms moving average
    win = max(3, int(round(0.20 * fps)))
    kernel = np.ones(win) / float(win)
    trend = np.convolve(cents_rel, kernel, mode="same")
    vib = cents_rel - trend

    # Choose analysis indices
    if sustain_segments:
        # Use longest sustained segment
        longest_seg = max(sustain_segments, key=len)
        vib_segment = vib[longest_seg]
    else:
        # Fall back to all voiced frames
        vib_segment = vib

    n = len(vib_segment)
    if n < 16:
        return out

    freqs = np.fft.rfftfreq(n, d=1.0 / fps)
    mag = np.abs(np.fft.rfft(vib_segment))
    band = (freqs >= 3.0) & (freqs <= 9.0)
    if not np.any(band):
        return out

    peak_idx = np.argmax(mag[band])
    peak_hz = float(freqs[band][peak_idx])
    depth_cents = float(np.sqrt(np.mean(vib_segment ** 2)))

    rate_score  = band_score(peak_hz,    low=4.0,  mid=5.5,  high=7.5)
    depth_score = band_score(depth_cents, low=10.0, mid=45.0, high=100.0)
    vib_score   = float(rate_score * depth_score)
    vib_score_weighted = float(vib_score * sustain_score)

    out.update({
        "peak_hz": peak_hz,
        "depth_cents": depth_cents,
        "rate_score": float(rate_score),
        "depth_score": float(depth_score),
        "vib_score": vib_score,  # raw vibrato quality (rate * depth)
        "vib_score_weighted": vib_score_weighted,  # vibrato × sustain quality
    })
    return out



def microtiming_analysis(y, sr):
    """
    Return intermediate microtiming stats:
      - tempo, n_onsets, n_beats, jitter_ms, jt_score
    Mirrors microtiming() logic (with the improved mapping).
    """
    hop = 512
    oenv = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
    onset_frames = librosa.onset.onset_detect(onset_envelope=oenv, sr=sr, hop_length=hop)

    if len(onset_frames) < 3:
        return {
            "tempo": 0.0, "n_onsets": int(len(onset_frames)), "n_beats": 0,
            "jitter_ms": None, "jt_score": 0.5, "note": "too_few_onsets"
        }

    tempo, beat_frames = librosa.beat.beat_track(y=y, sr=sr, hop_length=hop)

    if tempo <= 0 or len(beat_frames) < 4 or tempo < 50 or tempo > 180:
        return {
            "tempo": float(tempo), "n_onsets": int(len(onset_frames)), "n_beats": int(len(beat_frames)),
            "jitter_ms": None, "jt_score": 0.5, "note": "unreliable_beat"
        }

    times_onsets = librosa.frames_to_time(onset_frames, sr=sr, hop_length=hop)
    times_beats  = librosa.frames_to_time(beat_frames, sr=sr, hop_length=hop)
    if len(times_beats) < 2 or np.median(np.diff(times_beats)) <= 0:
        return {
            "tempo": float(tempo), "n_onsets": int(len(onset_frames)), "n_beats": int(len(beat_frames)),
            "jitter_ms": None, "jt_score": 0.5, "note": "degenerate_beats"
        }

    offsets = []
    for t in times_onsets:
        idx = np.argmin(np.abs(times_beats - t))
        offsets.append(t - times_beats[idx])
    offsets = np.asarray(offsets)

    jitter_s  = float(np.std(np.abs(offsets)))
    jitter_ms = 1000.0 * jitter_s

    if jitter_ms <= 20.0:
        jt_score = 1.0
    elif jitter_ms >= 100.0:
        jt_score = 0.0
    else:
        jt_score = (100.0 - jitter_ms) / (100.0 - 20.0 + EPS)

    return {
        "tempo": float(tempo),
        "n_onsets": int(len(onset_frames)),
        "n_beats": int(len(beat_frames)),
        "jitter_ms": float(jitter_ms),
        "jt_score": float(np.clip(jt_score, 0.0, 1.0)),
        "note": "ok"
    }

