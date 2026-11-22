"""Bounded cleanup of Clubhouse analysis artifacts, never gameplay data.

Only exact producer filenames below are touched. Reparse points, open files,
partial compression output, unknown files and settings/backups are preserved.
"""
from __future__ import annotations

import ctypes
import logging
import os
from pathlib import Path
import re
import stat
from clubhouse_runtime import InstalledLayout

RAW = re.compile(r"^(race-\d+-\d+-\d+)\.jsonl(?:\.mszip)?$")
PART = re.compile(r"^(race-\d+-\d+-\d+)\.jsonl\.mszip\.part$")
ARCHIVE = re.compile(r"^(race-\d+-\d+-\d+)\.json\.gz$")
EXPORT = re.compile(r"^[a-z0-9-]+-(?:plan|survey)\.json$")
SNAPSHOT = re.compile(r"^[0-9a-f]{64}$")
COMPARISON = re.compile(r"^(?:current|legacy|hybrid|sqp2|sqp4)\.json$")
REPARSE = 0x400


class Busy(RuntimeError):
    pass


def game_running():
    """Fail closed when the Windows process inventory cannot be inspected."""
    if os.name != "nt":
        raise OSError("RDR2 process inspection is unavailable")
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    class Entry(ctypes.Structure):
        _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
                    ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
                    ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                    ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", wintypes.LONG),
                    ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260)]
    kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    kernel.Process32FirstW.restype = wintypes.BOOL
    kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    kernel.Process32NextW.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    snap = kernel.CreateToolhelp32Snapshot(2, 0)
    if snap == ctypes.c_void_p(-1).value:
        raise OSError(ctypes.get_last_error(), "Cannot inspect running processes")
    try:
        entry = Entry(); entry.dwSize = ctypes.sizeof(entry)
        if not kernel.Process32FirstW(snap, ctypes.byref(entry)):
            raise OSError(ctypes.get_last_error(), "Cannot inspect running processes")
        while True:
            if entry.szExeFile.lower() == "rdr2.exe":
                return True
            if not kernel.Process32NextW(snap, ctypes.byref(entry)):
                error = ctypes.get_last_error()
                if error != 18:
                    raise OSError(error, "Cannot inspect running processes")
                return False
    finally:
        kernel.CloseHandle(snap)


def reparse(path):
    s = path.lstat()
    return stat.S_ISLNK(s.st_mode) or bool(getattr(s, "st_file_attributes", 0) & REPARSE)


def safe_path(root, path):
    """Reject link/junction traversal from an approved, resolved root."""
    root, path = Path(root), Path(path)
    try:
        relative = path.relative_to(root)
        if reparse(root) or not root.is_dir():
            return False
        current = root
        for part in relative.parts:
            current /= part
            if reparse(current):
                return False
        return path.resolve(strict=True).is_relative_to(root.resolve(strict=True))
    except (OSError, ValueError):
        return False


def entries(root, directory):
    if not directory.exists():
        return []
    if not safe_path(root, directory) or not directory.is_dir():
        raise OSError("Unsafe analysis directory: " + str(directory))
    with os.scandir(directory) as stream:
        return [Path(e.path) for e in stream]


