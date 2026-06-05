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

test-javascript-java: test-java

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
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure_with_host$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure_with_stdlib$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_javascript_with_stdlib$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _graal_create_isolate$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _graal_tear_down_isolate$'

xcframework:
    test -d build/native
    scripts/build-xcframework.sh
    just check-xcframework

check-xcframework:
    @scripts/check-xcframework.sh

check-abi:
    @scripts/check-abi.sh

test-abi-checker:
    python3 -m unittest Tests/ABI/test_check_abi.py

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

conformance:
    mkdir -p build/conformance
    python3 -m py_compile scripts/run-conformance.py Tests/Conformance/fixtures/provider.py Tests/Conformance/fixtures/clojure_native_provider.py Tests/Conformance/fixtures/javascript_native_provider.py
    python3 -m unittest Tests/Conformance/test_runner.py
    python3 scripts/run-conformance.py --manifest Tests/Conformance/manifest.json --provider python3 Tests/Conformance/fixtures/provider.py --mode scaffold > build/conformance/scaffold.json

test-conformance: conformance

conformance-clojure-native:
    test -d dist/local/EcritumRuntime.xcframework
    mkdir -p build/conformance
    python3 scripts/run-conformance.py --manifest Tests/Conformance/manifest.json --category eval --category host --category error --strict --provider-timeout-seconds 30 --provider python3 Tests/Conformance/fixtures/clojure_native_provider.py > build/conformance/clojure-native.json

conformance-javascript-native:
    test -d dist/local/EcritumRuntime.xcframework
    mkdir -p build/conformance
    python3 scripts/run-conformance.py --manifest Tests/Conformance/manifest.json --category eval --category host --category error --category stdlib --strict --provider-timeout-seconds 30 --provider python3 Tests/Conformance/fixtures/javascript_native_provider.py > build/conformance/javascript-native.json

test-facades-clojure:
    test -d dist/local/EcritumRuntime.xcframework
    mkdir -p build/facades
    python3 scripts/run-conformance.py --manifest Tests/Conformance/manifest.json --category stdlib --strict --provider-timeout-seconds 30 --provider python3 Tests/Conformance/fixtures/clojure_native_provider.py > build/facades/clojure.json

conformance-clojure-facades:
    test -d dist/local/EcritumRuntime.xcframework
    mkdir -p build/conformance
    python3 scripts/run-conformance.py --manifest Tests/Conformance/manifest.json --category stdlib --strict --provider-timeout-seconds 30 --provider python3 Tests/Conformance/fixtures/clojure_native_provider.py > build/conformance/clojure-facades.json

security:
    mkdir -p build/security
    python3 -m py_compile scripts/check-security-static.py scripts/run-security-abuse.py scripts/check-parser-abuse.py Tests/Security/fixtures/abuse_provider.py Tests/Security/fixtures/javascript_abuse_provider.py
    python3 -m unittest Tests/Security/test_security_baseline.py
    just test-security-static
    just test-security-abuse
    just test-security-fuzz

test-security-static:
    mkdir -p build/security
    python3 scripts/check-security-static.py > build/security/static.json

test-security-abuse:
    mkdir -p build/security
    python3 scripts/run-security-abuse.py --manifest Tests/Security/abuse-manifest.json --provider python3 Tests/Security/fixtures/abuse_provider.py --mode baseline > build/security/abuse.json
    if python3 scripts/run-security-abuse.py --manifest Tests/Security/abuse-manifest.json --strict --provider python3 Tests/Security/fixtures/abuse_provider.py --mode baseline > build/security/abuse-strict.json; then echo "strict security abuse unexpectedly passed" >&2; exit 1; else status=$?; test $status -eq 1; fi

security-clojure-facades:
    test -d dist/local/EcritumRuntime.xcframework
    mkdir -p build/security
    python3 scripts/run-security-abuse.py --manifest Tests/Security/abuse-manifest.json --strict --provider-timeout-seconds 30 --provider python3 Tests/Security/fixtures/clojure_facade_abuse_provider.py > build/security/clojure-facades.json

security-javascript:
    test -d dist/local/EcritumRuntime.xcframework
    mkdir -p build/security
    python3 scripts/run-security-abuse.py --manifest Tests/Security/abuse-manifest.json --strict --provider-timeout-seconds 30 --provider python3 Tests/Security/fixtures/javascript_abuse_provider.py > build/security/javascript.json

