#!/usr/bin/env python3
"""NOS-DOS: Build system
mkindex.py - Generate packages/packages.idx from all .npkg definitions.

Scans packages/<category>/*.NPKG, validates each file against the package
format spec (packages/README.md), and writes a fresh packages.idx.

Exit codes:
  0  success (index written, or --validate with no errors)
  1  one or more validation errors found
  2  internal error (bad paths, I/O failure)

Usage:
  python3 build/mkindex.py                 # validate + write index
  python3 build/mkindex.py --validate      # validate only, no write
  python3 build/mkindex.py --check         # fail if index would change (CI)
  python3 build/mkindex.py --verbose       # show per-package detail
"""

import argparse
import sys
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

BUILD_DIR  = Path(__file__).parent.resolve()
ROOT_DIR   = BUILD_DIR.parent
PKG_DIR    = ROOT_DIR / "packages"
INDEX_PATH = PKG_DIR / "packages.idx"

# ---------------------------------------------------------------------------
# Spec constants (must match packages/README.md)
# ---------------------------------------------------------------------------

VALID_CATEGORIES: set[str] = {
    "wordprocessing", "spreadsheets", "databases", "programming",
    "communications", "utilities", "game",
}

VALID_LICENSES: set[str] = {
    "freeware", "shareware", "commercial", "gpl", "bsd", "mit",
    "public domain",
}

VALID_EXTRACTORS: set[str] = {"pkzip", "unzip", "unarj", "lha", "copy"}

VALID_CPU_PRESETS: set[str] = {
    "SLOW477", "SLOW10", "SLOW33", "SLOW66", "SLOW100", "OFF",
}

VALID_MEM_PROFILES: set[str] = {"STD", "MAX", "EMS", "GAME"}

# (field_name, max_chars) — 0 means no limit enforced here
FIELD_LIMITS: dict[str, int] = {
    "id": 8, "name": 40, "version": 12, "category": 16,
    "description": 60, "author": 40, "year": 4, "license": 12,
    "requires": 80, "tags": 80,
    "url1": 100, "url2": 100, "url3": 100, "archive": 12,
    "md5": 32,
    "install_dir": 64, "extractor": 8,
    "launch_label": 20, "launch_exec": 64, "launch_dir": 64,
    "game_cpu_preset": 10, "game_mem_profile": 4,
    "game_sound_env": 80, "game_music_ext": 4, "game_notes": 200,
}

INDEX_MAGIC = "#NPKG-INDEX-1.0"
FIELD_SEP   = "\t"

# Batch-line sections — each non-blank line is a command, not KEY=VALUE
BATCH_SECTIONS: set[str] = {"POST-INSTALL", "REMOVE"}

# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def parse_npkg(path: Path) -> tuple[dict, list[str]]:
    """Parse a .npkg file into a data dict.  Returns (data, parse_warnings).

    data layout:
      data["SECTION"]["key"] = value        (most keys)
      data["SECTION"]["setvar"] = [...]     (SetVar — multiple allowed)
      data["POST-INSTALL"]["_lines"] = [...] (raw batch lines)
      data["REMOVE"]["_lines"] = [...]
    """
    data: dict[str, dict] = {}
    warnings: list[str] = []
    current: Optional[str] = None

    try:
        text = path.read_text(encoding="ascii", errors="replace")
    except OSError as exc:
        return {}, [f"cannot read file: {exc}"]

    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip("\r\n").strip()

        # Blank line or comment
        if not line or line.startswith(";") or line.startswith("#"):
            continue

        # Section header
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1].upper()
            if current not in data:
                data[current] = {}
            continue

        if current is None:
            warnings.append(f"line {lineno}: key=value before any section — skipped")
            continue

        # Batch-line sections: store raw commands
        if current in BATCH_SECTIONS:
            data[current].setdefault("_lines", []).append(line)
            continue

        # KEY=VALUE
        if "=" not in line:
            warnings.append(f"line {lineno}: no '=' in non-batch section — skipped")
            continue

        key, _, value = line.partition("=")
        key   = key.strip().lower()
        value = value.strip()

        # SetVar may repeat
        if key == "setvar":
            data[current].setdefault("setvar", []).append(value)
        else:
            data[current][key] = value

    return data, warnings


# ---------------------------------------------------------------------------
# Validator
# ---------------------------------------------------------------------------

