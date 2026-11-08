"""Copies the newest crash out of a build and symbolizes its dump.

    python Scripts/symbolize-crash.py                    # the newest crash under out/builds
    python Scripts/symbolize-crash.py --crash <folder>    # one crash folder, or one that holds several
    python Scripts/symbolize-crash.py --pdb <Weltall.pdb> # symbols from elsewhere, e.g. a release's -symbols.zip
    python Scripts/symbolize-crash.py --copy-only         # take the crash out, resolve nothing

A packaged game writes each crash inside the package, in Weltall/Saved/Crashes/UECC-<id>: the minidump,
the crash context (an XML with the exception, the modules and every thread's call stack) and a copy of
the log. A rebuild replaces that package, so this copies the crash to out/crashes/<name> first, and only
then looks for the symbols the build left in out/package/Weltall/Binaries: a package ships no debug files.

It runs cdb from the Windows Debugging Tools over the dump and writes Symbols.txt beside it, holding the
exception, the registers, the faulting stack and every frame of the crash context's own call stack
resolved to a function. Without cdb, or with the symbols gone, the copy still happens and the script
says what is missing; the dump keeps its value only until the build's own symbols are replaced.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BINARIES = REPO / "out" / "package" / "Weltall" / "Binaries" / "Win64"


def crashes(folders):
    """Every crash folder under the given roots, newest first."""
    found = []
    for root in folders:
        if not root.is_dir():
            continue
        for path in root.glob("**/UECC-*"):
            if path.is_dir() and any(path.glob("*.dmp")):
                found.append(path)
    return sorted(found, key=lambda path: path.stat().st_mtime, reverse=True)


def copy_out(source, target):
    """Copies the crash to the target folder, keeping what an earlier run already took."""
    target.mkdir(parents=True, exist_ok=True)
    for path in source.iterdir():
        if path.is_file():
            destination = target / path.name
            if not destination.exists() or destination.stat().st_size != path.stat().st_size:
                shutil.copy2(path, destination)
    return target


def cdb_path():
    """The debugging engine the Windows SDK installs, newest first, or None."""
    override = os.environ.get("CDB")
    if override and Path(override).is_file():
        return Path(override)
    roots = [Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits",
             Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Windows Kits"]
    found = []
    for root in roots:
        if root.is_dir():
            found += list(root.glob("*/Debuggers/x64/cdb.exe"))
    return max(found, key=lambda path: path.stat().st_mtime) if found else None


def properties(crash):
    """The crash context's runtime properties, or nothing when it holds no readable XML."""
    # The context is named CrashContext.runtime-xml: it ends in -xml, not .xml.
    context = next(iter(sorted(crash.glob("CrashContext*"))), None)
    if context is None or not context.is_file():
        return {}, []
    try:
        root = ElementTree.parse(context).getroot()
    except ElementTree.ParseError:
        return {}, []
    values = {}
    for node in root.iter():
        if node.text and node.text.strip() and len(node) == 0 and node.tag in {
                "ErrorMessage", "CrashType", "BuildConfiguration", "EngineVersion", "SecondsSinceStart", "IsAssert", "IsEnsure"}:
            values[node.tag] = node.text.strip()
    # The crashed thread's own frames, as module and offset: the debugger resolves them to functions.
    frames = []
    for node in root.iter("PCallStack"):
        for line in (node.text or "").splitlines():
            match = re.match(r"\s*(\S+)\s+0x([0-9a-fA-F]+)\s+\+\s*([0-9a-fA-F]+)\s*$", line)
            if match:
                frames.append(int(match.group(2), 16) + int(match.group(3), 16))
        break
    return values, frames


def symbol_commands(frames, path):
    """The debugger's script: the exception, the registers, the faulting stack and the context's frames."""
    lines = [".lines", ".reload /f", ".echo ### EXCEPTION", ".exr -1", ".ecxr",
             ".echo ### REGISTERS", "r", ".echo ### STACK", "kb"]
    if frames:
        lines.append(".echo ### FRAMES")
        lines += [f"ln 0x{address:016x}" for address in frames]
    lines.append("q")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def sections(text):
    """The debugger's output, split at the script's own ### markers."""
    kept = {}
    current = None
    for line in text.splitlines():
        bare = re.sub(r"^\d+:\d+> ", "", line).rstrip()
        if bare.startswith("NatVis ") or bare in ("quit:", "q"):
            continue
        if re.search(r"\.echo ###", bare):
            continue
        marker = re.fullmatch(r"###\s+(\w+)", bare.strip())
        if marker:
            current = marker.group(1)
            kept[current] = []
        elif current is not None:
            kept[current].append(line.rstrip())
    return kept


