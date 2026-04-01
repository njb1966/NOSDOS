#!/usr/bin/env python3
"""NOS-DOS: Build system
build.py - Master build orchestrator.

Stages (in order):
  1. fetch   - Download third-party dependencies (fetch_deps.py)
  2. compile - Cross-compile DOS components with Open Watcom (compile.py)
  3. image   - Create bootable FAT12 floppy disk image (mkimage.py)
  4. hdd     - Create FAT16 hard disk image for C: drive (mkhdd.py)
  5. iso     - Create bootable El Torito ISO (mkiso.py)

Usage:
  python build/build.py [options]

Options:
  --skip-fetch     Skip dependency download (assumes dist/thirdparty/ is populated)
  --skip-compile   Skip Open Watcom compilation
  --skip-hdd       Skip HDD image creation (keep existing nosdos.hdd)
  --only <stage>   Run only the named stage (fetch|compile|image|hdd|iso)
  --verbose        Show full subprocess output (default: summary only)
"""

import argparse
import importlib.util
import sys
import time
from pathlib import Path

BUILD_DIR = Path(__file__).parent.resolve()
ROOT_DIR = BUILD_DIR.parent


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    print(f"[build] {msg}", flush=True)


def run_stage(name: str, module_path: Path) -> bool:
    """Import and run a build stage module's main() function.

    Returns True on success (main() returns 0 or None).
    """
    spec = importlib.util.spec_from_file_location(name, module_path)
    if spec is None or spec.loader is None:
        log(f"ERROR: cannot load module: {module_path}")
        return False

    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)  # type: ignore[attr-defined]
    except Exception as e:
        log(f"ERROR loading {module_path.name}: {e}")
        return False

    if not hasattr(module, "main"):
        log(f"ERROR: {module_path.name} has no main() function")
        return False

    rc = module.main()
    return rc == 0 or rc is None


def banner(title: str) -> None:
    width = 60
    print("=" * width)
    print(f"  {title}")
    print("=" * width)


# ---------------------------------------------------------------------------
# Stages
# ---------------------------------------------------------------------------

STAGES = {
    "fetch":   BUILD_DIR / "fetch_deps.py",
    "compile": BUILD_DIR / "compile.py",
    "image":   BUILD_DIR / "mkimage.py",
    "hdd":     BUILD_DIR / "mkhdd.py",
    "iso":     BUILD_DIR / "mkiso.py",
}


def main() -> int:
    parser = argparse.ArgumentParser(description="NOS-DOS master build script")
    parser.add_argument("--skip-fetch",   action="store_true", help="Skip fetch stage")
    parser.add_argument("--skip-compile", action="store_true", help="Skip compile stage")
    parser.add_argument("--skip-hdd",     action="store_true", help="Skip HDD image stage")
    parser.add_argument("--only", choices=STAGES.keys(), metavar="STAGE",
                        help="Run only this stage: " + "|".join(STAGES.keys()))
    args = parser.parse_args()

    banner("NOS-DOS Build System")
    log(f"Root: {ROOT_DIR}")
    log("")

    skip = set()
    if args.skip_fetch:
        skip.add("fetch")
    if args.skip_compile:
        skip.add("compile")
    if args.skip_hdd:
        skip.add("hdd")

    if args.only:
        stages_to_run = [args.only]
    else:
        stages_to_run = list(STAGES.keys())

    total_start = time.monotonic()
    results: dict[str, bool] = {}

    for stage in stages_to_run:
        if stage in skip:
            log(f"[{stage}] SKIPPED")
            results[stage] = True
            continue

        banner(f"Stage: {stage}")
        t0 = time.monotonic()
        ok = run_stage(stage, STAGES[stage])
        elapsed = time.monotonic() - t0
        status = "OK" if ok else "FAILED"
        log(f"[{stage}] {status} ({elapsed:.1f}s)")
        results[stage] = ok

        if not ok:
            log("")
            log(f"Build stopped: stage '{stage}' failed.")
            log("Fix the error above and re-run, or use --skip-* to bypass stages.")
            return 1

    total_elapsed = time.monotonic() - total_start
    banner("Build Summary")
    for stage, ok in results.items():
        marker = "✓" if ok else "✗"
        log(f"  {marker} {stage}")

    log("")
    log(f"Total: {total_elapsed:.1f}s")

    out = ROOT_DIR / "out"
    iso = out / "nosdos.iso"
    hdd = out / "nosdos.hdd"
    if iso.exists():
        log(f"Output: {iso} ({iso.stat().st_size / (1024*1024):.1f} MB)")
    if hdd.exists():
        log(f"Output: {hdd} ({hdd.stat().st_size / (1024*1024):.1f} MB)")
    if iso.exists():
        log("")
        log("To test:")
        log(f"  python tests/boot_test.py")
        hdd_arg = f" --hdd {hdd}" if hdd.exists() else ""
        log(f"  python tests/boot_test.py{hdd_arg} --verbose")

    return 0


if __name__ == "__main__":
    sys.exit(main())
