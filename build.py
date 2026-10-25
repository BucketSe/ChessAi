"""Builds dist/ChessAi.exe: the desktop app with ChessAi and Stockfish bundled.

    python build.py

Requires: g++ (MSYS2) with mingw32-make or make, and `pip install -r requirements.txt`.
"""
import io
import pathlib
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent
APP = ROOT / "app"
ENGINES = APP / "engines"
STOCKFISH_URL = ("https://github.com/official-stockfish/Stockfish/releases/download/"
                 "sf_19/stockfish-windows-x86-64-universal.zip")


def run(cmd, **kw):
    print(">", " ".join(map(str, cmd)))
    subprocess.run(cmd, check=True, **kw)


def build_engine():
    make = shutil.which("mingw32-make") or shutil.which("make")
    if not make:
        sys.exit("make not found (install MSYS2 with mingw-w64-ucrt-x86_64-toolchain)")
    engine = ROOT / "engine"
    run([make, "clean"], cwd=engine)
    run([make, "-j8", "ARCH=x86-64-v2"], cwd=engine)  # portable: runs on any x86-64 CPU from ~2009 on
    ENGINES.mkdir(exist_ok=True)
    shutil.copy2(engine / "chessai.exe", ENGINES / "chessai.exe")


def fetch_stockfish():
    if (ENGINES / "stockfish.exe").exists():
        return
    print("Downloading Stockfish...")
    data = urllib.request.urlopen(STOCKFISH_URL).read()
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for name in z.namelist():
            if name.endswith(".exe"):
                (ENGINES / "stockfish.exe").write_bytes(z.read(name))
            elif name.endswith("Copying.txt"):
                (ENGINES / "STOCKFISH-LICENSE.txt").write_bytes(z.read(name))


def package():
    sep = ";" if sys.platform == "win32" else ":"
    run([
        sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile", "--windowed",
        "--name", "ChessAi",
        "--icon", str(APP / "assets" / "icon.ico"),
        "--add-data", f"{APP / 'static'}{sep}static",
        "--add-binary", f"{ENGINES / 'stockfish.exe'}{sep}engines",
        "--add-binary", f"{ENGINES / 'chessai.exe'}{sep}engines",
        "--add-data", f"{ENGINES / 'STOCKFISH-LICENSE.txt'}{sep}engines",
        "--distpath", str(ROOT / "dist"),
        "--workpath", str(ROOT / "build" / "pyinstaller"),
        "--specpath", str(ROOT / "build"),
        str(APP / "desktop.py"),
    ])
    print(f"\nBuilt {ROOT / 'dist' / 'ChessAi.exe'}")


if __name__ == "__main__":
    build_engine()
    fetch_stockfish()
    package()
