#!/usr/bin/env python3
"""
download_launcher.py
Downloads all assets, layout images, scripts, and styles from Keychron Launcher
and packages them into keychron_launcher_assets.zip.
"""

import os
import urllib.request
import zipfile
from pathlib import Path

BASE_URL = "https://launcher.keychron.com"
OUTPUT_DIR = Path("keychron_launcher_dump")

# Key asset paths extracted from DevTools Sources tab
ASSET_URLS = [
    # Keyboard frame & keycap textures
    "/assets/keyboard/skeleton.png",
    "/assets/keyboard/keycap.png",
    "/assets/keyboard/enter.png",
    "/assets/keyboard/space.png",
    "/assets/keyboard/tab.png",
    "/assets/keyboard/en.png",
    "/assets/keyboard/magnet/axis/purple.png",
    "/assets/logo/keychron.svg",
    # Core app scripts & bundles
    "/index.html",
    "/styles.cf3de82292df2365.css",
    "/runtime.e435ea46fbeeb4af.js",
    "/polyfills.94a211898be334df.js",
    "/scripts.e34e0ee36050e207.js",
    "/main.d60cabdb24f8f1c7.js",
    "/common.e275151a2d0a85fc.js",
]

def download_file(rel_path: str, dest_dir: Path):
    url = BASE_URL + rel_path
    local_path = dest_dir / rel_path.lstrip("/")
    local_path.parent.mkdir(parents=True, exist_ok=True)

    headers = {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
    }

    print(f"[*] Fetching: {rel_path} ...", end=" ", flush=True)
    try:
        req = urllib.request.Request(url, headers=headers)
        with urllib.request.urlopen(req, timeout=10) as resp, open(local_path, "wb") as f:
            f.write(resp.read())
        print("OK")
        return True
    except Exception as e:
        print(f"FAILED ({e})")
        return False

def main():
    OUTPUT_DIR.mkdir(exist_ok=True)
    print(f"=== Downloading Keychron Launcher Assets to '{OUTPUT_DIR}' ===")

    downloaded = 0
    for rel_path in ASSET_URLS:
        if download_file(rel_path, OUTPUT_DIR):
            downloaded += 1

    # Create ZIP archive
    zip_path = Path("keychron_launcher_assets.zip")
    print(f"\n[*] Compressing {downloaded} files into {zip_path.name} ...")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zipf:
        for root, _, files in os.walk(OUTPUT_DIR):
            for file in files:
                p = Path(root) / file
                zipf.write(p, p.relative_to(OUTPUT_DIR))

    print(f"[SUCCESS] Complete! Assets saved to {zip_path.resolve()}\n")

if __name__ == "__main__":
    main()