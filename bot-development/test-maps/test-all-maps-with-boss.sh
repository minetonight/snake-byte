#!/usr/bin/env bash
set -euo pipefail  # Fail fast on errors

# Enable nullglob so empty glob skips loop silently [web:21][web:27]
shopt -s nullglob

# From test-maps dir:
# Maven root: ../WinterChallenge2026-Exotec
# Maps dir: . (current dir)
MAP_DIR="$(pwd)"

echo "Scanning maps in: $MAP_DIR"

for map_file in "$MAP_DIR"/*.txt; do
  echo "Running map: $map_file"

  (
    cd ../../WinterChallenge2026-Exotec
    mvn compile exec:java \
      -Dexec.mainClass=HeadlessMain \
      -Dexec.classpathScope=test \
      -DcustomMapFile="$map_file"
  )
done

echo "Done."

