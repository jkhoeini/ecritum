set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

graalvm_version := "25.0.2"
native_name := "ecritum"
min_macos := "14.0"
maven_settings := ".mvn/settings.xml"
maven_project := "native/pom.xml"
native_output := "native/target/libecritum.dylib"
native_stable_dir := "build/native/macos-arm64"
native_core_stable_dir := "build/native/core/macos-arm64"
native_full_stable_dir := "build/native/full/macos-arm64"
native_private_headers_dir := "build/native/macos-arm64/include/private"
native_core_private_headers_dir := "build/native/core/macos-arm64/include/private"
native_full_private_headers_dir := "build/native/full/macos-arm64/include/private"

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
    mvn -s {{maven_settings}} -f {{maven_project}} -Pfull test

test-javascript-java: test-java

native:
    just native-full

native-core:
    test -f {{maven_project}}
    MACOSX_DEPLOYMENT_TARGET={{min_macos}} mvn -s {{maven_settings}} -f {{maven_project}} clean package -Pnative,core -Decritum.native.mainClass=ecritum.NativeCoreEntrypoints -Dmaven.test.skip=true
    test -f {{native_output}}
    mkdir -p {{native_core_stable_dir}}
    mkdir -p {{native_core_private_headers_dir}}
    cp {{native_output}} {{native_core_stable_dir}}/libecritum.dylib
    cp native/target/graal_isolate.h native/target/graal_isolate_dynamic.h native/target/libecritum.h native/target/libecritum_dynamic.h {{native_core_private_headers_dir}}/
    just check-native-core

native-full:
    test -f {{maven_project}}
    MACOSX_DEPLOYMENT_TARGET={{min_macos}} mvn -s {{maven_settings}} -f {{maven_project}} clean package -Pnative,full -Decritum.native.mainClass=ecritum.NativeEntrypoints -Dmaven.test.skip=true
    test -f {{native_output}}
    mkdir -p {{native_full_stable_dir}}
    mkdir -p {{native_full_private_headers_dir}}
    cp {{native_output}} {{native_full_stable_dir}}/libecritum.dylib
    cp native/target/graal_isolate.h native/target/graal_isolate_dynamic.h native/target/libecritum.h native/target/libecritum_dynamic.h {{native_full_private_headers_dir}}/
    mkdir -p {{native_stable_dir}}
    mkdir -p {{native_private_headers_dir}}
    cp {{native_output}} {{native_stable_dir}}/libecritum.dylib
    cp native/target/graal_isolate.h native/target/graal_isolate_dynamic.h native/target/libecritum.h native/target/libecritum_dynamic.h {{native_private_headers_dir}}/
    just check-native-full
    just check-native

check-native:
    just check-native-full {{native_stable_dir}}

check-native-core dir="build/native/core/macos-arm64":
    test -f {{dir}}/libecritum.dylib
    test -f {{dir}}/include/private/libecritum.h
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_version$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure_with_host$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure_with_stdlib$'
    ! nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_javascript_with_stdlib$'
    ! nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_lua_with_stdlib$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _graal_create_isolate$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _graal_tear_down_isolate$'

check-native-full dir="build/native/full/macos-arm64":
    test -f {{dir}}/libecritum.dylib
    test -f {{dir}}/include/private/libecritum.h
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_version$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure_with_host$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure_with_stdlib$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_javascript_with_stdlib$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_lua_with_stdlib$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _graal_create_isolate$'
    nm -gU {{dir}}/libecritum.dylib | grep -q ' _graal_tear_down_isolate$'

check-native-legacy:
    test -f {{native_stable_dir}}/libecritum.dylib
    test -f {{native_private_headers_dir}}/libecritum.h
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_version$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure_with_host$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_clojure_with_stdlib$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_javascript_with_stdlib$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _ecritum_graal_eval_lua_with_stdlib$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _graal_create_isolate$'
    nm -gU {{native_stable_dir}}/libecritum.dylib | grep -q ' _graal_tear_down_isolate$'

xcframework output="dist/local/EcritumRuntime.xcframework":
    test -d build/native
    scripts/build-xcframework.sh --native-dir "{{native_stable_dir}}" --lane full --output "{{output}}"
    just check-xcframework "{{output}}"

xcframework-core output="dist/core/EcritumRuntime.xcframework":
    test -d {{native_core_stable_dir}}
    scripts/build-xcframework.sh --native-dir "{{native_core_stable_dir}}" --lane core --work-dir "build/xcframework/core" --output "{{output}}"
    just check-xcframework "{{output}}"

xcframework-full output="dist/full/EcritumRuntime.xcframework":
    test -d {{native_full_stable_dir}}
    scripts/build-xcframework.sh --native-dir "{{native_full_stable_dir}}" --lane full --work-dir "build/xcframework/full" --output "{{output}}"
    just check-xcframework "{{output}}"

check-xcframework artifact="dist/local/EcritumRuntime.xcframework":
    @scripts/check-xcframework.sh --artifact "{{artifact}}"

