# AI-Vocal-Tool (MVP)

Offline **AI-assisted vocal comping**. For each phrase with multiple takes, the tool extracts features (pitch, SNR, sibilance, clipping), scores takes for **technical accuracy**, and exports a ranked table. Stretch goal (later): regenerative polishing.

## Why
Comping vocals is slow and subjective. This MVP gives a fast, explainable **best-take** suggestion per phrase using transparent DSP + ML-ready features.

## What it does (today)
- Loads WAVs per phrase (`data_pilot/songA/phrase01/*.wav`).
- Extracts:
  - **F0** with torchcrepe (+ periodicity/confidence)
  - **Pitch RMSE (cents)** vs phrase median (periodicity-aware)
  - **SNR proxy**, **de-ess ratio** (5–10 kHz / 1–5 kHz), **clip count**
- Normalizes & **scores** takes (weights are editable in `configs/weights.yaml`).
- Writes `outputs/features.csv` sorted best→worst per phrase.

## Quick start
```bash
# from project root, in your venv
pip install -r requirements.txt
python src/extract_features.py \
  --root data_pilot/songA \
  --cfg configs/weights.yaml \
  --out_csv outputs/features.csv
