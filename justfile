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
    mvn -s {{maven_settings}} -f {{maven_project}} -Pnative -DskipTests package
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

build-swift:
    test -f Package.swift
    swift build

test-swift-scaffold:
    test -f Package.swift
    swift package reset
    swift test

test-swift:
    test -d dist/local/EcritumRuntime.xcframework
    test -f Package.swift
    swift package reset
    swift test

test: plan-check test-swift-scaffold test-java

size:
    @if [ -d dist ]; then du -sh dist/*; else echo "No dist artifacts yet."; fi

license-report:
    @echo "TODO: generate Maven, SwiftPM, and bundled-runtime license inventory."

clean:
    rm -rf .build target build dist native/target
