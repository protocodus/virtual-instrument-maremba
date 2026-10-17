#!/usr/bin/env python3
"""Shared package names: Maremba-<X.Y.Z>-build.<run>, the run being CI's or BUILD_NUMBER."""

import argparse
import os
from pathlib import Path
import re


def build_number() -> str:
    value = os.environ.get("BUILD_NUMBER") or os.environ.get("GITHUB_RUN_NUMBER")
    if value is None:
        return "local"
    if not re.fullmatch(r"[1-9][0-9]*", value):
        raise ValueError("BUILD_NUMBER / GITHUB_RUN_NUMBER must be a positive integer")
    return value


def project_version(build_dir: Path) -> str:
    versions = re.findall(
        r"^CMAKE_PROJECT_VERSION:STATIC=(.+)$",
        (build_dir / "CMakeCache.txt").read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    if len(versions) != 1 or not re.fullmatch(r"\d+\.\d+\.\d+", versions[0]):
        raise ValueError(f"Expected one X.Y.Z project version in {build_dir}/CMakeCache.txt")
    return versions[0]


def package_prefix(version: str) -> str:
    return f"Maremba-{version}-build.{build_number()}"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(package_prefix(project_version(args.build_dir)))
    except (OSError, ValueError) as error:
        parser.exit(1, f"error: {error}\n")
