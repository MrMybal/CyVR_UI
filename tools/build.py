"""Native CMake builds and an APK packager. No browser, Gradle plugin or game engine."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
ENV = dict(os.environ)
SDK = Path(ENV.get("ANDROID_HOME", str(Path.home() / "AppData/Local/Android/Sdk")))
NDK = SDK / "ndk/27.2.12479018"
CMAKE = SDK / "cmake/3.22.1/bin/cmake.exe"
NINJA = CMAKE.with_name("ninja.exe")
GLSLC = NDK / "shader-tools/windows-x86_64/glslc.exe"
BUILD_TOOLS = SDK / "build-tools/35.0.1"
JAVA = Path(ENV.get("JAVA_HOME", "C:/Program Files/Android/Android Studio/jbr")) / "bin"

def run(*args):
    print("[CyVR]", Path(str(args[0])).name, " ".join(str(x) for x in args[1:]), flush=True)
    subprocess.run([str(x) for x in args], cwd=ROOT, env=ENV, check=True)

def notices(destination):
    destination.mkdir(parents=True, exist_ok=True)
    sources = {"CyVR-MIT.txt": ROOT / "LICENSE", "Apache-2.0.txt": ROOT / "third_party/Apache-2.0.txt", "OpenXR.txt": ROOT / "third_party/openxr/LICENSE", "Noto-OFL.txt": ROOT / "assets/fonts/OFL.txt", "THIRD_PARTY.md": ROOT / "THIRD_PARTY.md", "stb-image-MIT.txt": ROOT / "third_party/stb/LICENSE"}
    for name, source in sources.items():
        shutil.copy2(source, destination / name)
    for source in (ROOT / "third_party/notices").glob("*"):
        if source.is_file():
            shutil.copy2(source, destination / source.name)

def ensure_dependencies():
    if not (ROOT / ".deps/openxr/prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so").is_file():
        run("powershell.exe", "-NoProfile", "-File", ROOT / "tools/fetch-deps.ps1")
    archive = ROOT / ".deps/openxr-1.1.49.zip"
    if hashlib.sha256(archive.read_bytes()).hexdigest() != "9a731db8a21bfc5c4be47c8862a508a2e4db3d7167062dc65683cedf7563f0cd":
        raise RuntimeError("OpenXR AAR checksum mismatch. Refusing to package it.")

def configure(directory, *extra):
    run(CMAKE, "-S", ROOT, "-B", directory, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_MAKE_PROGRAM={NINJA}", f"-DCYVR_GLSLC={GLSLC}", *extra)
    run(CMAKE, "--build", directory, "--parallel", "4")

def windows(tests_only=False):
    # clang uses the installed Windows SDK; it avoids the MSBuild environment entirely.
    candidates = [Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"), Path("C:/Program Files/LLVM/bin")]
    compiler = next((p for p in candidates if (p / "clang++.exe").exists()), None)
    if compiler is None:
        raise RuntimeError("Install LLVM/Clang and the Windows C++ SDK for the native desktop reference.")
    kits = Path("C:/Program Files (x86)/Windows Kits/10/bin")
    rc = next(p / "x64/rc.exe" for p in sorted(kits.glob("10.*"), reverse=True) if (p / "x64/rc.exe").exists())
    directory = ROOT / "build/windows-clang"
    configure(directory, f"-DCMAKE_C_COMPILER={compiler / 'clang.exe'}", f"-DCMAKE_CXX_COMPILER={compiler / 'clang++.exe'}", f"-DCMAKE_RC_COMPILER={rc}")
    run(CMAKE.with_name("ctest.exe"), "--test-dir", directory, "--output-on-failure")
    if not tests_only:
        output = ROOT / "dist/windows"
        output.mkdir(parents=True, exist_ok=True)
        shutil.copy2(directory / "CyVR_UI.exe", output / "CyVR_UI.exe")
        shutil.copytree(directory / "assets", output / "assets", dirs_exist_ok=True)
        notices(output / "licenses")
        print("Native reference:", output / "CyVR_UI.exe")

def android(immersive=True):
    directory = ROOT / ("build/android" if immersive else "build/android-panel")
    configure(directory, f"-DCMAKE_TOOLCHAIN_FILE={NDK / 'build/cmake/android.toolchain.cmake'}", "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-32", "-DANDROID_STL=c++_static", "-DCYVR_BUILD_TESTS=OFF", f"-DCYVR_ENABLE_IMMERSIVE={'ON' if immersive else 'OFF'}")
    notices(directory / "assets/licenses")
    source_manifest = ROOT / "android/app/src/main/AndroidManifest.xml"
    manifest = source_manifest
    if not immersive:
        ET.register_namespace("android", "http://schemas.android.com/apk/res/android")
        tree = ET.parse(source_manifest)
        document = tree.getroot()
        ns = "{http://schemas.android.com/apk/res/android}"
        for feature in document.findall("uses-feature"):
            if feature.get(ns + "name") == "android.hardware.vr.headtracking":
                feature.set(ns + "required", "false")
        application = document.find("application")
        for activity in list(application.findall("activity")):
            if activity.get(ns + "name") == ".ImmersiveActivity":
                application.remove(activity)
        for node in list(document):
            if (node.tag == "queries") or (node.tag == "uses-permission" and node.get(ns+"name") == "com.oculus.permission.HAND_TRACKING") or (node.tag == "uses-feature" and node.get(ns+"name") in ("oculus.software.handtracking", "com.oculus.feature.PASSTHROUGH")):
                document.remove(node)
        manifest = directory / "AndroidManifest.xml"
        tree.write(manifest, encoding="utf-8", xml_declaration=True)
    classes = directory / "classes"
    classes.mkdir(exist_ok=True)
    sources = list((ROOT / "android/app/src/main/java").rglob("*.java"))
    if not immersive:
        sources = [s for s in sources if s.name != "ImmersiveActivity.java"]
    run(JAVA / "javac.exe", "-encoding", "UTF-8", "-Xlint:-options", "-source", "8", "-target", "8", "-bootclasspath", SDK / "platforms/android-34/android.jar", "-d", classes, *sources)
    dex = directory / "dex"
    dex.mkdir(exist_ok=True)
    run(BUILD_TOOLS / "d8.bat", "--release", "--min-api", "32", "--lib", SDK / "platforms/android-34/android.jar", "--output", dex, *classes.rglob("*.class"))
    run(BUILD_TOOLS / "aapt2.exe", "compile", "--dir", ROOT / "android/app/src/main/res", "-o", directory / "resources.zip")
    resources = directory / "resources.apk"
    unsigned = directory / "unsigned.apk"
    run(BUILD_TOOLS / "aapt2.exe", "link", "-o", resources, "-I", SDK / "platforms/android-34/android.jar", "--manifest", manifest, "-A", directory / "assets", "-R", directory / "resources.zip", "--min-sdk-version", "32", "--target-sdk-version", "34")
    # Repack before signing: normalize ZIP local/central headers emitted by different tools.
    with zipfile.ZipFile(resources) as source, zipfile.ZipFile(unsigned, "w") as apk:
        for entry in source.infolist():
            apk.writestr(entry.filename, source.read(entry.filename), compress_type=entry.compress_type)
        apk.write(directory / "libcyvr.so", "lib/arm64-v8a/libcyvr.so")
        apk.write(dex / "classes.dex", "classes.dex")
        if immersive:
            apk.write(ROOT / ".deps/openxr/prefab/modules/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so", "lib/arm64-v8a/libopenxr_loader.so")
    aligned = directory / "aligned.apk"
    run(BUILD_TOOLS / "zipalign.exe", "-P", "16", "-f", "4", unsigned, aligned)
    cache = ROOT / ".cache"
    cache.mkdir(exist_ok=True)
    key = cache / "debug.keystore"
    if not key.exists():
        # Conventional public development credentials. This key must never sign a store release.
        run(JAVA / "keytool.exe", "-genkeypair", "-keystore", key, "-storepass", "android", "-keypass", "android", "-alias", "androiddebugkey", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-dname", "CN=CyVR Development,O=CyVR,C=FR")
    output = ROOT / ("dist/android/CyVR_UI-debug.apk" if immersive else "dist/android/CyVR_UI-panel-debug.apk")
    output.parent.mkdir(parents=True, exist_ok=True)
    signer = BUILD_TOOLS / "lib/apksigner.jar"
    run(JAVA / "java.exe", "-jar", signer, "sign", "--v2-signing-enabled", "true", "--v3-signing-enabled", "true", "--ks", key, "--ks-pass", "pass:android", "--key-pass", "pass:android", "--out", output, aligned)
    run(JAVA / "java.exe", "-jar", signer, "verify", "--verbose", output)
    run(BUILD_TOOLS / "zipalign.exe", "-c", "-P", "16", "4", output)
    print("Quest APK:", output)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=["all", "windows", "android", "tests"], default="all")
    parser.add_argument("--no-immersive", action="store_true", help="Build a native 2D APK without OpenXR")
    args = parser.parse_args()
    run("python",ROOT/"tools/generate_locales.py")
    if not args.no_immersive:
        run("python", ROOT / "tools/prepare_pbr.py", "--check")
    if not args.no_immersive:
        ensure_dependencies()
    if args.target in ("all", "windows", "tests"):
        windows(args.target == "tests")
    if args.target in ("all", "android"):
        android(not args.no_immersive)
