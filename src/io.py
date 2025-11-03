import soundfile as sf, numpy as np, librosa

#sf = soundfile
#sr = samplerate

def load_wav(path, target_sr=48000, f0_sr=16000):
    y, sr = sf.read(path, always_2d=False) #y = waveform (essentially audio) converted to a float
    if y.ndim > 1: y = y.mean(axis=1)
    y = y.astype(np.float32)
    if sr != target_sr:
        y = librosa.resample(y, orig_sr=sr, target_sr=target_sr) #resampling to target sr with librosa
        sr = target_sr
    y = np.clip(y, -1.0, 1.0)
    y_f0 = librosa.resample(y, orig_sr=sr, target_sr=f0_sr) if f0_sr != sr else y #resampling for F0 (pitch)
    return y, sr, y_f0, f0_sr
