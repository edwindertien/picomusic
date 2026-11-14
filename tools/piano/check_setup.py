#!/usr/bin/env python3
"""Checks everything the bank tool and its GUI need, and says what to install for what is missing.   python3 tools/check_setup.py"""
import sys, os, shutil, subprocess, tempfile, importlib
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = HERE if os.path.isdir(os.path.join(HERE, "banks")) else os.path.normpath(os.path.join(HERE, ".."))      # picomusic: tools/piano holds samples/ and banks/; flat project: the parent
sys.path.insert(0, HERE)
ok_all = True; mac = sys.platform == "darwin"
def line(ok, what, detail="", fix=""):
    global ok_all; ok_all &= bool(ok)
    print(("  [ok]   " if ok else "  [FAIL] ") + what + (f"  ({detail})" if detail else ""))
    if not ok and fix: print("         -> " + fix.replace("\n", "\n            "))
def run(cmd, **kw):
    try: return subprocess.run(cmd, capture_output=True, text=True, timeout=120, **kw)
    except (OSError, subprocess.SubprocessError) as e: return type("R", (), dict(returncode=1, stdout="", stderr=str(e)))()
print(f"picopiano setup check   (Python {sys.version.split()[0]} on {sys.platform})\n")
print("Python")
line(sys.version_info >= (3, 8), "Python 3.8 or newer", sys.version.split()[0], "install a newer Python (python.org or `brew install python`)")
for mod, pipname in (("flask", "flask"), ("yaml", "pyyaml"), ("numpy", "numpy")):
    try:
        importlib.import_module(mod)
        try: from importlib.metadata import version; ver = version(pipname)
        except Exception: ver = ""
        line(True, f"python package {pipname}", ver)
    except ImportError: line(False, f"python package {pipname}", "not installed", f"{sys.executable} -m pip install -r tools/requirements.txt")
print("\nAudio tools")
ff = shutil.which("ffmpeg"); fp = shutil.which("ffprobe")
line(bool(ff and fp), "ffmpeg and ffprobe on the PATH", ff or "not found", "macOS: brew install ffmpeg      Linux: sudo apt install ffmpeg")
if ff:
    r = run([ff, "-v", "error", "-f", "lavfi", "-i", "sine=f=440:d=0.1", "-af", "aresample=resampler=soxr:precision=28", "-ar", "24000", "-f", "null", "-"])
    line(r.returncode == 0, "ffmpeg has the soxr resampler (the tool's high-quality sample-rate conversion)", (r.stderr.strip().splitlines() or [""])[0][:90], "macOS: brew install ffmpeg (the Homebrew build includes soxr)      Linux: use your distribution's full ffmpeg")
    r = run([ff, "-hide_banner", "-decoders"]); line(r.returncode == 0 and ("mp3" in r.stdout), "ffmpeg can decode MP3 (the Salamander samples are MP3) and FLAC (the Rhodes and clavinet are FLAC)", "", "install a full ffmpeg build (brew install ffmpeg)")
print("\nC++ compiler (builds the ADPCM encoder and the audition engine)")
try:
    from bankconf import core
    cands = core.cxx_candidates(); line(bool(cands), "a C++ compiler is installed", ", ".join(os.path.basename(c) for c in cands) or "none found", "macOS: xcode-select --install      Linux: sudo apt install build-essential")
    if cands:
        d = tempfile.mkdtemp(prefix="pp_check_"); src = os.path.join(d, "t.cpp"); exe = os.path.join(d, "t")
        open(src, "w").write('#include <cstdio>\n#include <cstdlib>\n#include <cstring>\n#include <cmath>\n#include <string>\n#include <vector>\n#include <algorithm>\n#include <cstdint>\nint main() { std::vector<int> v{3, 1, 2}; std::sort(v.begin(), v.end()); std::string s = "ok"; std::printf("%s %d\\n", s.c_str(), v[0] + (int)std::sqrt(4.0)); return 0; }\n')
        try: used = core.compile_cpp([src], exe, what="the setup check"); r = run([exe]); line(r.returncode == 0 and r.stdout.startswith("ok"), "it compiles and runs a C++17 program that uses <cstdio>, <vector>, <string>, <algorithm>, <cmath>", os.path.basename(used))
        except core.BankError as e: line(False, "it compiles a C++17 program that uses the standard library headers", "", str(e).replace(core.SETUP_HELP, "") + core.SETUP_HELP)
        try: exe2 = core.ensure_adpcm_tool(); line(True, "tools/adpcm_tool.cpp compiles", os.path.basename(exe2))
        except core.BankError as e: line(False, "tools/adpcm_tool.cpp compiles", "", str(e).splitlines()[0])