def validate_npkg(data: dict, path: Path) -> list[str]:
    """Validate parsed data against the spec.  Returns list of error strings."""
    errors: list[str] = []
    pkg = path.stem.upper()  # filename basename, e.g. "DOOM"

    def require_section(sect: str) -> bool:
        if sect not in data:
            errors.append(f"{pkg}: missing required section [{sect}]")
            return False
        return True

    def require_key(sect: str, key: str) -> Optional[str]:
        val = data.get(sect, {}).get(key.lower(), "").strip()
        if not val:
            errors.append(f"{pkg}: [{sect}] missing required key '{key}'")
            return None
        return val

    def check_len(sect: str, key: str, max_len: int) -> None:
        val = data.get(sect, {}).get(key.lower(), "")
        if isinstance(val, str) and len(val) > max_len:
            errors.append(
                f"{pkg}: [{sect}] {key} too long ({len(val)} > {max_len} chars)"
            )

    # ---- Required sections ----
    if not require_section("PACKAGE"):
        return errors   # can't validate further without PACKAGE
    require_section("SOURCE")
    require_section("INSTALL")

    # ---- [PACKAGE] required keys ----
    pkg_id   = require_key("PACKAGE", "ID")
    pkg_name = require_key("PACKAGE", "Name")
    pkg_ver  = require_key("PACKAGE", "Version")
    pkg_cat  = require_key("PACKAGE", "Category")
    pkg_desc = require_key("PACKAGE", "Description")
    pkg_lic  = require_key("PACKAGE", "License")

    # ---- ID matches filename ----
    if pkg_id and pkg_id.upper() != pkg:
        errors.append(
            f"{pkg}: ID='{pkg_id}' does not match filename '{path.name}'"
        )

    # ---- ID charset: [A-Z0-9_] only ----
    if pkg_id:
        bad_chars = [c for c in pkg_id if not (c.isalnum() or c == "_")]
        if bad_chars:
            errors.append(f"{pkg}: ID contains invalid chars: {bad_chars}")

    # ---- Category ----
    if pkg_cat and pkg_cat.lower() not in VALID_CATEGORIES:
        errors.append(f"{pkg}: unknown Category '{pkg_cat}'")

    # ---- Category matches directory ----
    if pkg_cat:
        parent_dir = path.parent.name.lower()
        # "game" category lives in "games/" directory
        expected_dir = "games" if pkg_cat.lower() == "game" else pkg_cat.lower()
        if parent_dir != expected_dir:
            errors.append(
                f"{pkg}: Category '{pkg_cat}' but file is in '{path.parent.name}/'"
            )

    # ---- License ----
    if pkg_lic and pkg_lic.lower() not in VALID_LICENSES:
        errors.append(f"{pkg}: unknown License '{pkg_lic}'")

    # ---- [SOURCE] ----
    require_key("SOURCE", "URL1")
    require_key("SOURCE", "Archive")

    # ---- Archive must be 8.3 ----
    arch = data.get("SOURCE", {}).get("archive", "")
    if arch:
        parts = arch.split(".")
        if len(parts) != 2 or len(parts[0]) > 8 or len(parts[1]) > 3:
            errors.append(f"{pkg}: Archive '{arch}' is not a valid 8.3 filename")

    # ---- Bytes must be a positive integer if present ----
    bytes_str = data.get("SOURCE", {}).get("bytes", "")
    if bytes_str:
        try:
            b = int(bytes_str)
            if b <= 0:
                errors.append(f"{pkg}: [SOURCE] Bytes must be positive")
        except ValueError:
            errors.append(f"{pkg}: [SOURCE] Bytes is not an integer: '{bytes_str}'")

    # ---- MD5 must be 32 hex chars if present ----
    md5 = data.get("SOURCE", {}).get("md5", "")
    if md5 and len(md5) != 32:
        errors.append(f"{pkg}: [SOURCE] MD5 must be exactly 32 hex characters")

    # ---- [INSTALL] ----
    require_key("INSTALL", "InstallDir")

    ext = data.get("INSTALL", {}).get("extractor", "unzip").lower()
    if ext not in VALID_EXTRACTORS:
        errors.append(f"{pkg}: [INSTALL] unknown Extractor '{ext}'")

    # ---- [POST-INSTALL] and [REMOVE] line lengths ----
    for sect in ("POST-INSTALL", "REMOVE"):
        for i, batch_line in enumerate(data.get(sect, {}).get("_lines", []), 1):
            if len(batch_line) > 127:
                errors.append(
                    f"{pkg}: [{sect}] line {i} exceeds 127 chars ({len(batch_line)})"
                )

    # ---- [GAME] ----
    if "GAME" in data:
        cpu = data["GAME"].get("cpupreset", "")
        if cpu and cpu.upper() not in VALID_CPU_PRESETS:
            errors.append(f"{pkg}: [GAME] unknown CPUPreset '{cpu}'")
        mem = data["GAME"].get("memprofile", "")
        if mem and mem.upper() not in VALID_MEM_PROFILES:
            errors.append(f"{pkg}: [GAME] unknown MemProfile '{mem}'")

    # ---- Field length checks ----
    limit_map = {
        ("PACKAGE", "id"): 8,   ("PACKAGE", "name"): 40,
        ("PACKAGE", "version"): 12, ("PACKAGE", "category"): 16,
        ("PACKAGE", "description"): 60, ("PACKAGE", "license"): 12,
        ("SOURCE", "url1"): 100, ("SOURCE", "url2"): 100,
        ("SOURCE", "url3"): 100,
        ("INSTALL", "installdir"): 64,
    }
    for (sect, key), maxlen in limit_map.items():
        check_len(sect, key, maxlen)

    return errors


