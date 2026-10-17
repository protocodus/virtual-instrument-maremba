#!/usr/bin/env python3
"""Package a Release Linux build, failing if a plug-in or notice is missing."""

import argparse
from pathlib import Path
import tarfile

from release_metadata import package_prefix, project_version

PROJECT_DIR = Path(__file__).resolve().parent.parent
FORMATS = ("VST3/Maremba.vst3", "CLAP/Maremba.clap", "Standalone/Maremba")


def notices() -> list[Path]:
    return [PROJECT_DIR / "LICENSE", PROJECT_DIR / "THIRD_PARTY_NOTICES.md",
            *sorted((PROJECT_DIR / "ThirdParty").iterdir())]


def package(build_dir: Path, config: str) -> Path:
    artefacts = build_dir / "Maremba_artefacts" / config
    for required in (artefacts / "VST3/Maremba.vst3/Contents/x86_64-linux/Maremba.so",
                     artefacts / "CLAP/Maremba.clap", artefacts / "Standalone/Maremba",
                     *notices()):
        if not required.is_file() or required.stat().st_size == 0:
            raise FileNotFoundError(f"Missing or empty distribution file: {required}")

    dist = build_dir / "dist"
    dist.mkdir(parents=True, exist_ok=True)
    # Wildcard-driven publication must not upload a previous build as well.
    for stale in dist.glob("Maremba-*-Linux-x64.tar.gz"):
        stale.unlink()
    archive_path = dist / f"{package_prefix(project_version(build_dir))}-Linux-x64.tar.gz"
    with tarfile.open(archive_path, "w:gz") as archive:
        for name in FORMATS:
            archive.add(artefacts / name, name)
        for notice in notices():
            archive.add(notice, notice.name)
    with tarfile.open(archive_path) as archive:
        names = archive.getnames()
    for name in (*FORMATS, *(notice.name for notice in notices())):
        if name not in names:
            raise ValueError(f"{archive_path} is missing {name}")
    return archive_path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build-plugin"))
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()
    print(f"Packaged {package(args.build_dir, args.config)}")
