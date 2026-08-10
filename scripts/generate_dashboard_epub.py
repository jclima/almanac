#!/usr/bin/env python3
"""Generate the dashboard EPUB and push it to the device.

See docs/mini-dashboard.md. The implementation lives in scripts/dashboard/.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from dashboard.__main__ import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
