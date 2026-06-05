set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

graalvm_version := "25.0.2"
native_name := "ecritum"
min_macos := "14.0"
maven_settings := ".mvn/settings.xml"
maven_project := "native/pom.xml"
native_output := "native/target/libecritum.dylib"
native_stable_dir := "build/native/macos-arm64"
native_private_headers_dir := "build/native/macos-arm64/include/private"

default:
    @just --list

setup:
    mise install

doctor:
    @echo "java: $(java -version 2>&1 | grep -m1 'version')"
    @echo "maven: $(mvn -version | head -1)"
    @echo "swift: $(swift --version | head -1)"
    @echo "native-image: $(native-image --version 2>&1 | head -1)"

plan-check:
    test -f PLAN.org
    test -f README.md
    test -f AGENTS.md

build-java:
    test -f {{maven_project}}
    mvn -s {{maven_settings}} -f {{maven_project}} -q -DskipTests package

test-java:
    test -f {{maven_project}}
    mvn -s {{maven_settings}} -f {{maven_project}} test

native:
    test -f {{maven_project}}
    MACOSX_DEPLOYMENT_TARGET={{min_macos}} mvn -s {{maven_settings}} -f {{maven_project}} -Pnative -DskipTests package
    test -f {{native_output}}
    mkdir -p {{native_stable_dir}}
    mkdir -p {{native_private_headers_dir}}
    cp {{native_output}} {{native_stable_dir}}/libecritum.dylib
    cp native/target/graal_isolate.h native/target/graal_isolate_dynamic.h native/target/libecritum.h native/target/libecritum_dynamic.h {{native_private_headers_dir}}/
    just check-native

check-native:
    test -f {{native_stable_dir}}/libecritum.dylib
    test -f {{native_private_headers_dir}}/libecritum.h
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_version$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _graal_create_isolate$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _graal_tear_down_isolate$'

xcframework:
    test -d build/native
    scripts/build-xcframework.sh
    just check-xcframework

check-xcframework:
    scripts/check-xcframework.sh

check-abi:
    scripts/check-abi.sh

build-swift:
    test -f Package.swift
    swift build

test-swift-scaffold:
    scripts/swift-test.sh --mode scaffold

test-swift:
    test -d dist/local/EcritumRuntime.xcframework
    scripts/swift-test.sh --mode runtime

test-swift-auto:
    @if [ -d dist/local/EcritumRuntime.xcframework ]; then \
        scripts/swift-test.sh --mode runtime; \
    else \
        scripts/swift-test.sh --mode scaffold; \
    fi

test: plan-check test-swift-auto test-java test-examples-auto

example-swift:
    @test -d dist/local/EcritumRuntime.xcframework || { echo "missing dist/local/EcritumRuntime.xcframework; run mise exec -- just xcframework first" >&2; exit 1; }
    @cd Examples/SwiftHost && swift package reset
    @cd Examples/SwiftHost && ECRITUM_LOCAL_RUNTIME=1 ECRITUM_LOCAL_RUNTIME_STATE=v4:runtime:runtime-present:examples swift run --quiet SwiftHost

example-c:
    @test -d dist/local/EcritumRuntime.xcframework || { echo "missing dist/local/EcritumRuntime.xcframework; run mise exec -- just xcframework first" >&2; exit 1; }
    @slice="macos-$(uname -m)"; \
    framework="dist/local/EcritumRuntime.xcframework/$slice/EcritumRuntime.framework"; \
    test -f "$framework/Headers/ecritum.h" || { echo "missing packaged ecritum.h in $framework" >&2; exit 1; }; \
    mkdir -p build/examples/chost; \
    clang -target "$(uname -m)-apple-macos{{min_macos}}" -mmacosx-version-min={{min_macos}} \
        -I "$framework/Headers" \
        -F "dist/local/EcritumRuntime.xcframework/$slice" \
        -framework EcritumRuntime \
        -Wl,-rpath,@loader_path/../../../dist/local/EcritumRuntime.xcframework/$slice \
        Examples/CHost/main.c \
        -o build/examples/chost/CHost; \
    build/examples/chost/CHost; \
    otool -L build/examples/chost/CHost | grep -q '@rpath/EcritumRuntime.framework/EcritumRuntime'; \
    repo_root="$(pwd -P)"; \
    if { otool -L build/examples/chost/CHost; otool -l build/examples/chost/CHost; } | grep -E '/GraalVM|/jdk|/native/target|/build/native' || \
        { otool -L build/examples/chost/CHost; otool -l build/examples/chost/CHost; } | grep -F "$repo_root"; then \
        echo "CHost links build-machine runtime path" >&2; exit 1; \
    fi

examples: example-swift example-c

test-examples-auto:
    @if [ -d dist/local/EcritumRuntime.xcframework ]; then \
        just examples; \
    else \
        echo "Skipping examples: dist/local/EcritumRuntime.xcframework is missing."; \
    fi

inspect:
    python3 scripts/inspect-artifact.py

package-artifact:
    python3 scripts/package-artifact.py

checksum:
    test -f dist/release/EcritumRuntime.xcframework.zip
    @swift package compute-checksum dist/release/EcritumRuntime.xcframework.zip

size:
    python3 scripts/size-artifact.py

license-report:
    python3 scripts/license-report.py

license-report-strict:
    python3 scripts/license-report.py --strict

release-check:
    scripts/release-check.sh

clean:
    rm -rf .build target build dist native/target
