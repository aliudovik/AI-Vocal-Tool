# src/segmentation.py
#
# Segmentation (vowel-safe) + guaranteed debug logging.
#
# This version ALWAYS writes a log file, even if env vars are not set.
# Default log location (Windows): %TEMP%\aivocal_segmentation_debug.log
#
# Optional env overrides:
#   AIVOCAL_SEG_LOG         -> full path to log file
#   AIVOCAL_SEG_TRACE       -> "1" enables periodic traceback dumps
#   AIVOCAL_SEG_TRACE_EVERY -> seconds (default 10)
#   AIVOCAL_SEG_TIME_BUDGET -> DP budget seconds (default 2.0)
#
# NOTE: Logging is flushed on every write.

import os
import tempfile
import time
import numpy as np
import librosa
import faulthandler


debug_emotion = False

# ----------------------------
# Guaranteed logging
# ----------------------------

def _default_log_path():
    # Always writable for the current user.
    return os.path.join(tempfile.gettempdir(), "aivocal_segmentation_debug.log")


def _get_cfg():
    log_path = os.getenv("AIVOCAL_SEG_LOG", _default_log_path())
    trace = os.getenv("AIVOCAL_SEG_TRACE", "0") == "1"
    trace_every = int(os.getenv("AIVOCAL_SEG_TRACE_EVERY", "10"))
    time_budget = float(os.getenv("AIVOCAL_SEG_TIME_BUDGET", "2.0"))
    return log_path, trace, trace_every, time_budget


