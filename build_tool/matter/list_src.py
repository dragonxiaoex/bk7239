#!/usr/bin/env python3
"""List product Matter sources. argv[1] is the src/ directory (may be a symlink)."""
import os
import sys

src_dir = sys.argv[1]
if not os.path.isdir(src_dir):
    raise SystemExit("src/ not found: %s" % src_dir)

for name in sorted(os.listdir(src_dir)):
    if name.endswith(".cpp") or name.endswith(".cc"):
        print("src/" + name)
