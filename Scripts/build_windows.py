"""Build the Windows x64 TSF component, settings app and NSIS installer."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / ".build/windows"
PAYLOAD = WORK / "out"
SHA = "f850177bb2d256574348258f221730dbf3d358a53dbeca4bd49cdc338d927438"
URL = "https://github.com/rime/librime/releases/download/1.17.0/rime-33e7814-Windows-clang-x64.7z"


def run(command, **kwargs):
    subprocess.run([str(arg) for arg in command], check=True, cwd=ROOT, **kwargs)


def executable(name, env, candidates):
    for tool in [os.getenv(env), shutil.which(name), *candidates]:
        if tool and Path(tool).is_file():
            return Path(tool).resolve()
    raise SystemExit(f"Missing {name}. Set {env} to its full path.")


def main():
    PAYLOAD.mkdir(parents=True, exist_ok=True)
    (ROOT / "dist").mkdir(exist_ok=True)
    zig = executable("zig", "ZIG", [WORK / "tools/ziglang/zig"])
    if subprocess.check_output([zig, "version"], text=True).strip() != "0.13.0":
        raise SystemExit("Use Zig 0.13.0 for this build.")
    nsis = executable("makensis", "MAKENSIS", [
        Path(os.getenv("ProgramFiles(x86)", ".")) / "NSIS/makensis.exe",
        WORK / "makensis/3.13/bin/makensis",
    ])
    archive = WORK / "rime.7z"
    if not archive.exists():
        with urllib.request.urlopen(URL, timeout=90) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != SHA:
            raise SystemExit("Windows librime download failed checksum verification.")
        archive.write_bytes(data)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA:
        raise SystemExit(f"Unexpected librime checksum: {archive}")
    dep = WORK / "rime"
    dep.mkdir(exist_ok=True)
    run(["tar", "-xf", archive, "-C", dep])
    sdk = dep / "dist"
    flags = [zig, "c++", "-target", "x86_64-windows-gnu", "-std=c++17", "-Os"]
    run([*flags, "-I" + str(sdk / "include"), "-shared", "Windows/ime.cpp",
         "-o", PAYLOAD / "YujuIME.dll", "-lmsctf", "-lole32", "-loleaut32", "-luuid",
         "-luser32", "-lgdi32", "-lshell32", "-ladvapi32", "-lwinhttp"])
    run([*flags, "-municode", "-Wl,--subsystem,windows", "Windows/settings.cpp",
         "-o", PAYLOAD / "YujuSettings.exe", "-luser32", "-lgdi32", "-ladvapi32"])
    shutil.copy2(sdk / "lib/rime.dll", PAYLOAD / "rime.dll")
    compiled = PAYLOAD / "Rime/build"
    compiled.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        user = WORK / "rime-user"
        user.mkdir(exist_ok=True)
        env = dict(os.environ, PATH=str(sdk / "lib") + os.pathsep + os.environ["PATH"])
        run([sdk / "bin/rime_deployer.exe", "--build", user, ROOT / "Resources/Rime", compiled], env=env)
    elif sys.platform == "darwin":
        data = ROOT / "dist/Yuju.app/Contents/Resources/Rime/build"
        if not data.is_dir():
            run(["bash", "Scripts/build.sh"])
        shutil.copytree(data, compiled, dirs_exist_ok=True)
    else:
        raise SystemExit("Build on macOS or Windows.")
    # Each TSF host owns a Rime engine. Use the bundled dictionaries without opening
    # a shared writable LevelDB from several applications at once.
    schema = compiled / "yuju_pinyin.schema.yaml"
    schema.write_text(schema.read_text(encoding="utf-8").replace("translator:\n", "translator:\n  enable_user_dict: false\n"), encoding="utf-8")
    assert (compiled / "yuju.table.bin").stat().st_size > 100000
    env = os.environ.copy()
    if (nsis.parent.parent / "share/nsis").is_dir():
        env["NSISDIR"] = str(nsis.parent.parent / "share/nsis")
    switch = "/" if os.name == "nt" else "-"
    run([nsis, switch + "V2", switch + "DPAYLOAD=" + str(PAYLOAD), "Windows/setup.nsi"], env=env)
    print(ROOT / "dist/Yuju-Setup-Windows-x64.exe")


if __name__ == "__main__":
    main()
