#!/usr/bin/env python3
"""Prepend a timestamped changelog entry for development rebuilds."""
import datetime, re, sys

changelog = "debian/changelog"

with open(changelog) as f:
    old = f.read()

# Extract base version (strip any existing +YYYYMMDDHHMMSS suffix)
m = re.match(r'\S+ \(([^)]+)\)', old)
if not m:
    sys.exit("Cannot parse debian/changelog")
base = re.sub(r'\+\d{14}$', '', m.group(1))

ts      = datetime.datetime.now().strftime("%Y%m%d%H%M%S")
newver  = f"{base}+{ts}"
rfc2822 = datetime.datetime.now(datetime.timezone.utc).strftime(
              "%a, %d %b %Y %H:%M:%S +0000")

source = None
with open("debian/control") as f:
    for line in f:
        if line.startswith("Source:"):
            source = line.split(":", 1)[1].strip()
            break
if not source:
    sys.exit("Cannot find Source: in debian/control")

entry = (
    f"{source} ({newver}) unstable; urgency=low\n\n"
    f"  * Development rebuild {ts}\n\n"
    f" -- Yann Chemin <dr.yann.chemin@gmail.com>  {rfc2822}\n\n"
)

with open(changelog, "w") as f:
    f.write(entry + old)

print(f"=== Stamped version {newver} ===")
