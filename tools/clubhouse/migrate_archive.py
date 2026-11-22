"""Explicit COPY of a developer archive into a new portable data directory.

The source is preserved byte-for-byte. The destination must not already exist;
no merge, overwrite, game INI edit or process stop is performed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "race_analysis"))
from clubhouse_runtime import InstallationLease
from output_policy import OutputCapacityError, bounded_workspace, checked_directory, _regular
from storage_retention import StorageRetention


def read_bounded(path, maximum):
    checked_directory(path.parent)
    before = path.lstat()
    if not _regular(before) or before.st_size > maximum:
        raise OutputCapacityError("Unsafe or oversized archive input; original preserved: " + str(path))
    with path.open('rb') as stream:
        opened = os.fstat(stream.fileno())
        data = stream.read(maximum + 1)
        after = os.fstat(stream.fileno())
    identity = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns)
    if (len(data) > maximum or identity(before) != identity(opened) or
            identity(before) != identity(after) or identity(before) != identity(path.lstat())):
        raise OutputCapacityError("Archive input changed; original preserved: " + str(path))
    return data


def migrate(source, game, destination=None):
    source, game = checked_directory(source), checked_directory(game)
    destination = checked_directory(destination or game / "TheJockeyClub/data/clubhouse")
    if destination.exists():
        raise ValueError("Destination already exists; migration never overwrites or merges an archive")
    index = source / "recordings.json"
    index_bytes = read_bounded(index, StorageRetention.INDEX_BYTES)
    entries = json.loads(index_bytes)
    if not isinstance(entries, dict):
        raise ValueError("Unsupported developer archive index")
    if len(entries) > StorageRetention.ARCHIVE_FILES:
        raise OutputCapacityError("Developer archive exceeds the replay count capacity; source preserved")
    files = [(index, index_bytes)]
    total = 0
    for identifier in entries:
        if (Path(identifier).name != identifier or identifier in (".", "..") or
                "/" in identifier or "\\" in identifier or ":" in identifier or
                identifier.endswith((".", " ")) or len(identifier) > 128):
            raise ValueError("Invalid recording identifier in developer archive")
        path = source / "recordings" / (identifier + ".json.gz")
        # Count before any output is created; each later read remains bounded.
        checked_directory(path.parent)
        info = path.lstat()
        if not _regular(info) or info.st_size > StorageRetention.RECORD_BYTES:
            raise OutputCapacityError("Unsafe or oversized replay; source preserved")
        total += info.st_size
        if total > StorageRetention.ARCHIVE_BYTES:
            raise OutputCapacityError("Developer archive exceeds the replay byte capacity; source preserved")
        files.append((path, None))
    lease = InstallationLease(game)
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        expected = ['payload/', 'payload/recordings/', 'payload/import.json']
        expected += ['payload/' + path.relative_to(source).as_posix() for path, _ in files]
        with bounded_workspace(destination, 'archive-import', parent=destination.parent, expected=expected) as workspace:
            staging = workspace / 'payload'
            (staging / 'recordings').mkdir(parents=True)
            checksums = {}
            total = 0
            for path, initial in files:
                relative = path.relative_to(source)
                maximum = StorageRetention.INDEX_BYTES if path == index else StorageRetention.RECORD_BYTES
                data = read_bounded(path, maximum)
                if initial is not None and data != initial:
                    raise ValueError("Archive index changed; import was not published")
                if path != index:
                    total += len(data)
                    if total > StorageRetention.ARCHIVE_BYTES:
                        raise OutputCapacityError("Developer archive grew beyond replay capacity; source preserved")
                before = hashlib.sha256(data).hexdigest()
                target = staging / relative
                with target.open('xb') as stream:
                    stream.write(data)
                    stream.flush()
                    os.fsync(stream.fileno())
                if (hashlib.sha256(read_bounded(target, maximum)).hexdigest() != before or
                        hashlib.sha256(read_bounded(path, maximum)).hexdigest() != before):
                    raise ValueError("Archive changed during copy; import was not published")
                checksums[relative.as_posix()] = before
            receipt = json.dumps({"source": str(source), "copied_records": len(entries), "sha256": checksums}, indent=2).encode('utf-8')
            if len(receipt) > 1024*1024:
                raise OutputCapacityError("Import receipt exceeds 1 MiB; source preserved")
            with (staging / 'import.json').open('xb') as stream:
                stream.write(receipt)
                stream.flush()
                os.fsync(stream.fileno())
            staging.rename(destination)
            return {"source_preserved": str(source), "destination": str(destination), "records": len(entries)}
    finally:
        lease.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--game-root", type=Path, required=True)
    parser.add_argument("--destination", type=Path)
    args = parser.parse_args()
    print(json.dumps(migrate(args.source, args.game_root, args.destination), ensure_ascii=False, indent=2))