check-abi artifact="dist/local/EcritumRuntime.xcframework":
    @scripts/check-abi.sh --artifact "{{artifact}}"

test-abi-checker:
    python3 -m unittest Tests/ABI/test_check_abi.py

test-release-tools:
    python3 -B -m unittest discover -s Tests/Release -p 'test_*.py'

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
    python3 -m py_compile scripts/run-conformance.py Tests/Conformance/fixtures/provider.py Tests/Conformance/fixtures/clojure_native_provider.py Tests/Conformance/fixtures/javascript_native_provider.py Tests/Conformance/fixtures/lua_native_provider.py
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

conformance-lua-native:
    test -d dist/local/EcritumRuntime.xcframework
    mkdir -p build/conformance
    python3 scripts/run-conformance.py --manifest Tests/Conformance/manifest.json --category eval --category host --category error --category stdlib --strict --provider-timeout-seconds 30 --provider python3 Tests/Conformance/fixtures/lua_native_provider.py > build/conformance/lua-native.json

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
    python3 -m py_compile scripts/check-security-static.py scripts/run-security-abuse.py scripts/check-parser-abuse.py Tests/Security/fixtures/abuse_provider.py Tests/Security/fixtures/javascript_abuse_provider.py Tests/Security/fixtures/lua_abuse_provider.py
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

security-lua:
    test -d dist/local/EcritumRuntime.xcframework
    mkdir -p build/security
    python3 scripts/run-security-abuse.py --manifest Tests/Security/abuse-manifest.json --strict --provider-timeout-seconds 30 --provider python3 Tests/Security/fixtures/lua_abuse_provider.py > build/security/lua.json

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

test-c-abi-eval-core-lane:
    mkdir -p build/c-abi
    clang -DECRITUM_TESTING -DECRITUM_RUNTIME_LANE_CORE -I Sources/CEcritum/include -I build/native/macos-arm64/include/private scripts/ecritum_runtime_wrapper.c Tests/C/eval_job_contract.c -o build/c-abi/eval_job_contract_core_lane
    build/c-abi/eval_job_contract_core_lane

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

test-lua-native-smoke: test-native-eval-smoke

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

test: plan-check conformance security test-swift-auto test-java test-c-abi-lifecycle test-c-abi-asan test-c-abi-host-registration test-c-abi-host-registration-asan test-c-abi-eval test-c-abi-eval-asan test-native-eval-smoke test-native-eval-smoke-asan test-xcframework-eval-smoke test-c-abi-policy-config test-c-abi-policy-config-asan check-abi test-release-tools license-report check-dep-delta test-examples-auto

test-m3-002b: native test-native-eval-smoke test-native-eval-smoke-asan xcframework test-xcframework-eval-smoke check-abi license-report check-dep-delta

test-m3-002c: native test-java test-c-abi-eval test-c-abi-eval-asan test-native-eval-smoke test-native-eval-smoke-asan xcframework test-xcframework-eval-smoke conformance-clojure-native security check-abi license-report check-dep-delta

test-m3-003: native test-java test-c-abi-eval test-c-abi-eval-asan test-c-abi-policy-config test-c-abi-policy-config-asan test-native-eval-smoke test-native-eval-smoke-asan xcframework test-xcframework-eval-smoke test-facades-clojure conformance-clojure-facades security-clojure-facades security check-abi license-report check-dep-delta

test-javascript-xcframework-smoke: test-swift

bench-lua-first-eval:
    test -d dist/local/EcritumRuntime.xcframework
    python3 scripts/measure-first-eval.py --name lua-first-eval --language lua --source "return 40 + 2" --source-name bench-first-eval.lua --runs 10 > build/perf/lua-first-eval.json

test-m5-001: native test-java test-native-eval-smoke xcframework test-swift conformance-lua-native security-lua check-abi inspect size bench-lua-first-eval license-report check-dep-delta

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

packaged-app-smoke:
    @scripts/test-packaged-app-smoke.sh

test-packaged-app-smoke: packaged-app-smoke

test-release-consumer-smoke artifact_url="" checksum="" release_zip="":
    @args=(); \
    if [ -n "{{artifact_url}}" ]; then args+=(--artifact-url "{{artifact_url}}"); fi; \
    if [ -n "{{checksum}}" ]; then args+=(--checksum "{{checksum}}"); fi; \
    if [ -n "{{release_zip}}" ]; then args+=(--release-zip "{{release_zip}}"); fi; \
    python3 scripts/test-release-consumer-smoke.py "${args[@]}"

examples: example-swift example-c packaged-app-smoke

test-examples-auto:
    @if [ -d dist/local/EcritumRuntime.xcframework ]; then \
        just examples; \
    else \
        echo "Skipping examples: dist/local/EcritumRuntime.xcframework is missing."; \
    fi

inspect artifact="dist/local/EcritumRuntime.xcframework":
    @python3 scripts/inspect-artifact.py --artifact "{{artifact}}"

