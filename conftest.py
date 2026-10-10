import sys
from pathlib import Path

_BUILD_DIR = Path(__file__).resolve().parent / "build"
if _BUILD_DIR.is_dir():
    sys.path.insert(0, str(_BUILD_DIR))
