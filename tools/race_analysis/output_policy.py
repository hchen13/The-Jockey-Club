"""Admission limits for generated output; never reclaim existing user data.

The periodic retention worker can be unavailable while RDR2 is running. Writers
therefore check capacity before publishing, and use one exclusive staging name
per output directory. An interrupted staging file is preserved and blocks the
next writer instead of creating another randomly named recovery file.
"""
from __future__ import annotations

from contextlib import contextmanager
import hashlib
import os
from pathlib import Path
import re
import stat
import tempfile


class OutputCapacityError(OSError):
    pass


def _regular(info):
    return stat.S_ISREG(info.st_mode) and not _redirected(info)


def _redirected(info):
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, "st_file_attributes", 0) & 0x400)


def checked_directory(path):
    """Check the whole lexical path, so a parent junction is not hidden."""
    path = Path(path).absolute()
    for part in reversed((path, *path.parents)):
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if _redirected(info) or not stat.S_ISDIR(info.st_mode):
            raise OutputCapacityError(f"Generated output directory is unsafe: {part}")
    return path


def _identity(info):
    return info.st_dev, info.st_ino


def check_capacity(root, target, size, *, max_files, max_bytes,
                   max_file_bytes=None, max_directories=1024, max_depth=2,
                   reserved_files=0, reserved_bytes=0, staging=None):
    """Count all existing files, including unknown files and old partial output.

    Unknown files count against capacity but are never changed. Early exits keep
    a preexisting oversized tree from turning admission into an unbounded scan.
    """
    root = checked_directory(root)
    target = Path(target).absolute()
    try:
        relative = target.relative_to(root)
    except ValueError as error:
        raise OutputCapacityError("Output target escapes its capacity root") from error
    if not relative.parts or ".." in relative.parts or len(relative.parts) > max_depth + 1:
        raise OutputCapacityError("Invalid generated output depth")
    if size < 0 or size > (max_file_bytes if max_file_bytes is not None else max_bytes):
        raise OutputCapacityError("Generated output exceeds its per-file capacity")
    files = total = directories = 0
    previous = None
    pending = [(root, 0)] if root.exists() else []
    while pending:
        folder, depth = pending.pop()
        for path in folder.iterdir():
            info = path.lstat()
            if staging is not None and path == staging[0] and _identity(info) == staging[1]:
                continue
            if _redirected(info):
                raise OutputCapacityError(f"Output inventory contains a redirected path: {path}")
            if stat.S_ISDIR(info.st_mode):
                directories += 1
                if depth >= max_depth or directories > max_directories:
                    raise OutputCapacityError("Generated output directory capacity reached; existing data preserved")
                pending.append((path, depth + 1))
            elif _regular(info):
                files += 1
                total += info.st_size
                if path == target:
                    previous = info
                # One replacement can remove at most the allowed file size.
                if files > max_files + 1 or total > max_bytes + (max_file_bytes or max_bytes):
                    raise OutputCapacityError("Generated output capacity reached; existing data preserved")
            else:
                raise OutputCapacityError(f"Output inventory contains a nonregular file: {path}")
    missing_dirs = sum(not part.exists() for part in target.parent.parents
                       if part != root and part.is_relative_to(root))
    if target.parent != root and not target.parent.exists():
        missing_dirs += 1
    projected_files = files + (0 if previous else 1) + reserved_files
    projected_bytes = total - (previous.st_size if previous else 0) + size + reserved_bytes
    if (projected_files > max_files or projected_bytes > max_bytes or
            directories + missing_dirs > max_directories):
        raise OutputCapacityError("Generated output capacity reached; existing data preserved")
    return {"files": projected_files, "bytes": projected_bytes,
            "directories": directories + missing_dirs}


def bounded_write(root, target, data, **limits):
    """Publish under a hard quota with a fixed, exclusive temporary file."""
    root = checked_directory(root)
    target = Path(target).absolute()
    if not isinstance(data, bytes):
        raise TypeError("bounded_write requires encoded bytes")
    # Reject an oversized payload without even creating the output directory.
    if len(data) > limits.get("max_file_bytes", limits["max_bytes"]):
        raise OutputCapacityError("Generated output exceeds its per-file capacity")
    root.mkdir(parents=True, exist_ok=True)
    checked_directory(root)
    temporary = root / ".write-v1.tmp"
    identity = None
    try:
        with temporary.open("xb") as output:
            identity = _identity(os.fstat(output.fileno()))
            check_capacity(root, target, len(data), staging=(temporary, identity), **limits)
            target.parent.mkdir(parents=True, exist_ok=True)
            checked_directory(target.parent)
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        # Recheck the destination immediately before atomic replacement.
        check_capacity(root, target, len(data), staging=(temporary, identity), **limits)
        staged_info = temporary.lstat()
        if not _regular(staged_info) or _identity(staged_info) != identity or staged_info.st_size != len(data):
            raise OutputCapacityError("Generated staging file changed; existing output preserved")
        os.replace(temporary, target)
    except FileExistsError as error:
        raise OutputCapacityError(f"Interrupted or active output preserved: {temporary}") from error
    finally:
        if identity is not None:
            try:
                info = temporary.lstat()
                if _regular(info) and _identity(info) == identity:
                    temporary.unlink()
            except FileNotFoundError:
                pass


