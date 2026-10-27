#!/usr/bin/env bash
# Publish one main build's screenshot, WAV demos and README blocks (G2, G3, G5)
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
EXPECTED_DEMOS="$(find Docs/audio/midi -maxdepth 1 -name '[0-9][0-9]-*.mid' | wc -l)"
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
python3 - "${ASSET_DIR}" "${README_COPY}" "${EXPECTED_DEMOS}" "${SCREENSHOT}" <<'PY'
import pathlib
import re
import sys

root, output, expected, screenshot = sys.argv[1:]
readme = pathlib.Path(root, "README.md").read_text(encoding="utf-8")

def block(text, name):
    begin, end = f"<!-- {name}-begin -->", f"<!-- {name}-end -->"
    if text.count(begin) != 1 or text.count(end) != 1 or text.index(begin) > text.index(end):
        sys.exit(f"error: README needs exactly one ordered {name} marker pair")
    return text.index(begin) + len(begin), text.index(end)

# G3: update_readme_blocks.py wrote the table into this README copy; it must
# link exactly one WAV per committed preset phrase, all produced by this build.
start, stop = block(readme, "audio-demos")
linked = re.findall(r"\(Docs/audio/([0-9][0-9]-[a-z0-9-]+\.wav)\)", readme[start:stop])
wavs = sorted(p.name for p in pathlib.Path(root, "Docs/audio").glob("[0-9][0-9]-*.wav"))
if len(wavs) != int(expected) or sorted(linked) != wavs:
    sys.exit(f"error: expected {expected} rendered WAVs matching the demo table, "
             f"found {wavs} for {linked}")
for name in wavs:
    data = pathlib.Path(root, "Docs/audio", name).read_bytes()
    if len(data) < 4096 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        sys.exit(f"error: {name} is not a complete WAV")

png = pathlib.Path(root, screenshot).read_bytes()
if png[:8] != b"\x89PNG\r\n\x1a\n" or png[-12:] != b"\0\0\0\0IEND\xaeB`\x82":
    sys.exit("error: screenshot is not a complete PNG")

pathlib.Path(output).write_text(readme, encoding="utf-8")
PY
# G5: links to this build's collected artifact, build number and source commit.
python3 Tools/update_readme_blocks.py download --readme "${README_COPY}" \
    --url "${DISTRIBUTION_URL}" --build "${BUILD_NUMBER}" --commit "${SOURCE_SHA}" \
    --repository "${GITHUB_REPOSITORY:-protocodus/virtual-instrument-maremba}"

# Fetch immediately before staging so an older run cannot overwrite newer work.
git fetch --no-tags "${REMOTE}" refs/heads/main
if [[ "$(git rev-parse FETCH_HEAD)" != "${SOURCE_SHA}" ]]; then
    echo "::notice::main advanced past ${SOURCE_SHA}; skipping generated assets."
    exit 0
fi

# Only numbered renderer outputs are owned here; the numbered MP3s are the
# retired demo format.
git rm -q --ignore-unmatch -- ':(top,glob)Docs/audio/[0-9][0-9]-*.mp3' ':(top,glob)Docs/audio/[0-9][0-9]-*.wav'
mkdir -p Docs/audio Docs/screenshots
cp "${README_COPY}" README.md
cp "${ASSET_DIR}/${SCREENSHOT}" "${SCREENSHOT}"
cp "${ASSET_DIR}"/Docs/audio/[0-9][0-9]-*.wav Docs/audio/
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