package-artifact artifact="dist/local/EcritumRuntime.xcframework" output="dist/release/EcritumRuntime.xcframework.zip" lane="core":
    @python3 scripts/package-artifact.py --artifact "{{artifact}}" --output "{{output}}" --release-lane "{{lane}}"

package-artifact-verify artifact="dist/local/EcritumRuntime.xcframework" lane="core":
    @python3 scripts/check-package-reproducible.py --artifact "{{artifact}}" --release-lane "{{lane}}"

checksum output="dist/release/EcritumRuntime.xcframework.zip":
    test -f "{{output}}"
    @swift package compute-checksum "{{output}}"

size artifact="dist/local/EcritumRuntime.xcframework" lane="core":
    @python3 scripts/size-artifact.py --artifact "{{artifact}}" --lane "{{lane}}"

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

check-dep-delta lane="full":
    @python3 scripts/check-dep-delta.py --lane "{{lane}}"

check-license-texts artifact="dist/local/EcritumRuntime.xcframework" license_report="":
    @args=(--artifact "{{artifact}}"); \
    if [ -n "{{license_report}}" ]; then args+=(--license-report-command python3 -c "import pathlib; print(pathlib.Path('{{license_report}}').read_text())"); fi; \
    python3 scripts/check-license-texts.py "${args[@]}"

check-license-texts-zip release_zip="dist/release/EcritumRuntime.xcframework.zip" license_report="":
    @args=(--release-zip "{{release_zip}}"); \
    if [ -n "{{license_report}}" ]; then args+=(--license-report-command python3 -c "import pathlib; print(pathlib.Path('{{license_report}}').read_text())"); fi; \
    python3 scripts/check-license-texts.py "${args[@]}"

check-vulnerability-response release_zip="dist/release/EcritumRuntime.xcframework.zip" sbom="" release_url="":
    @if [ -z "{{sbom}}" ]; then python3 scripts/license-report.py > "{{release_zip}}.spdx.json"; fi; \
    args=(--release-zip "{{release_zip}}"); \
    if [ -n "{{release_url}}" ]; then args+=(--release-url "{{release_url}}"); fi; \
    if [ -n "{{sbom}}" ]; then args+=(--sbom-command python3 -c "import pathlib, sys; sys.stdout.write(pathlib.Path('{{sbom}}').read_text())"); fi; \
    python3 scripts/check-vulnerability-response.py "${args[@]}"

check-public-signing artifact release_zip notary_submit_json notary_log_json stapling_exception_json="" stapler_evidence_json="" package_manifest="":
    @args=(--artifact "{{artifact}}" --release-zip "{{release_zip}}" --notary-submit-json "{{notary_submit_json}}" --notary-log-json "{{notary_log_json}}"); \
    if [ -n "{{stapling_exception_json}}" ]; then args+=(--stapling-exception-json "{{stapling_exception_json}}"); fi; \
    if [ -n "{{stapler_evidence_json}}" ]; then args+=(--stapler-evidence-json "{{stapler_evidence_json}}"); fi; \
    if [ -n "{{package_manifest}}" ]; then args+=(--package-manifest "{{package_manifest}}"); fi; \
    python3 scripts/check-public-signing.py "${args[@]}"

perf-baseline: size bench-cold-start bench-swift-cold-start bench-idle-rss bench-first-eval check-dep-delta

perf: perf-baseline

license-report lane="full":
    @python3 scripts/license-report.py --lane "{{lane}}"

sbom output="dist/release/EcritumRuntime.xcframework.zip.spdx.json" lane="full":
    @python3 scripts/license-report.py --lane "{{lane}}" > "{{output}}"

license-report-strict lane="full":
    @python3 scripts/license-report.py --strict --lane "{{lane}}"

third-party-notices output="THIRD_PARTY_NOTICES.md" lane="full":
    @SOURCE_DATE_EPOCH=0 python3 scripts/license-report.py --notices --lane "{{lane}}" > "{{output}}"

release-check lane="":
    @args=(); \
    if [ -n "{{lane}}" ]; then args+=(--lane "{{lane}}"); fi; \
    scripts/release-check.sh "${args[@]}"

release-check-public lane="core" notary_submit_json="" notary_log_json="" stapling_exception_json="" stapler_evidence_json="":
    @test -n "{{notary_submit_json}}" || { echo "release-check-public requires notary_submit_json" >&2; exit 2; }; \
    test -n "{{notary_log_json}}" || { echo "release-check-public requires notary_log_json" >&2; exit 2; }; \
    args=(--lane "{{lane}}" --public --notary-submit-json "{{notary_submit_json}}" --notary-log-json "{{notary_log_json}}"); \
    if [ -n "{{stapling_exception_json}}" ]; then args+=(--stapling-exception-json "{{stapling_exception_json}}"); fi; \
    if [ -n "{{stapler_evidence_json}}" ]; then args+=(--stapler-evidence-json "{{stapler_evidence_json}}"); fi; \
    scripts/release-check.sh "${args[@]}"

clean:
    rm -rf .build target build dist native/target
