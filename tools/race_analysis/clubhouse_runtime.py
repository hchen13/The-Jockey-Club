"""Application-local paths and installation identity for the portable Clubhouse.

Developer defaults remain in the checkout. A package.json marker makes portable
mode independent of environment overrides, PATH, source headers and build trees.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import stat

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
APPLICATION = "the-jockey-club-race-dashboard"


def payload_root():
    return ROOT if (ROOT / "package.json").is_file() else None


def helper_path(variable, filename):
    payload = payload_root()
    if payload:
        return payload / "bin" / filename
    return Path(os.environ.get(variable, ROOT / "build" / "Release" / filename))


def planner_python():
    payload = payload_root()
    if payload:
        return payload / "python-sqp" / "python.exe"
    return Path(os.environ.get("JOCKEY_PLANNER_PYTHON", ROOT / "build/race-analysis-python/Scripts/python.exe"))


class InstalledLayout:
    """One game-root identity, with a whole-layout pre-0.32 compatibility mode.

    Never choose individual old files after the consolidated main INI exists.
    The installer performs the migration while both applications are stopped.
    """

    def __init__(self, game):
        self.game = Path(game).resolve()
        self.mod = self.game / "TheJockeyClub"
        selected = self._legacy_configuration()
        self.legacy = selected is not None
        self.config = self.game if self.legacy else self.mod / "config"
        self.main_config = self.config / (selected or "TheJockeyClub.ini")
        self.courses = self.game / "TheJockeyClub-courses" if self.legacy else self.mod / "courses"
        self.recordings = self.game / "TheJockeyClub-motion" if self.legacy else self.mod / "recordings"
        self.logs = self.game if self.legacy else self.mod / "logs"
        self.legacy_cache = self.game if self.legacy else self.mod / "cache/legacy"
        self.state = (self.game / "TheJockeyClub-data" if self.legacy else self.mod / "data") / "clubhouse"

    def _legacy_configuration(self):
        # Match runtime_paths::Detect: any existing new main path, old main
        # directory, or status error prevents a fall-through to ancient data.
        for index, path in enumerate((self.mod / "config/TheJockeyClub.ini",
                                      self.game / "TheJockeyClub.ini", self.game / "HorseSpeedLab.ini")):
            try:
                info = path.stat()
            except FileNotFoundError:
                continue
            except OSError:
                return None
            return path.name if index and stat.S_ISREG(info.st_mode) else None
        return None

    def persistent_state(self):
        legacy = self.game / "TheJockeyClub-data/clubhouse"
        if not self.legacy and not self.state.exists() and legacy.exists():
            raise RuntimeError("Legacy Clubhouse history has not been migrated; run the 0.32 installer before opening Clubhouse.")
        return self.state


def installed_game_root():
    """Discover only the installed or standalone portable launcher's game root."""
    payload = payload_root()
    if payload is None:
        return None
    launcher = payload.parent
    for candidate in (launcher, *launcher.parents):
        if (candidate / "RDR2.exe").is_file():
            return candidate
    return None


def default_state(game):
    if payload_root():
        return InstalledLayout(game).persistent_state()
    return ROOT / "analysis" / "live-race-dashboard"


def installation_id(game):
    # resolve() uses GetFinalPathNameByHandle on Windows, resolving junctions as
    # well as relative paths. The native launcher hashes the same UTF-8 path.
    return hashlib.sha256(os.path.normcase(str(Path(game).resolve())).encode("utf-8")).hexdigest()


def discovery_file(game):
    local = Path(os.environ.get("LOCALAPPDATA", Path.home() / ".local/share"))
    return local / "TheJockeyClub" / "instances" / (installation_id(game) + ".json")


class InstallationLease:
    """One backend per canonical game installation, across developer/packages."""

    def __init__(self, game):
        self.handle = None
        if os.name != "nt":
            return  # POSIX is a developer/test mode, not the Windows launcher.
        import ctypes
        from ctypes import wintypes
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
        self.kernel.CreateMutexW.restype = wintypes.HANDLE
        self.kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.kernel.WaitForSingleObject.restype = wintypes.DWORD
        self.kernel.ReleaseMutex.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.kernel.CreateMutexW(None, False, "Local\\TheJockeyClub-Backend-" + installation_id(game))
        if not self.handle:
            raise OSError(ctypes.get_last_error(), "Cannot create Clubhouse installation mutex")
        if self.kernel.WaitForSingleObject(self.handle, 0) not in (0, 0x80):
            self.kernel.CloseHandle(self.handle)
            self.handle = None
            raise RuntimeError("A Clubhouse backend already owns this game installation; use its discovery file to reopen it.")

    def close(self):
        if self.handle:
            self.kernel.ReleaseMutex(self.handle)
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def remove_owned_discovery(path, session):
    try:
        if json.loads(path.read_text(encoding="utf-8")).get("session_id") == session:
            path.unlink()
    except (OSError, ValueError):
        pass