def publish_report(path, text):
    """Bound explicitly requested offline report artifacts in their output folder."""
    path = Path(path)
    return bounded_write(path.parent, path, text.encode('utf-8'), max_files=64,
                         max_bytes=256*1024*1024, max_file_bytes=32*1024*1024,
                         max_directories=8, max_depth=1)


@contextmanager
def bounded_workspace(identity, role, *, parent=None, expected=()):
    """One temporary workspace per installation/role, including after crashes.

    Existing directories are never claimed or removed. The caller must review
    interrupted work before retrying. Normal exits remove only this invocation's
    newly created directory after checking its identity and exact expected
    output names. An unexpected descendant preserves the complete workspace.
    """
    if not re.fullmatch(r"[a-z-]{1,32}", role):
        raise ValueError("Invalid temporary workspace role")
    key = hashlib.sha256(str(Path(identity).absolute()).casefold().encode()).hexdigest()[:24]
    parent = checked_directory(parent or tempfile.gettempdir())
    folder = parent / (".tjc-work-" + key + "-" + role)
    try:
        folder.mkdir()
    except FileExistsError as error:
        raise OutputCapacityError(f"Interrupted or active work preserved: {folder}") from error
    owned = _identity(folder.lstat())
    permitted = {Path(name).as_posix() for name in expected if not str(name).endswith('/')}
    permitted_dirs = {Path(name).as_posix() for name in expected if str(name).endswith('/')}
    permitted_dirs |= {Path(name).parent.as_posix() for name in permitted}
    permitted_dirs |= {ancestor.as_posix() for name in permitted for ancestor in Path(name).parents}
    try:
        yield folder
    finally:
        if _identity(folder.lstat()) != owned or _redirected(folder.lstat()):
            raise OutputCapacityError(f"Temporary workspace changed; preserved: {folder}")
        removable_files, removable_dirs = [], []
        for current, names, files in os.walk(folder, followlinks=False):
            for name in (*names, *files):
                info = (Path(current) / name).lstat()
                relative = (Path(current) / name).relative_to(folder).as_posix()
                allowed = relative in (permitted_dirs if stat.S_ISDIR(info.st_mode) else permitted)
                if _redirected(info) or not allowed or not (stat.S_ISDIR(info.st_mode) or _regular(info)):
                    raise OutputCapacityError(f"Temporary workspace contains an unknown or unsafe path; preserved: {folder}")
                path = Path(current) / name
                (removable_dirs if stat.S_ISDIR(info.st_mode) else removable_files).append((path, _identity(info)))
        # Remove only names in the checked role manifest. A newly inserted
        # unknown child makes rmdir fail; no recursive sweep can consume it.
        for path, identity in removable_files:
            info = path.lstat()
            if not _regular(info) or _identity(info) != identity:
                raise OutputCapacityError(f"Temporary output changed; preserved: {path}")
            path.unlink()
        for path, identity in sorted(removable_dirs, key=lambda item: len(item[0].parts), reverse=True):
            info = path.lstat()
            if _redirected(info) or _identity(info) != identity:
                raise OutputCapacityError(f"Temporary directory changed; preserved: {path}")
            path.rmdir()
        folder.rmdir()


SETTINGS_RECOVERY_SLOTS = 8
SETTINGS_FILE_BYTES = 1024 * 1024
_RECOVERY_MARKER = b'{"schema":"tjc-settings-recovery-v1","slots":8,"file_bytes":1048576}\n'


def settings_recovery_slot(folder, size):
    """Choose one fixed backup slot, preserving the pre-policy timestamp set."""
    if not 0 <= size <= SETTINGS_FILE_BYTES:
        raise OutputCapacityError("Settings exceed the 1 MiB recovery capacity; configuration preserved")
    folder = checked_directory(folder)
    folder.mkdir(parents=True, exist_ok=True)
    marker = folder / ".bounded-recovery-v1.json"
    slots = [folder / f"recovery-{number}.ini" for number in range(SETTINGS_RECOVERY_SLOTS)]
    if not marker.exists():
        if any(path.exists() or path.is_symlink() for path in slots):
            raise OutputCapacityError("Unclaimed settings recovery files preserved")
        with marker.open("xb") as output:
            output.write(_RECOVERY_MARKER)
            output.flush()
            os.fsync(output.fileno())
    marker_info = marker.lstat()
    if not _regular(marker_info) or marker_info.st_size != len(_RECOVERY_MARKER) or marker.read_bytes() != _RECOVERY_MARKER:
        raise OutputCapacityError("Settings recovery policy is unreadable; existing data preserved")
    available, existing = [], []
    for path in slots:
        try:
            info = path.lstat()
        except FileNotFoundError:
            available.append(path)
            continue
        if not _regular(info) or info.st_size > SETTINGS_FILE_BYTES:
            raise OutputCapacityError("Settings recovery slot is unsafe or oversized; existing data preserved")
        existing.append((info.st_mtime_ns, path.name, path))
    return available[0] if available else min(existing)[2]
