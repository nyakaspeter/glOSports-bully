#!/bin/sh
# Bully's two videos, converted for the console's player (loader/movie.c):
# BullyOrig/Movies/Rockstar.m4v (the logo at startup) and AttractMode.mp4
# (the opening on New Game, Jimmy and his mother on the way to Bullworth,
# under the credits), from the patch OBB, 1280x720 -> 640x360 H.264 baseline
# (no B-frames: the cheapest to decode on the Cortex-A7), AAC 44.1 kHz stereo.
# The originals (5.7 Mbit/s for the opening) are too heavy to decode in time.
# Out: gamedata/movies/BullyOrig/Movies/, copied loose into game/ (the player
# opens loose files before the OBB's).
#   tools/movies.sh [patch.obb]
set -eu
cd "$(dirname "$0")/.."
obb=${1:-gamedata/game/Android/patch.obb}
out=gamedata/movies/BullyOrig/Movies
mkdir -p "$out"
tmp=$(mktemp -d)
for f in Rockstar.m4v AttractMode.mp4; do
	unzip -o -q -d "$tmp" "$obb" "BullyOrig/Movies/$f"
	ffmpeg -v error -y -i "$tmp/BullyOrig/Movies/$f" -vf scale=640:360:flags=lanczos \
		-c:v libx264 -profile:v baseline -level 3.0 -preset slow -crf 20 -pix_fmt yuv420p \
		-c:a aac -b:a 128k -ar 44100 -ac 2 -movflags +faststart "$out/$f"
done
rm -rf "$tmp"
ls -la "$out"
