# Linux/RHEL Build and Packaging Guide

This guide builds and packages the app in a distribution root with this layout:

- `<root>/AICompInterface` (Linux executable)
- `<root>/ml`
- `<root>/src`
- `<root>/python` (embedded Python)
- `<root>/data_pilot`

The final artifact is a zip archive of `<root>`.

## 1) What changed in code

- `src/features.py`: F0 extraction now uses fast `librosa.yin` (torch/torchcrepe removed).
- `src/extract_features.py`: OpenSMILE switched to `eGeMAPSv02` functionals (88 features).
- Runtime model is loaded from `ml/BalancedRandomForest.pkl` (existing model, no retraining required).
- `interface/Source/AppPaths.h`: packaged root detection added so executable-parent layout is preferred when it contains `ml/src/python/data_pilot`.

## 2) Build on older Linux ABI (recommended)

To support older RHEL systems, build in an older userspace image so glibc compatibility is broad.

Recommended images:

- `rockylinux:8` (good practical baseline)
- `centos:7` (older baseline, stricter compatibility)

Use whichever is closest to your target runtime fleet.

## 3) Build dependencies in container/VM

Install at least:

- `gcc`, `g++`, `make`, `cmake` (3.22+), `pkg-config`
- ALSA dev headers (`alsa-lib-devel` on RHEL-like, `libasound2-dev` on Debian/Ubuntu)
- Optional JACK dev headers if you need JACK backend

Also provide JUCE source and set `JUCE_DIR` to the JUCE root (contains `CMakeLists.txt` and `modules/`).

## 4) Build the executable

From repository root:

```bash
mkdir -p build
cd build
cmake -DJUCE_DIR=/absolute/path/to/JUCE -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
```

Expected output binary is typically under:

- `build/AICompInterface_artefacts/Release/AICompInterface`

## 5) Create distribution root layout

Create a clean package root, example:

```bash
export DIST_ROOT=/absolute/path/to/AI-Vocal-Tool-linux
mkdir -p "$DIST_ROOT"
```

Copy app + runtime folders:

```bash
cp /absolute/path/to/build/AICompInterface_artefacts/Release/AICompInterface "$DIST_ROOT/"
cp -R /absolute/path/to/repo/ml "$DIST_ROOT/"
cp -R /absolute/path/to/repo/src "$DIST_ROOT/"
cp -R /absolute/path/to/repo/data_pilot "$DIST_ROOT/"
```

## 6) Embed Python into `<root>/python`

Use a portable Python distribution (recommended) and unpack it directly into:

- `$DIST_ROOT/python`

After this step, ensure one of these exists:

- `$DIST_ROOT/python/bin/python3`
- `$DIST_ROOT/python/bin/python`

## 7) Install Python dependencies into embedded Python

Use repository `requirements.txt` (torch stack removed):

```bash
"$DIST_ROOT/python/bin/python3" -m pip install -r /absolute/path/to/repo/requirements.txt
```

If you need a fully self-contained folder, install into target path:

```bash
"$DIST_ROOT/python/bin/python3" -m pip install -r /absolute/path/to/repo/requirements.txt --target "$DIST_ROOT/python/lib/site-packages"
```

## 8) Use existing model (no retraining)

Copy your current trained model into package root:

```bash
cp /absolute/path/to/repo/ml/BalancedRandomForest.pkl "$DIST_ROOT/ml/"
```

Verify this exists:

- `$DIST_ROOT/ml/BalancedRandomForest.pkl`

## 9) Smoke test package locally

From distribution root:

```bash
cd "$DIST_ROOT"
./AICompInterface
```

Expected runtime behavior:

- App root resolves to executable parent (`$DIST_ROOT`) via `AppPaths`.
- ML server starts with:
  - Python at `$DIST_ROOT/python/bin/python3`
  - Server script at `$DIST_ROOT/ml/server.py`
  - Model path `ml/BalancedRandomForest.pkl` (resolved from app root cwd)

## 10) Zip for distribution

From parent folder of `$DIST_ROOT`:

```bash
zip -r AI-Vocal-Tool-linux.zip "$(basename "$DIST_ROOT")"
```

## 11) Optional size reductions

- Strip executable:
  - `strip "$DIST_ROOT/AICompInterface"`
- Remove caches/tests/docs from embedded Python site-packages if needed.
- Keep `data_pilot` only with required phrase/take data.

## 12) Troubleshooting checklist

- If app cannot find Python: verify `$DIST_ROOT/python/bin/python3` exists and is executable.
- If app cannot find server: verify `$DIST_ROOT/ml/server.py` exists.
- If server reports feature mismatch: ensure extraction and model are both eGeMAPSv02 functionals and that `BalancedRandomForest.pkl` matches this feature setup.
- If binary fails on target RHEL due glibc: rebuild in an older base image (for example CentOS 7) and repackage.
