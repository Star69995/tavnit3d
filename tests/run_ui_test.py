#!/usr/bin/env python3
"""Runs the app with an automation script (see src/editor/editor_automation.cpp)
(@TMP@: a temporary folder, @DIR@: the script's folder) and fails if a step failed or an error dialog showed up."""
import os
import subprocess
import sys
import tempfile

exe, script = os.path.abspath(sys.argv[1]), sys.argv[2]
with tempfile.TemporaryDirectory() as tmp:
    path = os.path.join(tmp, "script.txt")
    with open(script, encoding="utf-8") as f:
        text = f.read().replace("@TMP@", tmp.replace("\\", "/"))
        # files next to the script, like recipes
        text = text.replace("@DIR@", os.path.dirname(os.path.abspath(script)).replace("\\", "/"))
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    env = dict(os.environ, DUNE3D_AUTOMATION=path)
    try:
        out = subprocess.run([exe], env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180).stderr
    except subprocess.TimeoutExpired:
        print("timed out (a modal dialog may be blocking)")
        sys.exit(1)

sys.stdout.reconfigure(errors="replace")
lines = [l for l in out.splitlines() if l.startswith("automation:")]
print("\n".join(lines))
bad = [l for l in lines if "FAILED" in l or "MessageDialog" in l or "Exception" in l]
if bad or not any(l.strip() == "automation: quit" for l in lines):
    print("UI test failed:", *bad, sep="\n  ")
    sys.exit(1)
print("UI test passed")