def _windows_delete(root, path, expected):
    """Delete the exact opened non-reparse file, exclusively held by handle."""
    from ctypes import wintypes
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                   wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.GetFinalPathNameByHandleW.argtypes = [wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD, wintypes.DWORD]
    kernel.GetFinalPathNameByHandleW.restype = wintypes.DWORD
    kernel.SetFileInformationByHandle.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    kernel.SetFileInformationByHandle.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(path), 0x00010000 | 0x80, 0, None, 3, 0x00200000, None)
    if handle == ctypes.c_void_p(-1).value:
        raise OSError(ctypes.get_last_error(), "File is in use or unavailable", str(path))
    try:
        # The handle does not follow reparse points and denies all sharing.
        class FileInfo(ctypes.Structure):
            _fields_ = [("attributes", wintypes.DWORD), ("creation", wintypes.FILETIME),
                        ("access", wintypes.FILETIME), ("write", wintypes.FILETIME),
                        ("volume", wintypes.DWORD), ("size_high", wintypes.DWORD),
                        ("size_low", wintypes.DWORD), ("links", wintypes.DWORD),
                        ("id_high", wintypes.DWORD), ("id_low", wintypes.DWORD)]
        kernel.GetFileInformationByHandle.argtypes = [wintypes.HANDLE, ctypes.POINTER(FileInfo)]
        kernel.GetFileInformationByHandle.restype = wintypes.BOOL
        info = FileInfo()
        if not kernel.GetFileInformationByHandle(handle, ctypes.byref(info)):
            raise OSError(ctypes.get_last_error(), "Cannot inspect opened file", str(path))
        size = (int(info.size_high) << 32) | int(info.size_low)
        identity = (int(info.id_high) << 32) | int(info.id_low)
        modified_ns = (((int(info.write.dwHighDateTime) << 32) | int(info.write.dwLowDateTime)) - 116444736000000000) * 100
        if (info.attributes & (REPARSE | 0x10) or size != expected.st_size or
                (expected.st_ino and identity != expected.st_ino) or
                int(info.volume) != expected.st_dev or modified_ns != expected.st_mtime_ns):
            raise OSError("File identity or type changed: " + str(path))
        out = ctypes.create_unicode_buffer(32768)
        count = kernel.GetFinalPathNameByHandleW(handle, out, len(out), 0)
        if not count or count >= len(out):
            raise OSError(ctypes.get_last_error(), "Cannot verify opened file path", str(path))
        actual = out.value
        if actual.startswith("\\\\?\\UNC\\"):
            actual = "\\\\" + actual[8:]
        elif actual.startswith("\\\\?\\"):
            actual = actual[4:]
        if (os.path.normcase(os.path.normpath(actual)) != os.path.normcase(os.path.normpath(str(path))) or
                not Path(actual).is_relative_to(root)):
            raise OSError("Opened file path changed or escaped the approved root: " + str(path))
        class StandardInfo(ctypes.Structure):
            _fields_ = [("allocation", ctypes.c_longlong), ("end_of_file", ctypes.c_longlong),
                        ("links", wintypes.DWORD), ("delete_pending", wintypes.BOOLEAN),
                        ("directory", wintypes.BOOLEAN)]
        kernel.GetFileInformationByHandleEx.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        kernel.GetFileInformationByHandleEx.restype = wintypes.BOOL
        standard = StandardInfo()
        allocation = 0
        if kernel.GetFileInformationByHandleEx(handle, 1, ctypes.byref(standard), ctypes.sizeof(standard)):
            if standard.links == 1 and not standard.directory:
                allocation = max(0, int(standard.allocation))
        disposition = ctypes.c_ubyte(1)  # FILE_DISPOSITION_INFO.DeleteFile
        if not kernel.SetFileInformationByHandle(handle, 4, ctypes.byref(disposition), ctypes.sizeof(disposition)):
            raise OSError(ctypes.get_last_error(), "Cannot remove opened file", str(path))
        return allocation
    finally:
        kernel.CloseHandle(handle)


def delete_file(root, path, expected=None):
    if not safe_path(root, path):
        raise OSError("Unsafe file or reparse point: " + str(path))
    before = path.lstat()
    if expected is not None and (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (
            expected.st_dev, expected.st_ino, expected.st_size, expected.st_mtime_ns):
        raise OSError("File changed since enumeration: " + str(path))
    if not stat.S_ISREG(before.st_mode):
        raise OSError("Not a regular file: " + str(path))
    if os.name == "nt":
        return _windows_delete(root, path, before)
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags)
    try:
        current = os.fstat(fd)
        if ((current.st_dev, current.st_ino, current.st_size, current.st_mtime_ns) !=
                (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) or
                path.lstat().st_ino != before.st_ino):
            raise OSError("File identity changed: " + str(path))
        os.unlink(path)
        return (getattr(before, "st_blocks", 0) * 512 if before.st_nlink == 1 else 0)
    finally:
        os.close(fd)


