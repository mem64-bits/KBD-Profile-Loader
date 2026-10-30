#!/usr/bin/env python3
"""
sync_cmake.py
Automatically scans project source, header, and test directories and syncs them
into CMakeLists.txt for both the main daemon and the GoogleTest suite.
"""

import os
import re
import sys
from pathlib import Path

# Supported C/C++ extensions
SOURCE_EXTENSIONS = {".cpp", ".c", ".cc", ".cxx"}
HEADER_EXTENSIONS = {".h", ".hpp", ".hxx"}
ALL_EXTENSIONS = SOURCE_EXTENSIONS | HEADER_EXTENSIONS

# Code that needs a window / GPU (ImGui + GLFW) or the program entry point is not
# linked into the unit-test executable. Everything else in src/ and headers/ is.
TEST_EXCLUDED_PREFIXES = (
    "src/main/",
    "src/ui/imgui/",
    "headers/ui/imgui/",
)


def find_files(base_dir: Path, target_subdirs: list[str]) -> list[str]:
    """Finds all code files in specified subdirectories relative to base_dir."""
    found = []
    for subdir in target_subdirs:
        folder = base_dir / subdir
        if not folder.exists():
            continue
        for root, _, files in os.walk(folder):
            for file in sorted(files):
                p = Path(root) / file
                if p.suffix.lower() in ALL_EXTENSIONS:
                    # Normalize to CMake-friendly forward slashes
                    rel_path = p.relative_to(base_dir).as_posix()
                    found.append(rel_path)
    return sorted(found)


def categorize_and_format_main(files: list[str]) -> str:
    """Groups daemon files by folder category with clean CMake indentation."""
    # Order matters: the first matching prefix wins, so the more specific
    # folders (src/ui/imgui, src/ui, ...) must come before their parents.
    categories = [
        ("src/main", []),
        ("src/kbd", []),
        ("src/files", []),
        ("src/ui/imgui", []),
        ("src/ui", []),
        ("src", []),
        ("headers/dsa", []),
        ("headers/files", []),
        ("headers/kbd", []),
        ("headers/ui/imgui", []),
        ("headers/ui", []),
        ("headers", []),
    ]
    other_files = []

    for f in sorted(files):
        matched = False
        for prefix, group in categories:
            if f.startswith(prefix + "/") or (
                    prefix in ("src", "headers") and f.count("/") == 1
            ):
                group.append(f)
                matched = True
                break
        if not matched:
            other_files.append(f)

    output_lines = []
    for prefix, group in categories:
        if group:
            output_lines.append(f"        # --- {prefix} ---")
            for item in group:
                output_lines.append(f"        {item}")
            output_lines.append("")

    if other_files:
        output_lines.append("        # --- other ---")
        for item in other_files:
            output_lines.append(f"        {item}")
        output_lines.append("")

    return "\n".join(output_lines).rstrip()


def format_test_target_sources(test_files: list[str], daemon_files: list[str]) -> str:
    """Formats KBD_Unit_Tests with all test suites and the daemon code they exercise."""
    output_lines = []

    # 1. Test Suite Files (tests/)
    output_lines.append("        # --- Test Suites ---")
    for f in sorted(test_files):
        output_lines.append(f"        {f}")
    output_lines.append("")

    # 2. Tested Implementations (everything that does not need a window or GPU)
    testable = [f for f in daemon_files if not f.startswith(TEST_EXCLUDED_PREFIXES)]
    tested_sources = [f for f in testable if Path(f).suffix.lower() in SOURCE_EXTENSIONS]
    tested_headers = [f for f in testable if Path(f).suffix.lower() in HEADER_EXTENSIONS]

    if tested_sources:
        output_lines.append("        # --- Tested Implementations ---")
        for f in tested_sources:
            output_lines.append(f"        {f}")
        output_lines.append("")

    # 3. Headers of the tested code (listed so IDEs show them in the target)
    if tested_headers:
        output_lines.append("        # --- Tested Headers ---")
        for h in tested_headers:
            output_lines.append(f"        {h}")

    return "\n".join(output_lines).rstrip()


def update_target_sources(
        cmake_path: Path, target_name: str, new_sources_block: str
) -> bool:
    """Replaces the source block inside add_executable(target_name ...) with new_sources_block."""
    if not cmake_path.exists():
        print(f"[!] Error: {cmake_path} not found.")
        return False

    content = cmake_path.read_text(encoding="utf-8")

    # Regex matching: add_executable(TARGET_NAME <files> )
    pattern = rf"(add_executable\s*\(\s*{re.escape(target_name)}\s*\n)([\s\S]*?)(\n\s*\))"
    match = re.search(pattern, content)

    if not match:
        print(f"[!] Warning: Could not find add_executable({target_name} ...) block in CMakeLists.txt.")
        return False

    current_block = match.group(2).strip()
    if current_block == new_sources_block.strip():
        print(f"[*] {target_name} is already up to date.")
        return False

    # Replace block
    updated_content = (
            content[: match.start(2)]
            + new_sources_block
            + content[match.end(2) :]
    )
    cmake_path.write_text(updated_content, encoding="utf-8")
    print(f"[+] Updated {target_name} in {cmake_path.name}")
    return True


def main():
    project_root = Path(__file__).resolve().parent
    cmake_file = project_root / "CMakeLists.txt"

    print(f"========================================================")
    print(f" CMakeLists.txt Dynamic Synchronization Utility")
    print(f" Root: {project_root}")
    print(f"========================================================")

    # 1. Backup CMakeLists.txt
    backup_file = project_root / "CMakeLists.txt.bak"
    backup_file.write_text(cmake_file.read_text(encoding="utf-8"), encoding="utf-8")

    # 2. Sync KBD_Profile_Loader (Main Daemon)
    daemon_files = find_files(project_root, ["src", "headers"])
    print(f"[*] Discovered {len(daemon_files)} file(s) for KBD_Profile_Loader.")
    daemon_block = categorize_and_format_main(daemon_files)
    updated_daemon = update_target_sources(cmake_file, "KBD_Profile_Loader", daemon_block)

    # 3. Sync KBD_Unit_Tests (GoogleTest Suite)
    test_files = find_files(project_root, ["tests"])
    print(f"[*] Discovered {len(test_files)} test suite(s) in tests/.")
    test_block = format_test_target_sources(test_files, daemon_files)
    updated_tests = update_target_sources(cmake_file, "KBD_Unit_Tests", test_block)

    print(f"--------------------------------------------------------")
    if updated_daemon or updated_tests:
        print(f"[SUCCESS] CMakeLists.txt updated. In CLion:")
        print(f"          Tools > CMake > Reload CMake Project")
    else:
        print(f"[*] All targets are already synchronized.")
    print(f"========================================================")


if __name__ == "__main__":
    main()
