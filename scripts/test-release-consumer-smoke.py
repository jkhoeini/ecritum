#!/usr/bin/env python3
import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import textwrap
import hashlib
from pathlib import Path
from urllib.parse import urlparse


SUCCESS_LINE = "ReleaseConsumerSmoke version=0.1.0 clojure=42 javascript=42 lua=42"
DEFAULT_INCLUDED_RUNTIMES = ["clojure", "javascript", "lua"]
CHECKSUM_RE = re.compile(r"^[0-9a-f]{64}$")


def fail(message):
    print(message, file=sys.stderr)
    raise SystemExit(1)


def run(command, *, cwd=None, env=None):
    completed = subprocess.run(
        command,
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 0:
        fail(
            "command failed: "
            + " ".join(str(part) for part in command)
            + "\nstdout:\n"
            + completed.stdout
            + "\nstderr:\n"
            + completed.stderr
        )
    return completed.stdout


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tool(name):
    found = shutil.which(name)
    if not found:
        fail(f"missing required tool on PATH: {name}")
    return found


def build_env(artifact_url, checksum, build_dir, use_default_package_runtime=False):
    tool_dirs = []
    for name in ["swift", "otool"]:
        directory = str(Path(tool(name)).parent)
        if directory not in tool_dirs:
            tool_dirs.append(directory)
    for directory in ["/usr/bin", "/bin", "/usr/sbin", "/sbin"]:
        if directory not in tool_dirs:
            tool_dirs.append(directory)

    home = build_dir / "home"
    tmp = build_dir / "tmp"
    home.mkdir(parents=True, exist_ok=True)
    tmp.mkdir(parents=True, exist_ok=True)

    env = {
        "HOME": str(home),
        "TMPDIR": str(tmp),
        "PATH": os.pathsep.join(tool_dirs),
    }
    if use_default_package_runtime:
        env["ECRITUM_RELEASE_RUNTIME_REQUIRED"] = "1"
    else:
        env.update({
            "ECRITUM_RELEASE_RUNTIME_REQUIRED": "1",
            "ECRITUM_RUNTIME_URL": artifact_url,
            "ECRITUM_RUNTIME_CHECKSUM": checksum,
        })
    for key in ["DEVELOPER_DIR", "SDKROOT", "TOOLCHAINS"]:
        if os.environ.get(key):
            env[key] = os.environ[key]
    return env


def validate_ecritum_release_manifest(repo_root, env):
    describe = json.loads(run([tool("swift"), "package", "describe", "--type", "json"], cwd=repo_root, env=env))
    runtime_targets = [target for target in describe.get("targets", []) if target.get("name") == "EcritumRuntime"]
    if len(runtime_targets) != 1:
        fail(f"expected one EcritumRuntime binary target in Ecritum release manifest, found {len(runtime_targets)}")
    runtime_target = runtime_targets[0]
    runtime_path = runtime_target.get("path")
    archive_prefix = "remote/archive/"
    archive_name = runtime_path[len(archive_prefix):] if isinstance(runtime_path, str) and runtime_path.startswith(archive_prefix) else ""
    if (
        runtime_target.get("type") != "binary"
        or not isinstance(runtime_path, str)
        or not runtime_path.startswith(archive_prefix)
        or "/" in archive_name
        or not runtime_path.endswith(".zip")
    ):
        fail(f"EcritumRuntime did not resolve through a remote binary target: {runtime_target}")
    return runtime_target


def package_swift(repo_root, dependency_url=None, dependency_exact=None):
    if dependency_url:
        dependency = f".package(url: {json.dumps(dependency_url)}, exact: {json.dumps(dependency_exact)})"
    else:
        dependency = f".package(path: {json.dumps(str(repo_root))})"
    return textwrap.dedent(
        f"""\
        // swift-tools-version: 5.9
        import PackageDescription

        let package = Package(
            name: "ReleaseConsumerSmoke",
            platforms: [
                .macOS(.v14),
            ],
            dependencies: [
                {dependency},
            ],
            targets: [
                .executableTarget(
                    name: "ReleaseConsumerSmoke",
                    dependencies: [
                        .product(name: "Ecritum", package: "Ecritum"),
                    ]
                ),
            ]
        )
        """
    )


def main_swift():
    return textwrap.dedent(
        """\
        import Ecritum
        import Foundation

        @main
        struct ReleaseConsumerSmoke {
            static func main() async {
                do {
                    guard Ecritum.runtimeArtifactAvailable else {
                        throw SmokeFailure("runtime artifact is not available")
                    }

                    let version = try Ecritum.version
                    let runtime = try EcritumRuntime(.init(languages: [.clojure, .javascript, .lua]))
                    let namespace = try runtime.namespace(.init("app"))
                    try namespace.register(.init("answer")) { call in
                        guard try call.argumentCount() == 0 else {
                            throw SmokeFailure("answer callback expected no arguments")
                        }
                        return .int(42)
                    }
                    let context = try runtime.context()
                    defer {
                        try? context.close()
                        try? namespace.close()
                        try? runtime.close()
                    }

                    let clojure = try await context.eval(EcritumScript(
                        "(+ 40 (- (app/answer) 40))",
                        language: .clojure,
                        sourceName: "release-consumer-smoke.clj"
                    ))
                    try expectInt(clojure, equals: 42, label: "clojure")

                    let javascript = try await context.eval(EcritumScript(
                        "40 + (ecritum.app.answer() - 40)",
                        language: .javascript,
                        sourceName: "release-consumer-smoke.js"
                    ))
                    try expectInt(javascript, equals: 42, label: "javascript")

                    let lua = try await context.eval(EcritumScript(
                        "return 40 + (ecritum.app.answer() - 40)",
                        language: .lua,
                        sourceName: "release-consumer-smoke.lua"
                    ))
                    try expectInt(lua, equals: 42, label: "lua")

                    print("ReleaseConsumerSmoke version=0.1.0 clojure=42 javascript=42 lua=42")
                    _ = version
                } catch {
                    fputs("ReleaseConsumerSmoke failed: \\(error)\\n", stderr)
                    exit(1)
                }
            }

            private static func expectInt(_ value: EcritumValue, equals expected: Int64, label: String) throws {
                guard value == .int(expected) else {
                    throw SmokeFailure("\\(label) expected \\(expected), got \\(value)")
                }
            }
        }

        private struct SmokeFailure: Error, CustomStringConvertible {
            var description: String

            init(_ description: String) {
                self.description = description
            }
        }
        """
    )


def is_within(path, root):
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def require_file(path, label):
    if not path.is_file():
        fail(f"missing {label}: {path}")
    return path


def validate_framework_runtime_metadata(framework):
    resources = framework / "Resources"
    metadata_path = resources / "ecritum-runtime.json"
    legacy_path = resources / "ecritum-runtime-lane.json"
    if metadata_path.exists():
        metadata = json.loads(metadata_path.read_text())
        artifact_kind = metadata.get("artifactKind")
        included = metadata.get("includedRuntimes")
        if metadata.get("formatVersion") != 1 or artifact_kind != "default":
            fail(f"invalid default runtime metadata in {metadata_path}: {metadata}")
        if not isinstance(included, list):
            fail(f"invalid includedRuntimes in {metadata_path}: {metadata}")
        missing = [runtime for runtime in DEFAULT_INCLUDED_RUNTIMES if runtime not in included]
        if missing:
            fail(f"default runtime metadata is missing required runtimes {missing}: {metadata_path}")
        return {
            "artifactKind": artifact_kind,
            "implementationProfile": metadata.get("implementationProfile"),
            "includedRuntimes": included,
            "metadataSource": str(metadata_path),
        }
    legacy = json.loads(require_file(legacy_path, "legacy runtime metadata").read_text())
    profile = legacy.get("releaseLane")
    if legacy.get("formatVersion") != 1 or profile != "full":
        fail(f"legacy runtime metadata is not a default-capable artifact: {legacy_path}: {legacy}")
    return {
        "artifactKind": "default",
        "implementationProfile": profile,
        "includedRuntimes": DEFAULT_INCLUDED_RUNTIMES,
        "metadataSource": str(legacy_path),
    }


def validate_workspace_runtime_artifact(swift_build_dir, repo_root, slice_name):
    state_path = swift_build_dir / "workspace-state.json"
    if not state_path.is_file():
        fail(f"missing SwiftPM workspace state: {state_path}")

    state = json.loads(state_path.read_text())
    artifacts = state.get("object", {}).get("artifacts", [])
    runtime_artifacts = [
        artifact for artifact in artifacts
        if artifact.get("targetName") == "EcritumRuntime"
        and artifact.get("packageRef", {}).get("identity") == "ecritum"
    ]
    if len(runtime_artifacts) != 1:
        fail(f"expected one EcritumRuntime artifact for package ecritum, found {len(runtime_artifacts)}")

    artifact = runtime_artifacts[0]
    source_type = artifact.get("source", {}).get("type")
    if source_type != "remote":
        fail(f"EcritumRuntime artifact source type is {source_type}, expected remote")

    artifact_path_value = artifact.get("path")
    if not artifact_path_value:
        fail(f"EcritumRuntime artifact is missing a path in {state_path}")
    artifact_path = Path(artifact_path_value).resolve()

    local_artifact = (repo_root / "dist" / "local" / "EcritumRuntime.xcframework").resolve()
    if is_within(artifact_path, local_artifact):
        fail(f"release consumer resolved the local artifact instead of a remote archive: {artifact_path}")
    if not is_within(artifact_path, swift_build_dir):
        fail(f"EcritumRuntime artifact path is outside the isolated SwiftPM build directory: {artifact_path}")

    framework = artifact_path / slice_name / "EcritumRuntime.framework"
    if not framework.is_dir():
        fail(f"missing downloaded EcritumRuntime.framework for {slice_name}: {framework}")
    return artifact, framework.resolve()


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Build and run a clean SwiftPM consumer against an HTTPS EcritumRuntime artifact.")
    parser.add_argument("--artifact-url", default=os.environ.get("ECRITUM_CONSUMER_ARTIFACT_URL") or os.environ.get("ECRITUM_RUNTIME_URL"))
    parser.add_argument("--checksum", default=os.environ.get("ECRITUM_CONSUMER_ARTIFACT_CHECKSUM") or os.environ.get("ECRITUM_RUNTIME_CHECKSUM"))
    parser.add_argument("--package-root", default=".")
    parser.add_argument("--build-dir", default="build/release-consumer-smoke")
    parser.add_argument("--release-zip", default="dist/release/EcritumRuntime.xcframework.zip")
    parser.add_argument("--package-manifest", default=None, help="Package JSON manifest. Defaults to RELEASE_ZIP.json.")
    parser.add_argument("--manifest-only", action="store_true", help="Validate release-mode Package.swift target selection without building a consumer.")
    parser.add_argument("--use-default-package-runtime", action="store_true", help="Do not inject Ecritum runtime env vars; validate the checked-in Package.swift default runtime.")
    parser.add_argument("--validate-workspace-state", default=None, help="Validate a SwiftPM build directory's workspace-state.json and exit.")
    parser.add_argument("--dependency-url", default=None, help="Use a remote Ecritum package dependency URL instead of a local path dependency.")
    parser.add_argument("--dependency-exact", default=None, help="Exact Ecritum package version to use with --dependency-url.")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    if args.use_default_package_runtime and (args.artifact_url or args.checksum):
        fail("--use-default-package-runtime cannot be combined with artifact URL/checksum env or flags")
    if not args.use_default_package_runtime and not args.artifact_url:
        fail("missing artifact URL: pass --artifact-url or set ECRITUM_CONSUMER_ARTIFACT_URL")
    if args.artifact_url and urlparse(args.artifact_url).scheme != "https":
        fail("artifact URL must use https because SwiftPM binary targets reject non-https URLs")
    if not args.use_default_package_runtime and not args.checksum:
        fail("missing artifact checksum: pass --checksum or set ECRITUM_CONSUMER_ARTIFACT_CHECKSUM")
    if args.checksum and not CHECKSUM_RE.match(args.checksum):
        fail("artifact checksum must be a 64-character lowercase SHA-256/SwiftPM checksum")
    if (args.dependency_url is None) != (args.dependency_exact is None):
        fail("--dependency-url and --dependency-exact must be set together")
    if args.dependency_url and urlparse(args.dependency_url).scheme != "https":
        fail("dependency URL must use https")

    repo_root = Path(args.package_root).resolve()
    if not (repo_root / "Package.swift").is_file():
        fail(f"missing Ecritum Package.swift: {repo_root / 'Package.swift'}")

    build_dir = Path(args.build_dir).resolve()
    release_zip = Path(args.release_zip).resolve()
    package_manifest = Path(args.package_manifest).resolve() if args.package_manifest else Path(str(release_zip) + ".json")
    checksum_sidecar = Path(str(release_zip) + ".checksum")
    if release_zip.exists() and args.checksum:
        local_zip_sha = sha256(release_zip)
        if local_zip_sha != args.checksum:
            fail(f"release zip SHA-256 {local_zip_sha} does not match requested checksum {args.checksum}")
        if checksum_sidecar.exists() and checksum_sidecar.read_text().strip() != args.checksum:
            fail(f"release zip checksum sidecar does not match requested checksum: {checksum_sidecar}")
        if package_manifest.exists():
            manifest = json.loads(package_manifest.read_text())
            if manifest.get("sha256") != args.checksum or manifest.get("swiftPackageChecksum") != args.checksum:
                fail(f"package manifest checksum does not match requested checksum: {package_manifest}")
            for key in ["releaseLane", "artifactReleaseLane"]:
                if key in manifest:
                    fail(f"package manifest contains retired release lane metadata {key}: {package_manifest}")
            if manifest.get("artifactKind") not in (None, "default"):
                fail(f"package manifest artifactKind is not default: {package_manifest}")
            included = manifest.get("includedRuntimes")
            if isinstance(included, list):
                missing = [runtime for runtime in DEFAULT_INCLUDED_RUNTIMES if runtime not in included]
                if missing:
                    fail(f"package manifest includedRuntimes is missing required runtimes {missing}: {package_manifest}")

    arch = platform.machine()
    slice_name = f"macos-{arch}"

    consumer_dir = build_dir / "Consumer"
    swift_build_dir = build_dir / "swift-build"
    source_dir = consumer_dir / "Sources" / "ReleaseConsumerSmoke"
    empty_bin = build_dir / "empty-bin"

    env = build_env(args.artifact_url, args.checksum, build_dir, args.use_default_package_runtime)
    runtime_target = validate_ecritum_release_manifest(repo_root, env)
    if args.manifest_only:
        print(json.dumps(
            {
                "artifactUrl": args.artifact_url or "checked-in Package.swift default",
                "binaryTargetPath": runtime_target.get("path"),
                "checksum": args.checksum,
                "defaultPackageRuntime": args.use_default_package_runtime,
                "manifestOnly": True,
                "ok": True,
            },
            indent=2,
            sort_keys=True,
        ))
        return 0
    if args.validate_workspace_state:
        _, framework = validate_workspace_runtime_artifact(Path(args.validate_workspace_state).resolve(), repo_root, slice_name)
        runtime_metadata = validate_framework_runtime_metadata(framework)
        print(json.dumps(
            {
                "artifactKind": runtime_metadata["artifactKind"],
                "artifactUrl": args.artifact_url or "checked-in Package.swift default",
                "checksum": args.checksum,
                "defaultPackageRuntime": args.use_default_package_runtime,
                "downloadedFramework": str(framework),
                "includedRuntimes": runtime_metadata["includedRuntimes"],
                "ok": True,
                "workspaceState": str(Path(args.validate_workspace_state).resolve() / "workspace-state.json"),
            },
            indent=2,
            sort_keys=True,
        ))
        return 0

    shutil.rmtree(build_dir, ignore_errors=True)
    source_dir.mkdir(parents=True)
    empty_bin.mkdir(parents=True)
    (consumer_dir / "Package.swift").write_text(package_swift(repo_root, args.dependency_url, args.dependency_exact))
    (source_dir / "main.swift").write_text(main_swift())
    env = build_env(args.artifact_url, args.checksum, build_dir, args.use_default_package_runtime)

    run([tool("swift"), "build", "--build-path", str(swift_build_dir), "--configuration", "debug", "--quiet"], cwd=consumer_dir, env=env)
    bin_path = Path(run([tool("swift"), "build", "--build-path", str(swift_build_dir), "--configuration", "debug", "--show-bin-path"], cwd=consumer_dir, env=env).strip())
    executable = bin_path / "ReleaseConsumerSmoke"
    if not executable.is_file():
        fail(f"missing release consumer executable: {executable}")

    _, framework = validate_workspace_runtime_artifact(swift_build_dir, repo_root, slice_name)
    runtime_metadata = validate_framework_runtime_metadata(framework)
    local_artifact = (repo_root / "dist" / "local" / "EcritumRuntime.xcframework").resolve()
    if is_within(framework, local_artifact):
        fail(f"release consumer resolved the local artifact instead of the remote archive: {framework}")

    runtime_env = {
        "HOME": env["HOME"],
        "TMPDIR": env["TMPDIR"],
        "PATH": str(empty_bin),
    }
    completed = subprocess.run(
        [str(executable)],
        env=runtime_env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 0:
        fail("release consumer failed under isolated runtime environment\nstdout:\n" + completed.stdout + "\nstderr:\n" + completed.stderr)
    output = completed.stdout.strip()
    if output != SUCCESS_LINE:
        fail(f"unexpected release consumer output: {output}")

    link_payload = "\n".join(
        [
            run([tool("otool"), "-L", str(require_file(executable, "release consumer executable"))]),
            run([tool("otool"), "-L", str(require_file(framework / "EcritumRuntime", "EcritumRuntime framework binary"))]),
            run([tool("otool"), "-L", str(require_file(framework / "Resources" / "libecritum_graal.dylib", "private GraalVM runtime dylib"))]),
        ]
    )
    for forbidden in ["/GraalVM", "/jdk", "/native/target", "/build/native"]:
        if forbidden in link_payload:
            fail(f"release consumer links a build-machine runtime path containing {forbidden}")

    payload = {
        "artifactKind": runtime_metadata["artifactKind"],
        "artifactUrl": args.artifact_url,
        "binaryTargetPath": runtime_target.get("path"),
        "checksum": args.checksum,
        "consumer": str(consumer_dir),
        "dependencyExact": args.dependency_exact,
        "dependencyUrl": args.dependency_url,
        "downloadedFramework": str(framework),
        "includedRuntimes": runtime_metadata["includedRuntimes"],
        "ok": True,
        "output": output,
        "releaseZip": str(release_zip) if release_zip.exists() else None,
        "swiftBuildDir": str(swift_build_dir),
    }
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