def write_index(root, path, data):
    """Replace our index through a new exclusive temporary file, never a link."""
    root, path = Path(root), Path(path)
    if len(data)>8*1024*1024:
        raise OSError("Analysis index exceeds 8 MiB capacity; previous index preserved")
    if path.parent != root or not safe_path(root, root):
        raise OSError("Unsafe analysis index directory")
    if (path.exists() or path.is_symlink()) and not safe_path(root, path):
        raise OSError("Unsafe analysis index")
    temp = root / '.recordings-index-v1.tmp'
    fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_BINARY', 0), 0o600)
    owned = os.fstat(fd)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        current = temp.lstat()
        if ((current.st_dev,current.st_ino)!=(owned.st_dev,owned.st_ino) or
                not safe_path(root, temp) or ((path.exists() or path.is_symlink()) and not safe_path(root, path))):
            raise OSError("Analysis index path changed")
        os.replace(temp, path)
    finally:
        if temp.exists() and safe_path(root, temp):
            try:
                current=temp.lstat()
                if (current.st_dev,current.st_ino)==(owned.st_dev,owned.st_ino):
                    delete_file(root, temp, expected=current)
            except OSError:
                pass


def clear_owned_dashboard_log(state):
    path = state / "dashboard.log"
    if not path.exists():
        return None
    if not safe_path(state, path):
        raise OSError("Unsafe dashboard log")
    target = os.path.normcase(str(path.resolve()))
    for handler in logging.getLogger().handlers:
        if os.path.normcase(getattr(handler, "baseFilename", "")) != target:
            continue
        handler.acquire()
        try:
            stream = handler.stream
            if not stream or stream.closed:
                raise OSError("Dashboard log handler unavailable")
            handler.flush()
            f, p = os.fstat(stream.fileno()), path.stat()
            if (f.st_dev, f.st_ino) != (p.st_dev, p.st_ino):
                raise OSError("Dashboard log identity changed")
            if f.st_nlink != 1 or p.st_nlink != 1:
                raise OSError("Dashboard log has multiple hard links")
            stream.seek(0); stream.truncate(0); stream.flush()
            return p.st_size
        finally:
            handler.release()
    # A different application owns the log; never truncate it blindly.
    raise OSError("Dashboard log is not owned by this process")


