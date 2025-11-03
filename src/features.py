import numpy as np
import librosa
import torch
import torchcrepe


def cents(f2, f1):
    #Convert frequency ratio to musical cents (1 octave = 1200 cents).
    f2 = np.maximum(f2, 1e-6)
    f1 = np.maximum(f1, 1e-6)
    return 1200.0 * np.log2(f2 / f1)



def f0_crepe_16k(y16k, sr16=16000, hop=256, device="cpu", model="full", mask_thresh=None):
    """
    Pitch track with torchcrepe at 16 kHz. Works with torchcrepe versions that
    expect [batch, time] input (preprocess will add the channel dim).
    Returns (f0_hz, periodicity) as numpy arrays.
    """
    # IMPORTANT: shape [1, T] (NOT [1,1,T])
    x = torch.tensor(y16k, dtype=torch.float32, device=device)[None, :]

    with torch.no_grad():
        try:
            f0, pd = torchcrepe.predict(
                x, sr16, hop, 50, 1100,
                model=model, batch_size=1024, device=device, return_periodicity=True
            )
        except TypeError:
            # Older builds without return_periodicity
            f0 = torchcrepe.predict(
                x, sr16, hop, 50, 1100,
                model=model, batch_size=1024, device=device
            )
            pd = torch.ones_like(f0)

        # smooth a bit
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
    If periodicity is provided, weight errors by periodicity and require pd >= pd_thresh.

    Returns:
      float: RMSE in cents (higher = more out of tune).
             Returns 120.0 as a safe fallback if not enough voiced frames.
    """
    if pd is None:
        voiced = f0 > 0
        f0v = f0[voiced]
        if len(f0v) < 5:
            return 120.0
        med = np.median(f0v)
        err_c = cents(f0v, med)
        return float(np.sqrt(np.mean(err_c ** 2)))

    # With periodicity: keep confident voiced frames, weight by pd
    voiced = (f0 > 0) & (pd >= pd_thresh)
    f0v = f0[voiced]
    pdv = pd[voiced]
    if len(f0v) < 5:
        return 120.0
    med = np.median(f0v)
    err_c = cents(f0v, med)
    w = pdv / (pdv.sum() + 1e-9)
    return float(np.sqrt(np.sum(w * (err_c ** 2))))


def voiced_ratio(pd, thresh=0.6):
    """Fraction of frames with periodicity >= thresh (how much is confidently voiced)."""
    if pd is None or len(pd) == 0:
        return 0.0
    return float((pd >= thresh).mean())


def mean_periodicity(pd):
    """Average periodicity/confidence across frames."""
    if pd is None or len(pd) == 0:
        return 0.0
    return float(np.mean(pd))


# ---------- other MVP features ----------

def snr_simple(y):
    """
    Crude SNR proxy from percentile RMS:
      - 'noise' ~ 10th percentile
      - 'signal' ~ 90th percentile
    Returns dB (higher ~ cleaner)."""
    rms = librosa.feature.rms(y=y, frame_length=2048, hop_length=512).squeeze()
    noise = np.percentile(rms, 10)
    signal = np.percentile(rms, 90)
    return float(20 * np.log10((signal + 1e-9) / (noise + 1e-9)))


def deesser_ratio(y, sr):
    """
    Sibilance proxy: power(5–10 kHz) / power(1–5 kHz).
    Higher → more sibilant/harsh."""
    S = np.abs(librosa.stft(y, n_fft=2048, hop_length=512)) ** 2
    freqs = librosa.fft_frequencies(sr=sr, n_fft=2048)
    hi = S[(freqs >= 5000) & (freqs <= 10000)].mean()
    mid = S[(freqs >= 1000) & (freqs <= 5000)].mean()
    return float(hi / (mid + 1e-9))


def clip_count(y):
    """
    Count samples at/near full scale (|y| >= 0.999).
    Non-zero suggests digital clipping/distortion."""
    return int(np.sum(np.abs(y) >= 0.999))