# Parser-abuse-equivalent gate until eval/value/error/callback fuzz surfaces exist.
test-security-fuzz:
    mkdir -p build/security
    python3 scripts/check-parser-abuse.py --manifest Tests/Security/parser-abuse-manifest.json > build/security/parser-abuse.json
    python3 scripts/check-parser-abuse.py --manifest Tests/Security/parser-abuse-manifest.json --verify-evidence > build/security/parser-abuse-evidence.json
    if python3 scripts/check-parser-abuse.py --manifest Tests/Security/parser-abuse-manifest.json --strict > build/security/parser-abuse-strict.json; then echo "strict parser abuse unexpectedly passed" >&2; exit 1; else status=$?; test $status -eq 1; fi

test-c-abi-lifecycle:
    mkdir -p build/c-abi
    clang -DECRITUM_TESTING -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/lifecycle_contract.c -o build/c-abi/lifecycle_contract
    build/c-abi/lifecycle_contract
    clang++ -std=c++17 -I Sources/CEcritum/include Tests/C/header_cpp_smoke.cpp -o build/c-abi/header_cpp_smoke
    build/c-abi/header_cpp_smoke

test-c-abi-asan:
    mkdir -p build/c-abi
    clang -DECRITUM_TESTING -fsanitize=address,undefined -fno-omit-frame-pointer -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/lifecycle_contract.c -o build/c-abi/lifecycle_contract_asan
    ASAN_OPTIONS=detect_leaks=0 build/c-abi/lifecycle_contract_asan

test-c-abi-host-registration:
    mkdir -p build/c-abi
    clang -DECRITUM_TESTING -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/host_registration_contract.c -o build/c-abi/host_registration_contract
    build/c-abi/host_registration_contract

test-c-abi-host-registration-asan:
    mkdir -p build/c-abi
    clang -DECRITUM_TESTING -fsanitize=address,undefined -fno-omit-frame-pointer -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/host_registration_contract.c -o build/c-abi/host_registration_contract_asan
    ASAN_OPTIONS=detect_leaks=0 build/c-abi/host_registration_contract_asan

test-c-abi-eval:
    mkdir -p build/c-abi
    clang -DECRITUM_TESTING -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/eval_job_contract.c -o build/c-abi/eval_job_contract
    build/c-abi/eval_job_contract

test-c-abi-eval-asan:
    mkdir -p build/c-abi
    clang -DECRITUM_TESTING -fsanitize=address,undefined -fno-omit-frame-pointer -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/eval_job_contract.c -o build/c-abi/eval_job_contract_asan
    ASAN_OPTIONS=detect_leaks=0 build/c-abi/eval_job_contract_asan

test-native-eval-smoke:
    test -f build/native/macos-arm64/libecritum.dylib
    mkdir -p build/c-abi
    clang -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/native_eval_smoke.c -L build/native/macos-arm64 -lecritum -o build/c-abi/native_eval_smoke
    DYLD_LIBRARY_PATH=build/native/macos-arm64 build/c-abi/native_eval_smoke

test-javascript-native-smoke: test-native-eval-smoke

test-native-eval-smoke-asan:
    test -f build/native/macos-arm64/libecritum.dylib
    mkdir -p build/c-abi
    clang -fsanitize=address,undefined -fno-omit-frame-pointer -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/native_eval_smoke.c -L build/native/macos-arm64 -lecritum -o build/c-abi/native_eval_smoke_asan
    ASAN_OPTIONS=detect_leaks=0 DYLD_LIBRARY_PATH=build/native/macos-arm64 build/c-abi/native_eval_smoke_asan

test-xcframework-eval-smoke:
    test -d dist/local/EcritumRuntime.xcframework
    scripts/swift-test.sh --mode runtime

test-c-abi-policy-config:
    mkdir -p build/c-abi
    clang -DECRITUM_TESTING -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/policy_config_contract.c -o build/c-abi/policy_config_contract
    build/c-abi/policy_config_contract

test-c-abi-policy-config-asan:
    mkdir -p build/c-abi
    clang -DECRITUM_TESTING -fsanitize=address,undefined -fno-omit-frame-pointer -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/policy_config_contract.c -o build/c-abi/policy_config_contract_asan
    ASAN_OPTIONS=detect_leaks=0 build/c-abi/policy_config_contract_asan

test-lifecycle-leak-smoke:
    test -d dist/local/EcritumRuntime.xcframework
    mkdir -p build/c-abi
    clang -target "$(uname -m)-apple-macos{{min_macos}}" -mmacosx-version-min={{min_macos}} Tests/C/framework_lifecycle_smoke.c -o build/c-abi/framework_lifecycle_smoke
    slice="macos-$(uname -m)"; \
        binary="dist/local/EcritumRuntime.xcframework/$slice/EcritumRuntime.framework/EcritumRuntime"; \
        leaks --atExit -- build/c-abi/framework_lifecycle_smoke "$binary"