def clear_files(game, state):
    """Delete whitelisted closed files; return retained record IDs and summary.

    The caller must own the Dashboard maintenance gate for the whole call.
    Recheck the game before every destructive operation; no recursive delete.
    """
    game, state = Path(game), Path(state)
    layout = InstalledLayout(game)
    if game_running():
        raise Busy("游戏正在运行，请退出游戏后再清理。")
    result = {"ok": True, "removed_files": 0, "bytes_reclaimed": 0, "skipped_files": []}
    retained = set()

    def skip(path, reason):
        result["skipped_files"].append({"path": str(path), "reason": str(reason)})

    snapshots = {}

    def remove(root, path):
        if not result["ok"] or game_running():
            result["ok"] = False
            skip(path, "游戏已启动，清理已停止；请退出游戏后重试。")
            return False
        try:
            expected = snapshots[path] if path in snapshots else path.lstat()
            if expected is None:
                raise OSError("File could not be inspected during enumeration")
            reclaimed = delete_file(root, path, expected)
            result["removed_files"] += 1
            result["bytes_reclaimed"] += reclaimed
            return True
        except OSError as exc:
            skip(path, exc)
            return False

    motion = layout.recordings
    # Validate every producer-owned directory before the first deletion.
    # Unknown nested names and reparse directories are skipped, never traversed.
    source = entries(game, motion)
    archives = entries(state, state / "recordings")
    exports = state / "exports"
    export_groups = []
    for group in ("host", "bookmaker"):
        directory = exports / group
        try:
            export_groups.append(entries(state, directory))
        except OSError as exc:
            skip(directory, exc)
    comparison_groups = []
    for directory in entries(state, state / "comparisons"):
        if not SNAPSHOT.fullmatch(directory.name):
            continue
        try:
            comparison_groups.append(entries(state, directory))
        except OSError as exc:
            skip(directory, exc)
    for path in [*source, *archives, *(p for group in export_groups for p in group),
                 *(p for group in comparison_groups for p in group)]:
        try:
            snapshots[path] = path.lstat()
        except OSError:
            snapshots[path] = None
    for path in source:
        match = PART.fullmatch(path.name)
        if match:
            retained.add(match[1]); skip(path, "压缩未完成，已保留")
    for path in source:
        match = RAW.fullmatch(path.name)
        if not match:
            continue
        if match[1] in retained:
            skip(path, "同场压缩尚未完成，已保留")
        elif not remove(game, path):
            retained.add(match[1])

    for path in archives:
        match = ARCHIVE.fullmatch(path.name)
        if not match:
            continue
        if match[1] in retained:
            skip(path, "原始记录仍在使用或无法清除，已保留对应分析记录")
        elif not remove(state, path):
            retained.add(match[1])

    for group in export_groups:
        for path in group:
            if EXPORT.fullmatch(path.name):
                remove(state, path)
    for group in comparison_groups:
        for path in group:
            if COMPARISON.fullmatch(path.name):
                remove(state, path)

    # Diagnostic logs only; HorseLedger audit log and all gameplay data stay.
    for name in ("TheJockeyClub.log", "TheJockeyClub.log.1", "TheJockeyClub-fault.log", "TheJockeyClub-fault.log.1"):
        path = layout.logs / name
        if path.exists() or path.is_symlink():
            remove(game, path)
    # Reconstructible native terrain observations, not the speed profiles or
    # race/horse progression. The game is checked again by remove().
    for name in ("TheJockeyClub-snow-history-v1.dat", "TheJockeyClub-snow-history-v1.dat.tmp",
                 "TheJockeyClub-snow-history-v2.dat.tmp", "TheJockeyClub-snow-history-v3.dat.tmp",
                 "TheJockeyClub-snow-history-v4.dat.tmp", "TheJockeyClub-snow-history-v5.dat.tmp"):
        path = layout.legacy_cache / name
        if path.exists() or path.is_symlink():
            remove(game, path)
    # Delete oldest migration sources before downstream data and markers.
    # Keeping the entire downstream chain when a source cannot be removed
    # prevents a future launch from reimporting a supposedly cleared cache.
    def remove_snow_sources(names, permitted=True):
        removed = permitted
        for name in names:
            path = layout.legacy_cache / name
            if path.exists() or path.is_symlink():
                if permitted:
                    if not remove(game, path):
                        removed = False
                else:
                    skip(path, "旧雪地缓存未能清除，已保留以避免下次启动重新导入。")
        return removed

    v2_removed = remove_snow_sources(("TheJockeyClub-snow-history-v2.dat",
                                       "TheJockeyClub-snow-history-v2.dat.migrating"))
    v3_removed = remove_snow_sources(("TheJockeyClub-snow-history-v3.dat",
                                       "TheJockeyClub-snow-history-v3.dat.migrating"), v2_removed)
    v4_removed = remove_snow_sources(("TheJockeyClub-snow-history-v4.dat",
                                       "TheJockeyClub-snow-history-v4.dat.migrating"), v3_removed)
    remove_snow_sources(("TheJockeyClub-snow-history-v2.dat.migrated",), v2_removed)
    remove_snow_sources(("TheJockeyClub-snow-history-v3.dat.migrated",), v3_removed)
    remove_snow_sources(("TheJockeyClub-snow-history-v4.dat.migrated",
                         "TheJockeyClub-snow-history-v5.dat"), v4_removed)
    for name in ("dashboard.log.1", "launcher.log", "backend-startup.log"):
        path = state / name
        if path.exists() or path.is_symlink():
            remove(state, path)
    path = state / "dashboard.log"
    if path.exists() or path.is_symlink():
        if not result["ok"] or game_running():
            result["ok"] = False
            skip(path, "游戏已启动，日志已保留。")
            return result, retained
        try:
            previous = clear_owned_dashboard_log(state)
            if previous is not None:
                result["removed_files"] += 1
                # Truncation is completed in-place. Do not claim a physical
                # allocation saving without a verified allocation readback.
        except OSError as exc:
            skip(path, exc)
    return result, retained
