# src/segmentation.py
# Phrase segmentation helpers.
#
# Current approach:
#   - Use RMS (energy) only to find low-energy valleys.
#   - Prefer cuts at those valleys so we don't slice sustained vowels.
#   - Aim for ~2-beat segments (via BPM), but:
#       * enforce a minimum duration (so we don't get tiny segments),
#       * use a soft max (~5 s), but allow longer if there are no good valleys.
#
# The best overall take (chosen in extract_features.py) defines the
# "master" segment grid; all other takes are cut at the same times.

import numpy as np
import librosa


def one_phrase(y, sr):
    """Fallback: treat whole file as one phrase."""
    return [(0.0, len(y) / float(sr))]


def _rms_envelope(y, sr, hop=512):
    """Return (rms, times) for the RMS envelope."""
    rms = librosa.feature.rms(y=y, frame_length=2048, hop_length=hop).squeeze()
    times = librosa.frames_to_time(np.arange(len(rms)), sr=sr, hop_length=hop)
    return rms.astype(np.float32), times.astype(np.float32)


def _find_rms_valleys(rms, times, min_spacing=0.18):
    """
    Find candidate valley times from RMS:
      - local minima,
      - below a global "quiet" threshold (percentile-based),
      - de-duplicated so we don't get tons of very close valleys.
    """
    n = len(rms)
    if n < 3:
        return np.zeros(0, dtype=np.float32)

    # Normalize RMS to [0,1]
    max_r = float(rms.max())
    if max_r > 0:
        rms_norm = rms / max_r
    else:
        rms_norm = rms.copy()

    dur = float(times[-1]) if len(times) > 0 else 0.0

    # Global "quiet" threshold: below this => considered a dip/valley candidate.
    # 25th percentile usually picks the quieter inter-word gaps.
    quiet_thresh = float(np.percentile(rms_norm, 25))
    # Don't let the threshold be too high (or everything becomes a valley).
    quiet_thresh = min(quiet_thresh, 0.6)

    valley_idxs = []
    for i in range(1, n - 1):
        t = times[i]
        # Avoid extreme edges
        if t < 0.03 or t > dur - 0.03:
            continue

        r = rms_norm[i]
        if r > quiet_thresh:
            continue

        # Local minimum condition
        if r <= rms_norm[i - 1] and r <= rms_norm[i + 1]:
            valley_idxs.append(i)

    if not valley_idxs:
        return np.zeros(0, dtype=np.float32)

    # De-duplicate valleys that are too close: keep the lowest one in each cluster
    keep_times = []
    keep_rms = []
    for idx in valley_idxs:
        t = float(times[idx])
        r = float(rms_norm[idx])

        if not keep_times:
            keep_times.append(t)
            keep_rms.append(r)
            continue

        if t - keep_times[-1] < min_spacing:
            # If this new valley is lower (quieter), replace the previous one.
            if r < keep_rms[-1]:
                keep_times[-1] = t
                keep_rms[-1] = r
        else:
            keep_times.append(t)
            keep_rms.append(r)

    return np.asarray(keep_times, dtype=np.float32)


def segment_phrase_reference(
    y,
    sr,
    bpm,
    min_seg_dur=0.45,
    max_seg_dur=5.0,
):
    """
    Segment a reference take into sub-phrase chunks.

    Design goals:
      - Boundaries must fall at low-energy valleys (between words/syllables),
        so we avoid splitting long continuous vowels.
      - Typical segments should be around ~2 beats (BPM-based).
      - Minimum segment duration ~0.45 s (~2 short words).
      - Soft maximum segment duration ~5 s, but if there are no good valleys
        (long continuous singing on one word), we allow longer segments.

    Args:
      y (np.ndarray): mono waveform.
      sr (int): sample rate.
      bpm (float): user-provided tempo in BPM.
      min_seg_dur (float): minimum segment length in seconds.
      max_seg_dur (float): "soft" maximum segment length in seconds.

    Returns:
      list[(start_s, end_s)] in seconds.
    """
    dur = len(y) / float(sr)
    if dur <= 2 * min_seg_dur:
        # Phrase too short to meaningfully segment
        return one_phrase(y, sr)

    hop = 512
    rms, rms_times = _rms_envelope(y, sr, hop=hop)

    # Detect RMS valleys once; we will pick among these.
    valley_times = _find_rms_valleys(rms, rms_times, min_spacing=0.18)

    # Target duration: about 2 beats, but clamped to reasonable bounds.
    if bpm is not None and bpm > 0:
        beat_period = 60.0 / float(bpm)
        target_seg_dur = max(min_seg_dur, min(4.0, 2.0 * beat_period))
    else:
        target_seg_dur = 1.2  # generic fallback ~ spoken phrase chunk

    # If there are no valleys at all, either the phrase is very compressed/flat
    # or it's continuous singing. In that case, don't force cuts.
    if valley_times.size == 0:
        return [(0.0, dur)]

    boundaries = [0.0]
    last = 0.0

    # Iterate forward through the phrase, placing cuts where:
    #   - we are at least min_seg_dur from the last boundary,
    #   - there's a valley in [last + min_seg_dur, last + max_seg_dur].
    while True:
        # Earliest we are allowed to cut
        window_start = last + min_seg_dur
        # If there isn't enough room left for another min segment, stop.
        if dur - window_start < min_seg_dur:
            break

        # Latest we'd *like* to cut (soft max). Keep some room for the tail.
        window_end = min(last + max_seg_dur, dur - min_seg_dur)
        if window_end <= window_start:
            break

        # Candidate valleys inside this window
        mask = (valley_times >= window_start) & (valley_times <= window_end)
        if not np.any(mask):
            # No good valley in this window: don't force an arbitrary cut.
            # We'll leave the remainder as one longer segment.
            break

        cand = valley_times[mask]
        desired = last + target_seg_dur

        # Pick the valley closest to the desired time
        idx = int(np.argmin(np.abs(cand - desired)))
        btime = float(cand[idx])

        # Safety: ensure progress and minimum duration
        if btime - last < min_seg_dur:
            # If we somehow got an invalid boundary, stop to avoid infinite loop.
            break

        boundaries.append(btime)
        last = btime

    # Always close at phrase end
    if dur - boundaries[-1] >= 0.2:  # tiny tail (<200 ms) is ignored
        boundaries.append(dur)

    # Build (start, end) segments and merge any tiny leftovers defensively
    merged = []
    for s, e in zip(boundaries[:-1], boundaries[1:]):
        if not merged:
            merged.append([s, e])
            continue

        seg_len = e - s
        if seg_len < min_seg_dur:
            # Merge too-short segment with previous one
            merged[-1][1] = e
        else:
            merged.append([s, e])

    segments = [(float(s), float(e)) for (s, e) in merged if (e - s) > 1e-3]

    if not segments:
        return one_phrase(y, sr)

    return segments
