"""
Download Speech Commands v0.02 dataset for KWS training.
Downloads and extracts into the current directory.
URL: http://download.tensorflow.org/data/speech_commands_v0.02.tar.gz
Size: ~2.5 GB compressed, ~3.5 GB extracted
"""
import tarfile, urllib.request, sys, os
from pathlib import Path

URL  = "http://download.tensorflow.org/data/speech_commands_v0.02.tar.gz"
DEST = Path(__file__).parent / "speech_commands_v0.02.tar.gz"

def main():
    print("Downloading Speech Commands v0.02 (~2.5 GB)...")
    print(f"  URL: {URL}")
    print(f"  Dest: {DEST}")

    def report(count, block_size, total_size):
        pct = count * block_size * 100 / total_size if total_size > 0 else 0
        sys.stdout.write(f"\r  {count * block_size / (1<<20):.0f} / {total_size / (1<<20):.0f} MB ({pct:.0f}%)")
        sys.stdout.flush()

    urllib.request.urlretrieve(URL, str(DEST), reporthook=report)
    print("\nDownload complete. Extracting...")

    with tarfile.open(DEST, "r:gz") as tar:
        tar.extractall(path=Path(__file__).parent, filter='data')
    print("Extraction complete.")

    # Remove tar file to save space
    DEST.unlink()
    print(f"Removed {DEST.name}")
    print("Done. Dataset ready in data/ directory.")


if __name__ == '__main__':
    main()