def symbolize(cdb, dump, symbols, frames):
    """cdb over the dump, returning its output split into sections."""
    with tempfile.TemporaryDirectory() as scratch:
        commands = Path(scratch) / "commands.txt"
        symbol_commands(frames, commands)
        result = subprocess.run([str(cdb), "-z", str(dump), "-y", str(symbols.parent), "-lines", "-cf", str(commands)],
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600)
    return sections(result.stdout + result.stderr)


def summary(values, crash):
    """What the crash was, in a few lines."""
    lines = [f"crash: {crash}"]
    if values.get("ErrorMessage"):
        lines.append(f"  {values['ErrorMessage']}")
    if values.get("BuildConfiguration") or values.get("EngineVersion"):
        lines.append(f"  {values.get('BuildConfiguration', '?')} build of {values.get('EngineVersion', '?')}")
    if values.get("SecondsSinceStart"):
        lines.append(f"  {int(values['SecondsSinceStart']) // 60} min {int(values['SecondsSinceStart']) % 60} s into the run")
    return lines


def main(argv):
    parser = argparse.ArgumentParser(description="Copy the newest crash out of a build and symbolize its dump.")
    parser.add_argument("--crash", type=Path, help="a crash folder, or a folder that holds crashes")
    parser.add_argument("--pdb", type=Path, help="the symbols to resolve with (default: the build's own)")
    parser.add_argument("--out", type=Path, default=REPO / "out" / "crashes", help="where the crash is copied")
    parser.add_argument("--copy-only", action="store_true", help="take the crash out, resolve nothing")
    arguments = parser.parse_args(argv)

    if arguments.crash is not None:
        roots = [arguments.crash]
    else:
        roots = [REPO / "out" / "builds", REPO / "out" / "crashes"]
    found = crashes(roots)
    if not found:
        print(f"crash: no crash with a dump under {', '.join(str(root) for root in roots)}")
        return 1

    source = found[0]
    crash = copy_out(source, arguments.out / source.name)
    values, frames = properties(crash)
    for line in summary(values, source):
        print(line)
    print(f"copied: {crash.relative_to(REPO) if crash.is_relative_to(REPO) else crash}")

    dump = next(iter(sorted(crash.glob("*.dmp"))))
    if arguments.copy_only:
        return 0
    symbols = arguments.pdb or BINARIES / "Weltall.pdb"
    engine = cdb_path()
    if not symbols.is_file():
        print(f"symbols: none: {symbols} is missing (a build since the crash replaces it)")
        return 0
    if not engine:
        print("symbols: none: cdb.exe not found; set CDB or install the Windows Debugging Tools")
        return 0
    if symbols.stat().st_mtime > dump.stat().st_mtime + 60:
        print(f"symbols: {symbols.name} is newer than the crash: a build happened since, so its functions may be wrong")
    else:
        print(f"symbols: {symbols}")

    resolved = symbolize(engine, dump, symbols, frames)
    if not resolved:
        print("symbols: the debugger printed nothing")
        return 1
    report = crash / "Symbols.txt"
    with report.open("w", encoding="utf-8") as out:
        out.write("The crash, symbolized. The dump beside this file is only worth what these symbols made it.\n")
        out.write("Re-run Scripts/symbolize-crash.py --crash <folder> --pdb <Weltall.pdb> to resolve it again.\n\n")
        out.write("\n".join(summary(values, source)) + "\n")
        out.write(f"symbols: {symbols}\n\n")
        for name in ("EXCEPTION", "REGISTERS", "STACK", "FRAMES"):
            if resolved.get(name):
                out.write(f"=== {name}\n" + "\n".join(resolved[name]).rstrip() + "\n\n")
    print(f"wrote: {report.relative_to(REPO) if report.is_relative_to(REPO) else report}")
    for line in resolved.get("STACK", []):
        if "!" in line:
            print("  " + line.split(" : ")[-1].strip())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
