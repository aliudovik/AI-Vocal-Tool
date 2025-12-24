# AI Vocal Comp

One-button vocal comping app that helps singers record multiple takes on a loop and build a seamless “comp” by selecting the best take per phrase segment — then export the final vocal.

> **Help with data collection:** https://vocalcomping.pythonanywhere.com/

---

## What it does

Traditional comping in a DAW is powerful but slow: record takes, manually slice phrases, audition, crossfade, and assemble. **AI Vocal Comp** compresses that workflow into a fast loop:

1. **Import** an instrumental
2. Set **BPM**
3. Define **IN/OUT** loop
4. Press **Record** (once) to capture multiple takes
5. App **auto-pads + splits** the recording into loop-sized takes
6. A reference take is **segmented** into musical phrase chunks
7. The same boundaries are reused across all takes
8. Python magic (segments ranking)
10. **Export** the comped result

<img width="1920" height="1002" alt="main_interface" src="https://github.com/user-attachments/assets/fcef7313-2801-408b-b6fe-252ad5248bd7" />



---

## Key features

- **One-button loop recording:** record continuously inside IN/OUT and stop anytime.
- **Auto-padding + deterministic take splitting:** on stop, audio is padded to the loop length and split into take-sized regions (no manual trimming).
- **Musical segmentation (Python):** BPM-aware segmentation that prefers **low-energy RMS valleys** to avoid cutting sustained vowels.
- **Per-segment audition + selection:** quickly audition takes segment-by-segment.
- **Export selected comp** to a single audio file.

---

## Architecture (high level)

- **C++ app (real-time audio + UI)**
  - Transport (play/stop/loop IN-OUT)
  - Recording pipeline (buffer capture → WAV writing)
  - Auto-padding + take splitting
  - Waveform display + selection UI

- **Python toolkit**
  - Feature extraction
  - Segmentation (RMS valleys + BPM-aware target)
  - Segment scoring / ranking; writing to a compmap json
  - Stitching from the compmap

Repository structure:

```
AI-Vocal-Comp/
  README.md
  LICENSE
  .gitignore
  requirements.txt

  JUCE/                         # git submodule (JUCE framework)

  interface/
    AI-Comp-Interface.jucer
    Source/
      Main.cpp
      MainComponent.cpp
      MainComponent.h
      MainComponent_AudioAndRecording.cpp
      MainComponent_Comping.cpp
      MainComponent_Interaction.cpp
      MainComponent_Saving.cpp
      MainComponent_Views.cpp
      NeonUI.cpp
      NeonUI.h
      ProjectState.cpp
      ProjectState.h

  python/
    extract_features.py
    features.py
    io.py
    run_comping.py
    scoring.py
    segmentation.py
    stitch_from_compmap.py

  scripts/
    apply_ranker.py
    build_pairs.py
    train_ranker_sklearn.py

  models/
    ranker_sklearn.joblib        # optional (use LFS if big)

  assets/
    screenshots/
      main_interface.png
      comping_interface.png
      wireframe.png
      data_collection.png
```

---

## Technical highlights

### 1) Seamless recording: padding + auto-splitting (C++)

Users often stop mid-loop. The recording pipeline pads the captured audio buffer/WAV to a multiple of `loopLengthSamples`, then expands `takeTracks` so each loop becomes a take — all from a single user action.

> See: `src/` (recording + transport) and `MainComponent_AudioAndRecording.cpp`

### 2) BPM-aware RMS-valley segmentation (Python)

Segmentation computes an RMS envelope, detects **valleys**, and uses BPM to define a target segment duration (~2 beats). It chooses the nearest valley within a valid window; if no valley exists, it avoids forcing a cut (prevents slicing vowels).

```python
# excerpt from python/segmentation.py
rms, t = _rms_envelope(y, sr, hop=512)
valleys = _find_rms_valleys(rms, t, min_spacing=0.18)
beat = 60.0 / float(bpm)
target = max(min_seg_dur, min(4.0, 2.0 * beat))
```

```python
window_start = last + min_seg_dur
window_end   = min(last + max_seg_dur, dur)
cand = valleys[(valleys >= window_start) & (valleys <= window_end)]
if cand.size == 0:
    break  # keep a longer phrase instead of cutting a vowel
btime = float(cand[np.argmin(np.abs(cand - (last + target)))])
boundaries.append(btime); last = btime
```

**Why this functionality for boundaries:**
- Early boundaries align with clear RMS dips between short syllables/phrases.
- A long middle region stays uncut because the signal remains energetic/continuous (no valid valley in the allowed window).
- Later pauses create new valleys → new segments.
This bias intentionally preserves musical phrasing.

---

## Dataset / data collection

I initially had no “take” dataset for segmentation + ranking experiments, so I built a separate data-collection mini-app to generate consistent takes and labels:

https://vocalcomping.pythonanywhere.com/

---

## Results

In my test scenario (18 takes), this workflow was **~21× faster** than manual slicing/comping. In typical sessions, it should be **10×+ faster** depending on song length and take count.

---

## Roadmap

- Manual crossfade editor per boundary (cleaner transitions)
- ML-based scoring for emotion/accuracy understanding
- Quantization / timing alignment features

---

## Timeline & role

Built solo in ~6 weeks part-time, finished (demo) **20 Dec 2025**. I owned end-to-end design, C++ audio/UI, and Python tooling.

---

## License

MIT License (see the license section).
