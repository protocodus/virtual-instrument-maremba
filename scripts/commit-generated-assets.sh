#!/usr/bin/env bash
# Publish one main build's screenshot, MP3 demos and README blocks (G2, G3, G5)
# in a single commit, and only while main still points at the built commit.
set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "usage: $0 source_sha generated_asset_dir remote distribution_url build_number" >&2
    exit 1
fi
SOURCE_SHA="$1"
ASSET_DIR="$(cd "$2" && pwd)"
REMOTE="$3"
DISTRIBUTION_URL="$4"
BUILD_NUMBER="$5"
REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "${REPO_ROOT}"
EXPECTED_DEMOS=20
SCREENSHOT="Docs/screenshots/maremba.png"

if [[ "$(git rev-parse HEAD)" != "${SOURCE_SHA}" ]]; then
    echo "error: checkout HEAD does not match source SHA ${SOURCE_SHA}" >&2
    exit 1
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "error: tracked checkout must be clean before publishing generated assets" >&2
    exit 1
fi

README_COPY="$(mktemp "${TMPDIR:-/tmp}/maremba-readme.XXXXXX")"
trap 'rm -f "${README_COPY}"' EXIT
python3 - "${ASSET_DIR}" "${README_COPY}" "${SOURCE_SHA}" "${DISTRIBUTION_URL}" \
    "${BUILD_NUMBER}" "${EXPECTED_DEMOS}" "${SCREENSHOT}" <<'PY'
import os
import pathlib
import re
import sys

root, output, source_sha, url, build, expected, screenshot = sys.argv[1:]
readme = pathlib.Path(root, "README.md").read_text(encoding="utf-8")

def block(text, name):
    begin, end = f"<!-- {name}-begin -->", f"<!-- {name}-end -->"
    if text.count(begin) != 1 or text.count(end) != 1 or text.index(begin) > text.index(end):
        sys.exit(f"error: README needs exactly one ordered {name} marker pair")
    return text.index(begin) + len(begin), text.index(end)

# G3: the renderer wrote the table into this README copy; every row must link
# an MP3 that this build produced.
start, stop = block(readme, "audio-demos")
linked = re.findall(r"\(Docs/audio/([0-9][0-9]-[a-z0-9-]+\.mp3)\)", readme[start:stop])
mp3s = sorted(p.name for p in pathlib.Path(root, "Docs/audio").glob("[0-9][0-9]-*.mp3"))
if len(mp3s) != int(expected) or sorted(linked) != mp3s:
    sys.exit(f"error: expected {expected} rendered MP3s matching the demo table, "
             f"found {mp3s} for {linked}")
for name in mp3s:
    data = pathlib.Path(root, "Docs/audio", name).read_bytes()
    if len(data) < 4096 or not (data[:3] == b"ID3" or data[0] == 0xFF):
        sys.exit(f"error: {name} is not a complete MP3")

png = pathlib.Path(root, screenshot).read_bytes()
if png[:8] != b"\x89PNG\r\n\x1a\n" or png[-12:] != b"\0\0\0\0IEND\xaeB`\x82":
    sys.exit("error: screenshot is not a complete PNG")

# G5: links to this build's collected artifact, build number and source commit.
repository = os.environ.get("GITHUB_REPOSITORY", "protocodus/virtual-instrument-maremba")
if not re.fullmatch(rf"https://github\.com/{re.escape(repository)}/actions/runs/[0-9]+/artifacts/[0-9]+", url):
    sys.exit("error: distribution URL must be a GitHub Actions artifact URL for this repository")
start, stop = block(readme, "build-download")
links = (f"\n**[Download main build {build}]({url})**, built from "
         f"[`{source_sha[:12]}`](https://github.com/{repository}/commit/{source_sha}). "
         f"It holds the macOS, Windows and Linux packages, `SHA256SUMS` and `BUILD.txt`.\n")
readme = readme[:start] + links + readme[stop:]
pathlib.Path(output).write_text(readme, encoding="utf-8")
PY

# Fetch immediately before staging so an older run cannot overwrite newer work.
git fetch --no-tags "${REMOTE}" refs/heads/main
if [[ "$(git rev-parse FETCH_HEAD)" != "${SOURCE_SHA}" ]]; then
    echo "::notice::main advanced past ${SOURCE_SHA}; skipping generated assets."
    exit 0
fi

# Only numbered renderer outputs are owned here.
git rm -q --ignore-unmatch -- ':(top,glob)Docs/audio/[0-9][0-9]-*.mp3'
mkdir -p Docs/audio Docs/screenshots
cp "${README_COPY}" README.md
cp "${ASSET_DIR}/${SCREENSHOT}" "${SCREENSHOT}"
cp "${ASSET_DIR}"/Docs/audio/[0-9][0-9]-*.mp3 Docs/audio/
git add -- README.md "${SCREENSHOT}" Docs/audio
if git diff --cached --quiet; then
    echo "::notice::Generated assets are unchanged; no commit needed."
    exit 0
fi

git -c user.name='github-actions[bot]' \
    -c user.email='41898282+github-actions[bot]@users.noreply.github.com' \
    commit -q -m "chore: refresh generated media for ${SOURCE_SHA:0:12}"
if git push "${REMOTE}" HEAD:refs/heads/main; then
    echo "::notice::Published generated assets for ${SOURCE_SHA}."
    exit 0
fi

# A concurrent push may win between the fetch and push. Never force an update
# or apply generated media to a different source revision.
git fetch --no-tags "${REMOTE}" refs/heads/main
if [[ "$(git rev-parse FETCH_HEAD)" != "${SOURCE_SHA}" ]]; then
    echo "::notice::main advanced during publication; skipping generated assets."
    exit 0
fi
echo "error: generated asset push failed while main still matches ${SOURCE_SHA}" >&2
exit 1