# ---------------------------------------------------------------------------
# Index entry extraction
# ---------------------------------------------------------------------------

def entry_from_data(data: dict) -> dict:
    """Extract the fields needed for packages.idx from parsed data."""
    pkg_sect  = data.get("PACKAGE", {})
    src_sect  = data.get("SOURCE", {})
    bytes_raw = src_sect.get("bytes", "0") or "0"
    try:
        size_kb = max(0, int(bytes_raw)) // 1024
    except ValueError:
        size_kb = 0

    return {
        "id":          pkg_sect.get("id",          "").upper(),
        "category":    pkg_sect.get("category",    "").lower(),
        "name":        pkg_sect.get("name",         ""),
        "version":     pkg_sect.get("version",      ""),
        "description": pkg_sect.get("description",  ""),
        "size_kb":     size_kb,
        "license":     pkg_sect.get("license",      "").lower(),
    }


# ---------------------------------------------------------------------------
# Index writer
# ---------------------------------------------------------------------------

def build_index_text(entries: list[dict]) -> str:
    """Render entries as packages.idx content (CRLF line endings)."""
    sorted_entries = sorted(entries, key=lambda e: e["id"].upper())
    lines = [INDEX_MAGIC]
    for e in sorted_entries:
        row = FIELD_SEP.join([
            e["id"], e["category"], e["name"], e["version"],
            e["description"], str(e["size_kb"]), e["license"],
        ])
        lines.append(row)
    return "\r\n".join(lines) + "\r\n"


def write_index(entries: list[dict], out_path: Path) -> None:
    """Write packages.idx from entries list."""
    out_path.write_text(build_index_text(entries), encoding="ascii")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def log(msg: str) -> None:
    print(f"[mkindex] {msg}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate packages/packages.idx from .npkg definitions."
    )
    parser.add_argument(
        "--validate", action="store_true",
        help="Validate all .npkg files but do not write packages.idx",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="Fail if packages.idx would differ from current content (CI mode)",
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="Print per-package detail",
    )
    args = parser.parse_args()

    # ---- Discover all .npkg files ----
    npkg_files = sorted(PKG_DIR.rglob("*.NPKG"))
    # Also match lowercase extension on case-sensitive filesystems
    npkg_files += [p for p in PKG_DIR.rglob("*.npkg") if p not in npkg_files]
    npkg_files = sorted(set(npkg_files))

    if not npkg_files:
        log(f"ERROR: no .NPKG files found under {PKG_DIR}")
        return 2

    log(f"Found {len(npkg_files)} .NPKG file(s) in {PKG_DIR.relative_to(ROOT_DIR)}")

    # ---- Parse and validate ----
    all_errors:   list[str] = []
    all_warnings: list[str] = []
    entries:      list[dict] = []

    for npkg_path in npkg_files:
        rel = npkg_path.relative_to(ROOT_DIR)
        data, parse_warnings = parse_npkg(npkg_path)
        errors = validate_npkg(data, npkg_path)

        if args.verbose:
            status = "OK" if not errors else f"{len(errors)} error(s)"
            log(f"  {rel}  [{status}]")

        for w in parse_warnings:
            all_warnings.append(f"{rel}: {w}")
        for e in errors:
            all_errors.append(e)

        if not errors:
            entries.append(entry_from_data(data))

    # ---- Report ----
    if all_warnings:
        log(f"{len(all_warnings)} parse warning(s):")
        for w in all_warnings:
            log(f"  WARN  {w}")

    if all_errors:
        log(f"{len(all_errors)} validation error(s):")
        for e in all_errors:
            log(f"  ERR   {e}")
    else:
        log("All .NPKG files valid.")

    # ---- Early exit for --validate ----
    if args.validate:
        return 1 if all_errors else 0

    # ---- Generate index text ----
    index_text = build_index_text(entries)

    # ---- --check mode: compare to existing file ----
    if args.check:
        if INDEX_PATH.exists():
            current = INDEX_PATH.read_text(encoding="ascii", errors="replace")
            if current == index_text:
                log("packages.idx is up to date.")
                return 1 if all_errors else 0
            else:
                log("ERROR: packages.idx is stale — run 'python3 build/mkindex.py' and commit.")
                return 1
        else:
            log("ERROR: packages.idx does not exist — run 'python3 build/mkindex.py' and commit.")
            return 1

    # ---- Write index ----
    if all_errors:
        log(f"Writing index for {len(entries)} valid package(s) "
            f"({len(npkg_files) - len(entries)} skipped due to errors).")
    write_index(entries, INDEX_PATH)
    log(f"Wrote {INDEX_PATH.relative_to(ROOT_DIR)} ({len(entries)} entries).")

    return 1 if all_errors else 0


if __name__ == "__main__":
    sys.exit(main())
