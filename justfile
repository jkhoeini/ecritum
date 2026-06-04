set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

graalvm_version := "25.0.2"
native_name := "ecritum"
min_macos := "14.0"

default:
    @just --list

setup:
    mise install

doctor:
    @echo "java: $(java -version 2>&1 | head -1)"
    @echo "maven: $(mvn -version | head -1)"
    @echo "swift: $(swift --version | head -1)"
    @echo "native-image: $(native-image --version 2>&1 | head -1)"

plan-check:
    test -f PLAN.org
    test -f README.md
    test -f AGENTS.md

build-java:
    test -f pom.xml
    mvn -q -DskipTests package

test-java:
    test -f pom.xml
    mvn test

native:
    test -f pom.xml
    mvn -Pnative -DskipTests package

xcframework:
    test -d build/native
    scripts/build-xcframework.sh

build-swift:
    test -f Package.swift
    swift build

test-swift:
    test -f Package.swift
    swift test

test: plan-check
    @echo "Source targets are not scaffolded yet. Use PLAN.org for the implementation sequence."

size:
    @if [ -d dist ]; then du -sh dist/*; else echo "No dist artifacts yet."; fi

license-report:
    @echo "TODO: generate Maven, SwiftPM, and bundled-runtime license inventory."

clean:
    rm -rf .build target build dist