except ImportError as e: line(False, "the bank tool itself imports", str(e), "run this script from the project (python3 tools/check_setup.py) after installing the packages above")
def diagnose_macos():
    """when the compiler cannot find the standard library headers on a Mac: look at the usual suspects and say which one it is"""
    print("\n  macOS toolchain diagnosis (what the compiler can see):")
    def sh(cmd): r = run(cmd); return (r.stdout + r.stderr).strip()
    dev = sh(["xcode-select", "-p"]); print(f"    xcode-select -p            : {dev or '(nothing: no developer directory is selected)'}")
    sdk = sh(["xcrun", "--show-sdk-path"]); print(f"    xcrun --show-sdk-path      : {sdk}")
    c = sh(["xcrun", "--find", "clang++"]); print(f"    xcrun --find clang++       : {c}")
    allcpp = sh(["which", "-a", "c++"]).splitlines(); print(f"    which -a c++               : {', '.join(allcpp) or 'none'}")
    print(f"    c++ --version              : {(sh(['c++', '--version']).splitlines() or ['?'])[0]}")
    env = {k: os.environ[k] for k in ("SDKROOT", "CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "CXXFLAGS", "CPPFLAGS", "CXX", "CC", "LIBRARY_PATH", "DEVELOPER_DIR") if k in os.environ}
    print(f"    environment overrides      : {env or 'none'}")
    roots = [p for p in (sdk, dev and os.path.join(dev, "SDKs", "MacOSX.sdk")) if p]
    found = []
    for base in roots + [dev or "", (dev or "") and os.path.join(dev, "usr")]:
        for sub in ("usr/include/c++/v1", "include/c++/v1"):
            q = os.path.join(base, sub) if base else ""
            if q and os.path.isfile(os.path.join(q, "cstdio")): found.append(q)
    print(f"    libc++ headers (cstdio)    : {found[0] if found else 'NOT FOUND in the SDK or the developer directory'}")
    sdks = os.path.join(dev, "SDKs") if dev else ""
    print(f"    SDKs installed             : {', '.join(sorted(os.listdir(sdks))) if sdks and os.path.isdir(sdks) else 'none'}")
    print("\n  Conclusion:")
    bad_env = [k for k in ("SDKROOT", "CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "DEVELOPER_DIR") if k in env]
    other = [a for a in allcpp if not a.startswith("/usr/bin")]
    if bad_env: print(f"    * these environment variables change where the compiler looks: {', '.join(bad_env)}. Try again after `unset {' '.join(bad_env)}`.")
    if other: print(f"    * a different c++ comes first on your PATH ({other[0]}), e.g. a Homebrew LLVM without the macOS SDK configured. Try  /usr/bin/c++  directly:  CXX=/usr/bin/clang++ python3 tools/check_setup.py")
    if not found: print("    * the headers are missing from the selected SDK: the Command Line Tools are incomplete (usual after a macOS update). Reinstall them:\n"
                        "          sudo rm -rf /Library/Developer/CommandLineTools\n          xcode-select --install\n"
                        "      (a window asks you to confirm; wait for it to finish, then run this check again). If you have the full Xcode app, you can instead select it:\n"
                        "          sudo xcode-select -s /Applications/Xcode.app/Contents/Developer\n"
                        "      Alternative without Xcode tools: brew install gcc   then   CXX=g++-14 python3 tools/check_setup.py   (use the version number brew installed)")
    elif not bad_env and not other: print("    * the headers exist, so the compiler is being pointed somewhere else: run  c++ -v -x c++ -E /dev/null  and send me the 'search starts here' part.")

if sys.platform == "darwin" and not ok_all:
    diagnose_macos()
print("\nOptional")
print("  [info] git:", shutil.which("git") or "not found (needed by tools/piano/fetch_*.sh to download the sample sets)")
print("  [info] node:", shutil.which("node") or "not found (only the project's own test suite needs it)")
for name in ("salamander", "jRhodes3c", "stevie-clavinet"): print(f"  [info] samples/{name}:", "present" if os.path.isdir(os.path.join(ROOT, "samples", name)) else f"not downloaded (tools/piano/fetch_{ {'salamander': 'salamander', 'jRhodes3c': 'rhodes', 'stevie-clavinet': 'clavinet'}[name]}.sh)")
print("\n" + ("Everything the bank tool needs is in place." if ok_all else "Some things above need attention (see the lines marked [FAIL])."))
sys.exit(0 if ok_all else 1)