def _open_log():
    log_path, _, _, _ = _get_cfg()
    try:
        # ensure parent exists if a custom path is used
        parent = os.path.dirname(log_path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        # line-buffered file (still explicitly flushed in _log)
        return open(log_path, "a", encoding="utf-8")
    except Exception:
        return None


def _log(fh, msg: str):
    if fh is None:
        return
    try:
        ts = time.strftime("%H:%M:%S")
        fh.write(f"[{ts}] {msg.rstrip()}\n")
        fh.flush()
    except Exception:
        pass


# ----------------------------
# Original helpers
# ----------------------------

def one_phrase(y, sr):
    """Fallback: treat whole file as one phrase."""
    return [(0.0, len(y) / float(sr))]


def _rms_envelope(y, sr, hop=512):
    """Return (rms, times) for the RMS envelope."""
    rms = librosa.feature.rms(y=y, frame_length=2048, hop_length=hop).squeeze()
    times = librosa.frames_to_time(np.arange(len(rms)), sr=sr, hop_length=hop)
    return rms.astype(np.float32), times.astype(np.float32)


def _smooth_1d(x, win):
    if win is None or win <= 1:
        return x.astype(np.float32)
    win = int(win)
    k = np.ones(win, dtype=np.float32) / float(win)
    return np.convolve(x.astype(np.float32), k, mode="same").astype(np.float32)


def _find_rms_valleys(
    rms,
    times,
    min_spacing=0.18,
    valley_percentile=35.0,
    quiet_cap=0.75,
    smooth_win=7,
):
    """
    Returns:
        valley_times: np.ndarray [K]
        valley_rms:   np.ndarray [K]  (normalized/smoothed RMS at valley; lower = quieter)
    """
    n = len(rms)
    if n < 3:
        return np.zeros(0, dtype=np.float32), np.zeros(0, dtype=np.float32)

    max_r = float(rms.max())
    rms_norm = (rms / max_r) if max_r > 0 else rms.astype(np.float32)

    dur = float(times[-1]) if len(times) > 0 else 0.0
    env = _smooth_1d(rms_norm, smooth_win)

    quiet_thresh = float(np.percentile(env, float(valley_percentile)))
    quiet_thresh = min(quiet_thresh, float(quiet_cap))

    valley_idxs = []
    for i in range(1, n - 1):
        t = float(times[i])
        if t < 0.03 or t > dur - 0.03:
            continue

        r = float(env[i])
        if r > quiet_thresh:
            continue

        if r <= float(env[i - 1]) and r <= float(env[i + 1]):
            valley_idxs.append(i)

    if not valley_idxs:
        return np.zeros(0, dtype=np.float32), np.zeros(0, dtype=np.float32)

    # De-duplicate close valleys: keep lowest RMS in each cluster
    keep_times = []
    keep_vals = []
    for idx in valley_idxs:
        t = float(times[idx])
        r = float(env[idx])

        if not keep_times:
            keep_times.append(t)
            keep_vals.append(r)
            continue

        if t - keep_times[-1] < float(min_spacing):
            if r < keep_vals[-1]:
                keep_times[-1] = t
                keep_vals[-1] = r
        else:
            keep_times.append(t)
            keep_vals.append(r)

    return np.asarray(keep_times, dtype=np.float32), np.asarray(keep_vals, dtype=np.float32)


def _choose_boundaries_greedy(
    valley_times,
    dur,
    min_seg_dur,
    target_seg_dur,
    max_seg_dur,
):
    """Fast fallback: window + closest-to-target valley."""
    if valley_times.size == 0:
        return [0.0, float(dur)]

    boundaries = [0.0]
    last = 0.0

    while True:
        window_start = last + float(min_seg_dur)
        if float(dur) - window_start < float(min_seg_dur):
            break

        window_end = min(last + float(max_seg_dur), float(dur) - float(min_seg_dur))
        if window_end <= window_start:
            break

        mask = (valley_times >= window_start) & (valley_times <= window_end)
        if not np.any(mask):
            break

        cand = valley_times[mask]
        desired = last + float(target_seg_dur)
        idx = int(np.argmin(np.abs(cand - desired)))
        btime = float(cand[idx])

        if btime - last < float(min_seg_dur):
            break

        boundaries.append(btime)
        last = btime

    if float(dur) - boundaries[-1] >= 0.2:
        boundaries.append(float(dur))

    return boundaries


def _choose_boundaries_dp(
    cand_times,
    cand_valley_rms,
    min_seg_dur,
    target_seg_dur,
    max_seg_dur,
    *,
    max_edge_dur=None,
    w_over=1.10,
    w_under=0.65,
    w_long=7.50,
    w_boundary=0.08,
    w_valley=1.10,
    time_budget_sec=2.0,
    debug_fh=None,
):
    """
    DP boundary chooser with:
      - bounded transitions (prevents O(n^2) blowups)
      - time budget (fallback if exceeded)
      - periodic progress logging
    """
    n = len(cand_times)
    if n < 2:
        return [0.0, float(cand_times[-1])]

    if max_edge_dur is None:
        max_edge_dur = max(float(max_seg_dur) + 0.75 * float(target_seg_dur), 8.0)
    max_edge_dur = float(max_edge_dur)

    dp = np.full(n, np.inf, dtype=np.float64)
    prev = np.full(n, -1, dtype=np.int32)
    dp[0] = 0.0

    t0 = time.time()
    last_heartbeat = t0

    for j in range(1, n):
        if (time.time() - t0) > float(time_budget_sec):
            _log(debug_fh, f"DP budget hit ({time_budget_sec:.2f}s) -> abort DP")
            return None

        now = time.time()
        if (now - last_heartbeat) > 0.5:
            _log(debug_fh, f"DP progress j={j}/{n} t={float(cand_times[j]):.3f}s")
            last_heartbeat = now

        tj = float(cand_times[j])

        # bounded scan backwards; break once internal edge too long
        for i in range(j - 1, -1, -1):
            ti = float(cand_times[i])
            L = tj - ti

            if L < float(min_seg_dur):
                continue

            if j != (n - 1) and L > max_edge_dur:
                break

            over = max(0.0, L - float(target_seg_dur))
            under = max(0.0, float(target_seg_dur) - L)
            len_cost = float(w_over) * (over * over) + float(w_under) * (under * under)

            long_over = max(0.0, L - float(max_seg_dur))
            long_cost = float(w_long) * (long_over * long_over)

            if j == n - 1:
                b_cost = 0.0
            else:
                r = float(cand_valley_rms[j])  # 0=quiet good, 1=loud bad
                b_cost = float(w_boundary) + float(w_valley) * r

            cost = dp[i] + len_cost + long_cost + b_cost
            if cost < dp[j]:
                dp[j] = cost
                prev[j] = i

        # if unreachable due to bounds, allow unbounded fallback for this j
        if prev[j] == -1:
            for i in range(j - 1, -1, -1):
                ti = float(cand_times[i])
                L = tj - ti
                if L < float(min_seg_dur):
                    continue

                over = max(0.0, L - float(target_seg_dur))
                under = max(0.0, float(target_seg_dur) - L)
                len_cost = float(w_over) * (over * over) + float(w_under) * (under * under)

                long_over = max(0.0, L - float(max_seg_dur))
                long_cost = float(w_long) * (long_over * long_over)

                if j == n - 1:
                    b_cost = 0.0
                else:
                    r = float(cand_valley_rms[j])
                    b_cost = float(w_boundary) + float(w_valley) * r

                cost = dp[i] + len_cost + long_cost + b_cost
                if cost < dp[j]:
                    dp[j] = cost
                    prev[j] = i

    if prev[n - 1] == -1:
        return [float(cand_times[0]), float(cand_times[-1])]

    # reconstruct
    idx = n - 1
    chosen = []
    while idx != -1:
        chosen.append(float(cand_times[idx]))
        idx = int(prev[idx])
    chosen.reverse()
    return chosen


def segment_phrase_reference(
    y,
    sr,
    bpm,
    min_seg_dur=0.45,
    max_seg_dur=5.0,
):
    """
    Segment a reference take into sub-phrase chunks.

    Returns:
        list[(start_s, end_s)] in seconds.
    """
    log_path, trace, trace_every, time_budget = _get_cfg()
    fh = _open_log()
    t_all0 = time.time()

    _log(fh, "------------------------------")
    _log(fh, f"segment start log_path={log_path}")
    _log(fh, f"sr={sr} bpm={bpm} len(y)={len(y)}")

    try:
        if trace and fh is not None:
            try:
                faulthandler.dump_traceback_later(int(trace_every), repeat=True, file=fh)
                _log(fh, f"faulthandler enabled every {int(trace_every)}s")
            except Exception as e:
                _log(fh, f"faulthandler enable failed: {repr(e)}")

        dur = len(y) / float(sr)
        _log(fh, f"dur={dur:.3f}s")

        if dur <= 2 * float(min_seg_dur):
            _log(fh, "too short -> one phrase")
            return one_phrase(y, sr)

        hop = 512
        rms, rms_times = _rms_envelope(y, sr, hop=hop)
        _log(fh, f"rms_frames={len(rms)} hop={hop}")

        if bpm is not None and bpm > 0:
            beat_period = 60.0 / float(bpm)
            eff_min_seg_dur = min(float(min_seg_dur), max(0.25, 0.90 * beat_period))
            target_seg_dur = max(eff_min_seg_dur, min(4.0, 2.0 * beat_period))
            valley_min_spacing = min(0.18, 0.40 * beat_period)
        else:
            eff_min_seg_dur = float(min_seg_dur)
            target_seg_dur = 1.2
            valley_min_spacing = 0.18

        _log(
            fh,
            f"params eff_min_seg_dur={eff_min_seg_dur:.3f} target_seg_dur={target_seg_dur:.3f} "
            f"max_seg_dur={float(max_seg_dur):.3f} valley_min_spacing={valley_min_spacing:.3f} "
            f"dp_budget={float(time_budget):.2f}s",
        )

        valley_times, valley_vals = _find_rms_valleys(
            rms,
            rms_times,
            min_spacing=valley_min_spacing,
            valley_percentile=35.0,
            quiet_cap=0.75,
            smooth_win=7,
        )
        _log(fh, f"valleys={int(valley_times.size)}")

        if valley_times.size == 0:
            _log(fh, "no valleys -> one segment")
            return [(0.0, dur)]

        cand_times = np.concatenate(
            [np.asarray([0.0], dtype=np.float32), valley_times, np.asarray([dur], dtype=np.float32)]
        )
        cand_vals = np.concatenate(
            [np.asarray([1.0], dtype=np.float32), valley_vals, np.asarray([1.0], dtype=np.float32)]
        )
        _log(fh, f"candidates={len(cand_times)}")

        boundaries = _choose_boundaries_dp(
            cand_times=cand_times,
            cand_valley_rms=cand_vals,
            min_seg_dur=eff_min_seg_dur,
            target_seg_dur=target_seg_dur,
            max_seg_dur=float(max_seg_dur),
            max_edge_dur=max(float(max_seg_dur) + 0.75 * float(target_seg_dur), 8.0),
            time_budget_sec=float(time_budget),
            debug_fh=fh,
        )

        if boundaries is None:
            _log(fh, "dp aborted -> greedy fallback")
            boundaries = _choose_boundaries_greedy(
                valley_times=valley_times,
                dur=dur,
                min_seg_dur=eff_min_seg_dur,
                target_seg_dur=target_seg_dur,
                max_seg_dur=float(max_seg_dur),
            )

        if boundaries[-1] < dur - 1e-3:
            boundaries.append(float(dur))

        _log(fh, f"boundaries={len(boundaries)} first={boundaries[0]:.3f} last={boundaries[-1]:.3f}")

        merged = []
        for s, e in zip(boundaries[:-1], boundaries[1:]):
            if not merged:
                merged.append([float(s), float(e)])
                continue

            seg_len = float(e) - float(s)
            if seg_len < float(eff_min_seg_dur):
                merged[-1][1] = float(e)
            else:
                merged.append([float(s), float(e)])

        segments = [(float(s), float(e)) for (s, e) in merged if (float(e) - float(s)) > 1e-3]
        _log(fh, f"segments={len(segments)} elapsed={(time.time() - t_all0):.3f}s")

        if not segments:
            _log(fh, "no segments after merge -> one phrase")
            return one_phrase(y, sr)

        # UI SPEED CAP: high BPM makes too many segments -> slow JUCE comp tab render
        if bpm and bpm > 140 and len(segments) > 8:
            _log(fh, f"UI cap: {len(segments)} segs -> 8 (bpm={bpm})")
            segments = segments[:8]
            segments[-1] = (segments[-1][0], dur)  # extend last to phrase end

        t_all1 = time.time()
        _log(fh, f"done: segments={len(segments)} elapsed={(t_all1 - t_all0):.3f}s")

        return segments


    finally:
        if trace:
            try:
                faulthandler.cancel_dump_traceback_later()
            except Exception:
                pass
        if fh is not None:
            try:
                fh.close()
            except Exception:
                pass
