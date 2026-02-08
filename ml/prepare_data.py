# prepare_data.py
import os
import glob
import librosa
import soundfile as sf
import numpy as np
import argparse
from pathlib import Path


def split_and_save(source_folder, dest_folder, label_prefix, splits=50):
    """
    Reads all .wav files in source_folder.
    Splits each into 'splits' equal parts.
    Saves to dest_folder.
    """
    # Create destination if not exists
    os.makedirs(dest_folder, exist_ok=True)

    # Get files
    files = []
    for ext in ("*.wav", "*.webm", "*.mp4"):
        files += glob.glob(os.path.join(source_folder, ext))
    print("Looking in:", source_folder)
    print("Exists?", os.path.exists(source_folder))
    print(files)
    print(f"Found {len(files)} files in {source_folder}")
    if not files:
        print(f"No .wav files found in {source_folder}!")
        return

    print(f"Processing {len(files)} files from '{source_folder}'...")

    count = 0
    for f in files:
        try:
            # Load audio
            y, sr = librosa.load(f, sr=None)  # Keep original SR

            # Calculate samples per chunk
            total_samples = len(y)
            chunk_size = total_samples // splits

            base_name = Path(f).stem

            for i in range(splits):
                start = i * chunk_size
                end = start + chunk_size

                # If it's the last chunk, take everything till the end
                if i == splits - 1:
                    y_chunk = y[start:]
                else:
                    y_chunk = y[start:end]

                # Discard chunks shorter than 0.5s (junk data)
                if len(y_chunk) / sr < 0.5:
                    continue

                # Save
                out_name = f"{label_prefix}_{base_name}_part{i + 1}.wav"
                out_path = os.path.join(dest_folder, out_name)
                sf.write(out_path, y_chunk, sr)
                count += 1

        except Exception as e:
            print(f"Error processing {f}: {e}")

    print(f"Done! Created {count} chunks in '{dest_folder}'.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Split audio files for training data")
    parser.add_argument("--input", required=True, help="Path to source folder with original WAVs")
    parser.add_argument("--target", required=True, choices=["emotion", "bad", "perfect"],
                        help="Which class is this? (emotion, bad, perfect)")

    args = parser.parse_args()

    # Destination is always data/<target>
    dest = os.path.join("data", args.target)

    split_and_save(args.input, dest, args.target)