#!/usr/bin/env python3
"""Package a Release Windows build, failing if a plug-in or notice is missing."""

import argparse
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

from release_metadata import package_prefix, project_version

PROJECT_DIR = Path(__file__).resolve().parent.parent
REQUIRED = (
    "VST3/Maremba.vst3/Contents/x86_64-win/Maremba.vst3",
    "CLAP/Maremba.clap",
    "Standalone/Maremba.exe",
)


def notices() -> list[Path]:
    return [PROJECT_DIR / "LICENSE", PROJECT_DIR / "THIRD_PARTY_NOTICES.md",
            *sorted((PROJECT_DIR / "ThirdParty").iterdir())]


def package(build_dir: Path, config: str) -> Path:
    artefacts = build_dir / "Maremba_artefacts" / config
    for source in (*(artefacts / name for name in REQUIRED), *notices()):
        if not source.is_file() or source.stat().st_size == 0:
            raise FileNotFoundError(f"Missing or empty distribution file: {source}")

    dist = build_dir / "dist"
    dist.mkdir(parents=True, exist_ok=True)
    # Wildcard-driven publication must not upload a previous build as well.
    for stale in dist.glob("Maremba-*-Windows-x64.zip"):
        stale.unlink()
    archive_path = dist / f"{package_prefix(project_version(build_dir))}-Windows-x64.zip"
    with ZipFile(archive_path, "w", compression=ZIP_DEFLATED) as archive:
        # The complete VST3 bundle, including its generated module info.
        for directory in ("VST3/Maremba.vst3", "CLAP", "Standalone"):
            for source in sorted((artefacts / directory).rglob("*")):
                if source.is_file():
                    archive.write(source, source.relative_to(artefacts).as_posix())
        for notice in notices():
            archive.write(notice, notice.name)

    with ZipFile(archive_path) as archive:
        if archive.testzip() is not None:
            raise ValueError(f"Corrupt archive: {archive_path}")
        for name in (*REQUIRED, *(notice.name for notice in notices())):
            if archive.getinfo(name).file_size == 0:
                raise ValueError(f"Empty archive entry: {name}")
    return archive_path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build-plugin"))
    parser.add_argument("--config", default="Release")
    args = parser.parse_args()
    print(f"Packaged {package(args.build_dir, args.config)}")