test: plan-check conformance security test-swift-auto test-java test-c-abi-lifecycle test-c-abi-asan test-c-abi-host-registration test-c-abi-host-registration-asan test-c-abi-eval test-c-abi-eval-asan test-native-eval-smoke test-native-eval-smoke-asan test-xcframework-eval-smoke test-c-abi-policy-config test-c-abi-policy-config-asan check-abi license-report check-dep-delta test-examples-auto

test-m3-002b: native test-native-eval-smoke test-native-eval-smoke-asan xcframework test-xcframework-eval-smoke check-abi license-report check-dep-delta

test-m3-002c: native test-java test-c-abi-eval test-c-abi-eval-asan test-native-eval-smoke test-native-eval-smoke-asan xcframework test-xcframework-eval-smoke conformance-clojure-native security check-abi license-report check-dep-delta

test-m3-003: native test-java test-c-abi-eval test-c-abi-eval-asan test-c-abi-policy-config test-c-abi-policy-config-asan test-native-eval-smoke test-native-eval-smoke-asan xcframework test-xcframework-eval-smoke test-facades-clojure conformance-clojure-facades security-clojure-facades security check-abi license-report check-dep-delta

test-javascript-xcframework-smoke: test-swift

test-m4-002: native test-java test-native-eval-smoke xcframework test-swift conformance-javascript-native security-javascript check-abi inspect size bench-javascript-first-eval license-report check-dep-delta

example-swift:
    @test -d dist/local/EcritumRuntime.xcframework || { echo "missing dist/local/EcritumRuntime.xcframework; run mise exec -- just xcframework first" >&2; exit 1; }
    @cd Examples/SwiftHost && swift package reset
    @cd Examples/SwiftHost && swift build --quiet
    @cd Examples/SwiftHost && slice="macos-$(uname -m)" && binary=".build/$(uname -m)-apple-macosx/debug/SwiftHost" && DYLD_FRAMEWORK_PATH="../../dist/local/EcritumRuntime.xcframework/$slice" "$binary"

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
    @python3 scripts/inspect-artifact.py

package-artifact:
    @python3 scripts/package-artifact.py

checksum:
    test -f dist/release/EcritumRuntime.xcframework.zip
    @swift package compute-checksum dist/release/EcritumRuntime.xcframework.zip

size:
    @python3 scripts/size-artifact.py

bench-cold-start:
    @python3 scripts/measure-runtime.py --mode startup

bench-swift-cold-start:
    @cd Examples/SwiftHost && \
        mkdir -p ../../build && \
        tmp_build=$(mktemp -d ../../build/swift-host-bench.XXXXXX) && \
        trap 'rm -rf "$tmp_build"' EXIT && \
        slice="macos-$(uname -m)" && \
        binary="$tmp_build/$(uname -m)-apple-macosx/debug/SwiftHost" && \
        ECRITUM_LOCAL_RUNTIME=1 ECRITUM_LOCAL_RUNTIME_STATE=v4:runtime:runtime-present:examples swift build --quiet --build-path "$tmp_build" && \
        DYLD_FRAMEWORK_PATH="../../dist/local/EcritumRuntime.xcframework/$slice" "$binary" >/dev/null && \
        DYLD_FRAMEWORK_PATH="../../dist/local/EcritumRuntime.xcframework/$slice" python3 ../../scripts/measure-command.py --name swift-host --runs 30 --max-p50-ms 1000 --max-p95-ms 2000 --expect-stdout "SwiftHost version=0.1.0-dev" -- "$binary"

bench-idle-rss:
    @python3 scripts/measure-runtime.py --mode rss

bench-first-eval:
    @python3 scripts/measure-first-eval.py

bench-javascript-first-eval:
    @python3 scripts/measure-first-eval.py --name javascript-first-eval --language javascript --source "40 + 2" --source-name bench-first-eval.js

check-dep-delta:
    @python3 scripts/check-dep-delta.py

perf-baseline: size bench-cold-start bench-swift-cold-start bench-idle-rss bench-first-eval check-dep-delta

perf: perf-baseline

license-report:
    @python3 scripts/license-report.py

license-report-strict:
    @python3 scripts/license-report.py --strict

release-check:
    @scripts/release-check.sh

clean:
    rm -rf .build target build dist native/target
