"""Launch offline analysis children without taking a console or foreground focus."""
from __future__ import annotations

import os
import subprocess


def run_analysis(command, **options):
    """Keep subprocess.run capture/timeout/error behavior and use quiet scheduling.

    CREATE_NO_WINDOW prevents a console-subsystem exporter or Python child from
    creating a window when its parent is the resident pythonw dashboard. Explicit
    below-normal priority also covers callers outside that resident process.
    POSIX callers receive no Windows-only options.
    """
    if os.name == "nt":
        options["creationflags"] = (options.get("creationflags", 0) |
                                    subprocess.CREATE_NO_WINDOW |
                                    subprocess.BELOW_NORMAL_PRIORITY_CLASS)
    return subprocess.run(command, **options)
