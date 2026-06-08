#!/usr/bin/env python3
import argparse
import datetime as dt
import hashlib
import json
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


POM_NS = {"m": "http://maven.apache.org/POM/4.0.0"}
ACCEPTED_GRAALVM_VERSION = "25.0.2"
ACCEPTED_GRAALVM_NATIVE_IMAGE_LICENSE_SHA256 = "11a8fe0c63dcff8bd8674b89a5895dfbcf5f7e5453cf0a33566c4b3fb64e404c"
ACCEPTED_GRAALVM_VERSION_SNIPPETS = [
    "native-image 25.0.2",
    "GraalVM Runtime Environment GraalVM CE 25.0.2+10.1",
    "Substrate VM GraalVM CE 25.0.2+10.1",
]
FIRST_PARTY_LICENSE_ID = "MIT"
FIRST_PARTY_LICENSE_FILE = "LICENSE"
FIRST_PARTY_LICENSE_SHA256 = "41d9a76b60d9da5bb4b77b00ed219efff21ac9cee12f764aa9e8200f252f9f87"
FIRST_PARTY_COPYRIGHT = "Copyright (c) 2026 Ecritum contributors"
POM_METADATA_ERRORS = []


def text_at(root, path):
    node = root.find(path, POM_NS)
    return node.text.strip() if node is not None and node.text else None


def pom_license(path, package_name=None, required=False):
    if not path.exists():
        if required:
            POM_METADATA_ERRORS.append(f"missing expected POM for {package_name or path.name}: {path}")
        return {"name": None, "url": None, "names": [], "urls": []}
    root = ET.parse(path).getroot()
    license_nodes = root.findall("m:licenses/m:license", POM_NS)
    if not license_nodes:
        license_nodes = root.findall("licenses/license")
    licenses = []
    for license_node in license_nodes:
        licenses.append({
            "name": text_at(license_node, "m:name") or text_at(license_node, "name"),
            "url": text_at(license_node, "m:url") or text_at(license_node, "url"),
        })
    if not licenses:
        licenses.append({"name": None, "url": None})
    return {
        "name": licenses[0]["name"],
        "url": licenses[0]["url"],
        "names": [item["name"] for item in licenses if item["name"]],
        "urls": [item["url"] for item in licenses if item["url"]],
    }


def missing_license():
    return {"name": None, "url": None, "names": [], "urls": []}


def shipped_pom_license(path, package_name):
    return pom_license(path, package_name, required=True)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def graalvm_home(graalvm_home_arg):
    if graalvm_home_arg:
        return Path(graalvm_home_arg)
    env_home = os.environ.get("ECRITUM_GRAALVM_HOME") or os.environ.get("JAVA_HOME")
    if env_home:
        return Path(env_home)
    native_image = shutil.which("native-image")
    if native_image:
        return Path(native_image).resolve().parents[1]
    return None


def graalvm_evidence_errors(graalvm_home_arg, graal_version):
    errors = []
    if graal_version != ACCEPTED_GRAALVM_VERSION:
        errors.append(
            f"GraalVM Native Image license evidence is accepted only for {ACCEPTED_GRAALVM_VERSION}; native/pom.xml resolved {graal_version}"
        )

    home = graalvm_home(graalvm_home_arg)
    if home is None:
        errors.append("could not locate GraalVM home for LICENSE_NATIVEIMAGE.txt evidence")
        native_image = shutil.which("native-image")
    else:
        license_path = home / "LICENSE_NATIVEIMAGE.txt"
        if not license_path.exists():
            errors.append(f"missing GraalVM Native Image license evidence: {license_path}")
        else:
            actual_sha256 = sha256_file(license_path)
            if actual_sha256 != ACCEPTED_GRAALVM_NATIVE_IMAGE_LICENSE_SHA256:
                errors.append(
                    "GraalVM Native Image license evidence hash mismatch: "
                    + f"{license_path} sha256={actual_sha256}, expected {ACCEPTED_GRAALVM_NATIVE_IMAGE_LICENSE_SHA256}"
                )
        home_native_image = home / "bin" / "native-image"
        if home_native_image.exists():
            native_image = str(home_native_image)
        elif graalvm_home_arg:
            errors.append(f"missing native-image under explicit GraalVM home: {home_native_image}")
            native_image = None
        else:
            native_image = shutil.which("native-image")

    if native_image is None and not graalvm_home_arg:
        native_image = shutil.which("native-image")
    if native_image is None:
        errors.append("could not locate native-image for GraalVM version evidence")
        return errors

    completed = subprocess.run(
        [native_image, "--version"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=15,
    )
    version_output = completed.stdout.strip()
    if completed.returncode != 0:
        errors.append(f"native-image --version exited {completed.returncode}: {version_output}")
        return errors
    for snippet in ACCEPTED_GRAALVM_VERSION_SNIPPETS:
        if snippet not in version_output:
            errors.append(f"native-image --version does not match ADR-011 evidence; missing: {snippet}")
    return errors


def first_party_license_errors(license_file_arg):
    license_path = Path(license_file_arg)
    if not license_path.is_file():
        return [f"missing first-party LICENSE file: {license_path}"]
    actual_sha256 = sha256_file(license_path)
    if actual_sha256 != FIRST_PARTY_LICENSE_SHA256:
        return [
            "first-party LICENSE hash mismatch: "
            + f"{license_path} sha256={actual_sha256}, expected {FIRST_PARTY_LICENSE_SHA256}"
        ]
    return []


def spdx_license_expression(license_name):
    mapping = {
        "Universal Permissive License, Version 1.0": "UPL-1.0",
        "Eclipse Public License 1.0": "EPL-1.0",
        "Eclipse Public License 2.0": "EPL-2.0",
        "GNU General Public License, version 2, with the Classpath Exception": "GPL-2.0-only WITH Classpath-exception-2.0",
        "Unicode/ICU License": "ICU",
        "MIT License": "MIT",
        "Python Software Foundation License": "PSF-2.0",
        "Bouncy Castle Licence": "LicenseRef-Bouncy-Castle",
        "New BSD License (3-clause BSD license)": "BSD-3-Clause",
        "Simplified BSD License (2-clause BSD license)": "BSD-2-Clause",
    }
    known_spdx = set(mapping.values())
    if isinstance(license_name, (list, tuple)):
        expressions = []
        for name in license_name:
            expression = spdx_license_expression(name)
            if expression == "NOASSERTION":
                return "NOASSERTION"
            expressions.append(expression)
        expressions = list(dict.fromkeys(expressions))
        return " AND ".join(expressions) if expressions else "NOASSERTION"
    if license_name in (None, "NOASSERTION", "UNKNOWN"):
        return "NOASSERTION"
    if license_name in known_spdx:
        return license_name
    return mapping.get(license_name, "NOASSERTION")


def purl_ref(locator, comment=None):
    ref = {
        "referenceCategory": "PACKAGE-MANAGER",
        "referenceType": "purl",
        "referenceLocator": locator,
    }
    if comment:
        ref["comment"] = comment
    return ref


def maven_purl(coordinates, version):
    group, artifact = coordinates.split(":", 1)
    return f"pkg:maven/{group}/{artifact}@{version}"


def license_names(license_info):
    if license_info is None:
        return None
    names = license_info.get("names") or []
    return names if names else license_info.get("name")


def license_name_text(license_name):
    if isinstance(license_name, (list, tuple)):
        return " AND ".join(license_name) if license_name else "NOASSERTION"
    return license_name or "NOASSERTION"


def package(
    spdx_id,
    name,
    version,
    scope,
    license_name,
    created,
    download_location="NOASSERTION",
    license_source=None,
    external_refs=None,
    copyright_text="NOASSERTION",
):
    license_expression = spdx_license_expression(license_name)
    blocker = scope == "shipped" and license_expression == "NOASSERTION"
    comment = (
        f"ecritum-scope={scope}; release-blocker={str(blocker).lower()}; "
        f"license-name={license_name_text(license_name)}"
    )
    if license_source:
        comment += f"; license-source={license_source}"
    result = {
        "SPDXID": spdx_id,
        "name": name,
        "versionInfo": version,
        "downloadLocation": download_location,
        "filesAnalyzed": False,
        "licenseConcluded": license_expression,
        "licenseDeclared": license_expression,
        "copyrightText": copyright_text,
        "annotations": [
            {
                "annotationType": "OTHER",
                "annotator": "Tool: scripts/license-report.py",
                "annotationDate": created,
                "comment": comment,
            }
        ]
    }
    if external_refs:
        result["externalRefs"] = external_refs
    return result


def annotation_value(item, key):
    prefix = f"{key}="
    comment = item["annotations"][0]["comment"]
    for part in comment.split(";"):
        part = part.strip()
        if part.startswith(prefix):
            return part[len(prefix):]
    return None


def markdown_escape(value):
    return str(value).replace("|", "\\|").replace("\n", " ")


def markdown_table(headers, rows):
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(markdown_escape(value) for value in row) + " |")
    return "\n".join(lines)


def render_markdown_notices(document, blockers):
    lines = [
        "# Ecritum Third Party Notices",
        "",
        "Generated from `scripts/license-report.py`.",
        "",
        "This file is an inventory and notice index. It does not include full license text; public release packaging must carry the required upstream license texts for the selected distribution format.",
        "",
        "## Release Blockers",
        "",
    ]
    if blockers:
        lines.extend(f"- {blocker}" for blocker in blockers)
    else:
        lines.append("- None")
    for scope, title in [
        ("shipped", "Shipped Components"),
        ("build", "Build-Time Components"),
        ("test", "Test Components"),
    ]:
        scoped = sorted(
            [item for item in document["packages"] if annotation_value(item, "ecritum-scope") == scope],
            key=lambda item: item["name"].lower(),
        )
        rows = [
            [
                item["name"],
                item["versionInfo"],
                item["licenseConcluded"],
                annotation_value(item, "license-source") or item["downloadLocation"],
            ]
            for item in scoped
        ]
        lines.extend(["", f"## {title}", "", markdown_table(["Component", "Version", "SPDX", "License source"], rows)])
    return "\n".join(lines) + "\n"


parser = argparse.ArgumentParser(description="Emit the M7 SPDX JSON license inventory or third-party notices.")
parser.add_argument("--strict", action="store_true", help="Exit nonzero when shipped license blockers exist.")
parser.add_argument("--notices", action="store_true", help="Emit generated THIRD_PARTY_NOTICES.md Markdown instead of SPDX JSON.")
parser.add_argument("--native-pom", default="native/pom.xml")
parser.add_argument("--m2", default=str(Path.home() / ".m2" / "repository"))
parser.add_argument("--graalvm-home", default=None, help="GraalVM home used to verify Native Image license evidence in strict mode.")
parser.add_argument("--first-party-license-file", default=FIRST_PARTY_LICENSE_FILE, help="First-party Ecritum license file to validate in strict mode.")
args = parser.parse_args()

native_pom = Path(args.native_pom)
m2 = Path(args.m2)
graal_version = "25.0.2"
if native_pom.exists():
    root = ET.parse(native_pom).getroot()
    graal_version = text_at(root, "m:properties/m:graalvm.version") or graal_version

nativeimage_license = pom_license(m2 / "org" / "graalvm" / "sdk" / "nativeimage" / graal_version / f"nativeimage-{graal_version}.pom", "org.graalvm.sdk:nativeimage", required=True)
word_license = pom_license(m2 / "org" / "graalvm" / "sdk" / "word" / graal_version / f"word-{graal_version}.pom", "org.graalvm.sdk:word", required=True)
polyglot_license = shipped_pom_license(m2 / "org" / "graalvm" / "polyglot" / "polyglot" / graal_version / f"polyglot-{graal_version}.pom", "org.graalvm.polyglot:polyglot")
collections_license = shipped_pom_license(m2 / "org" / "graalvm" / "sdk" / "collections" / graal_version / f"collections-{graal_version}.pom", "org.graalvm.sdk:collections")
js_language_license = shipped_pom_license(m2 / "org" / "graalvm" / "js" / "js-language" / graal_version / f"js-language-{graal_version}.pom", "org.graalvm.js:js-language")
polyglot_python_license = shipped_pom_license(m2 / "org" / "graalvm" / "polyglot" / "python" / graal_version / f"python-{graal_version}.pom", "org.graalvm.polyglot:python")
graal_python_license = shipped_pom_license(m2 / "org" / "graalvm" / "python" / "python" / graal_version / f"python-{graal_version}.pom", "org.graalvm.python:python")
python_language_license = shipped_pom_license(m2 / "org" / "graalvm" / "python" / "python-language" / graal_version / f"python-language-{graal_version}.pom", "org.graalvm.python:python-language")
python_resources_license = shipped_pom_license(m2 / "org" / "graalvm" / "python" / "python-resources" / graal_version / f"python-resources-{graal_version}.pom", "org.graalvm.python:python-resources")
regex_license = shipped_pom_license(m2 / "org" / "graalvm" / "regex" / "regex" / graal_version / f"regex-{graal_version}.pom", "org.graalvm.regex:regex")
truffle_api_license = shipped_pom_license(m2 / "org" / "graalvm" / "truffle" / "truffle-api" / graal_version / f"truffle-api-{graal_version}.pom", "org.graalvm.truffle:truffle-api")
profiler_tool_license = shipped_pom_license(m2 / "org" / "graalvm" / "tools" / "profiler-tool" / graal_version / f"profiler-tool-{graal_version}.pom", "org.graalvm.tools:profiler-tool")
shadowed_json_license = shipped_pom_license(m2 / "org" / "graalvm" / "shadowed" / "json" / graal_version / f"json-{graal_version}.pom", "org.graalvm.shadowed:json")
icu4j_license = shipped_pom_license(m2 / "org" / "graalvm" / "shadowed" / "icu4j" / graal_version / f"icu4j-{graal_version}.pom", "org.graalvm.shadowed:icu4j")
xz_license = shipped_pom_license(m2 / "org" / "graalvm" / "shadowed" / "xz" / graal_version / f"xz-{graal_version}.pom", "org.graalvm.shadowed:xz")
truffle_nfi_license = shipped_pom_license(m2 / "org" / "graalvm" / "truffle" / "truffle-nfi" / graal_version / f"truffle-nfi-{graal_version}.pom", "org.graalvm.truffle:truffle-nfi")
truffle_nfi_libffi_license = shipped_pom_license(m2 / "org" / "graalvm" / "truffle" / "truffle-nfi-libffi" / graal_version / f"truffle-nfi-libffi-{graal_version}.pom", "org.graalvm.truffle:truffle-nfi-libffi")
truffle_nfi_panama_license = shipped_pom_license(m2 / "org" / "graalvm" / "truffle" / "truffle-nfi-panama" / graal_version / f"truffle-nfi-panama-{graal_version}.pom", "org.graalvm.truffle:truffle-nfi-panama")
truffle_runtime_license = shipped_pom_license(m2 / "org" / "graalvm" / "truffle" / "truffle-runtime" / graal_version / f"truffle-runtime-{graal_version}.pom", "org.graalvm.truffle:truffle-runtime")
jniutils_license = shipped_pom_license(m2 / "org" / "graalvm" / "sdk" / "jniutils" / graal_version / f"jniutils-{graal_version}.pom", "org.graalvm.sdk:jniutils")
truffle_compiler_license = shipped_pom_license(m2 / "org" / "graalvm" / "truffle" / "truffle-compiler" / graal_version / f"truffle-compiler-{graal_version}.pom", "org.graalvm.truffle:truffle-compiler")
bouncycastle_bcprov_license = shipped_pom_license(m2 / "org" / "bouncycastle" / "bcprov-jdk18on" / "1.78.1" / "bcprov-jdk18on-1.78.1.pom", "org.bouncycastle:bcprov-jdk18on")
bouncycastle_bcpkix_license = shipped_pom_license(m2 / "org" / "bouncycastle" / "bcpkix-jdk18on" / "1.78.1" / "bcpkix-jdk18on-1.78.1.pom", "org.bouncycastle:bcpkix-jdk18on")
bouncycastle_bcutil_license = shipped_pom_license(m2 / "org" / "bouncycastle" / "bcutil-jdk18on" / "1.78.1" / "bcutil-jdk18on-1.78.1.pom", "org.bouncycastle:bcutil-jdk18on")
sci_license = pom_license(m2 / "org" / "babashka" / "sci" / "0.12.51" / "sci-0.12.51.pom", "org.babashka:sci", required=True)
clojure_license = pom_license(m2 / "org" / "clojure" / "clojure" / "1.10.3" / "clojure-1.10.3.pom", "org.clojure:clojure", required=True)
spec_alpha_license = pom_license(m2 / "org" / "clojure" / "spec.alpha" / "0.2.194" / "spec.alpha-0.2.194.pom", "org.clojure:spec.alpha", required=True)
core_specs_alpha_license = pom_license(m2 / "org" / "clojure" / "core.specs.alpha" / "0.2.56" / "core.specs.alpha-0.2.56.pom", "org.clojure:core.specs.alpha", required=True)
edamame_license = pom_license(m2 / "borkdude" / "edamame" / "1.5.37" / "edamame-1.5.37.pom", "borkdude:edamame", required=True)
tools_reader_license = pom_license(m2 / "org" / "clojure" / "tools.reader" / "1.5.2" / "tools.reader-1.5.2.pom", "org.clojure:tools.reader", required=True)
graal_locking_license = pom_license(m2 / "borkdude" / "graal.locking" / "0.0.2" / "graal.locking-0.0.2.pom", "borkdude:graal.locking", required=True)
luaj_jme_license = shipped_pom_license(m2 / "org" / "luaj" / "luaj-jme" / "3.0.1" / "luaj-jme-3.0.1.pom", "org.luaj:luaj-jme")
# M12-002 Slice 2: Ruby (TruffleRuby) is now a default shipped language with LLVM
# EXCLUDED per ADR-0028. These are the seven net-new shipped coordinates Ruby adds
# beyond the pre-Ruby 4-language baseline (truffle-api/regex/truffle-nfi*/
# truffle-runtime/jniutils/truffle-compiler were already shipped via Python). The
# six org.graalvm.llvm:* artifacts and org.graalvm.shadowed:antlr4 are NOT shipped
# (antlr4 is transitive under the excluded llvm-language).
truffleruby_version = "34.0.1"
truffleruby_license = shipped_pom_license(m2 / "dev" / "truffleruby" / "truffleruby" / truffleruby_version / f"truffleruby-{truffleruby_version}.pom", "dev.truffleruby:truffleruby")
truffleruby_runtime_license = shipped_pom_license(m2 / "dev" / "truffleruby" / "internal" / "runtime" / truffleruby_version / f"runtime-{truffleruby_version}.pom", "dev.truffleruby.internal:runtime")
truffleruby_resources_license = shipped_pom_license(m2 / "dev" / "truffleruby" / "internal" / "resources" / truffleruby_version / f"resources-{truffleruby_version}.pom", "dev.truffleruby.internal:resources")
truffleruby_annotations_license = shipped_pom_license(m2 / "dev" / "truffleruby" / "internal" / "annotations" / truffleruby_version / f"annotations-{truffleruby_version}.pom", "dev.truffleruby.internal:annotations")
truffleruby_shared_license = shipped_pom_license(m2 / "dev" / "truffleruby" / "internal" / "shared" / truffleruby_version / f"shared-{truffleruby_version}.pom", "dev.truffleruby.internal:shared")
truffleruby_joni_license = shipped_pom_license(m2 / "dev" / "truffleruby" / "shadowed" / "joni" / truffleruby_version / f"joni-{truffleruby_version}.pom", "dev.truffleruby.shadowed:joni")
jcodings_license = shipped_pom_license(m2 / "org" / "graalvm" / "shadowed" / "jcodings" / graal_version / f"jcodings-{graal_version}.pom", "org.graalvm.shadowed:jcodings")
if "SOURCE_DATE_EPOCH" in os.environ:
    created_at = dt.datetime.fromtimestamp(int(os.environ["SOURCE_DATE_EPOCH"]), dt.timezone.utc)
else:
    created_at = dt.datetime.now(dt.timezone.utc)
created = created_at.replace(microsecond=0).isoformat().replace("+00:00", "Z")

# The TruffleRuby resources package embeds the Ruby standard-library tree. The
# denied runtime surface is recorded as a SEPARATE annotation object (never
# appended to annotations[0].comment, whose ';'/'=' parser would corrupt on
# tokens like 'ffi-fiddle'). Scope/license-source stay on annotations[0].
truffleruby_resources_package = package(
    "SPDXRef-Package-TruffleRuby-Resources",
    "dev.truffleruby.internal:resources",
    truffleruby_version,
    "shipped",
    license_names(truffleruby_resources_license),
    created,
    "https://github.com/oracle/truffleruby",
    external_refs=[purl_ref(maven_purl("dev.truffleruby.internal:resources", truffleruby_version))],
)
truffleruby_resources_package["annotations"].append({
    "annotationType": "OTHER",
    "annotator": "Tool: scripts/license-report.py",
    "annotationDate": created,
    "comment": "ruby-denied-surface=rubygems,bundler,openssl,sockets,ffi-fiddle,native-extensions,native-so",
})

packages = [
    package(
        "SPDXRef-Package-EcritumRuntime",
        "EcritumRuntime.xcframework",
        "0.1.0",
        "shipped",
        FIRST_PARTY_LICENSE_ID,
        created,
        license_source=Path(args.first_party_license_file).name,
        external_refs=[purl_ref("pkg:generic/ecritum/EcritumRuntime.xcframework@0.1.0")],
        copyright_text=FIRST_PARTY_COPYRIGHT,
    ),
    package(
        "SPDXRef-Package-GraalVM-NativeImage-EmbeddedRuntime",
        "GraalVM Native Image embedded runtime code",
        graal_version,
        "shipped",
        "GNU General Public License, version 2, with the Classpath Exception",
        created,
        "https://www.graalvm.org/",
        "GraalVM Community 25.0.2 LICENSE_NATIVEIMAGE.txt sha256=11a8fe0c63dcff8bd8674b89a5895dfbcf5f7e5453cf0a33566c4b3fb64e404c and https://www.graalvm.org/faq/",
        [purl_ref(f"pkg:generic/oracle/graalvm-native-image-embedded-runtime@{graal_version}")],
    ),
    package(
        "SPDXRef-Package-GraalVM-NativeImage-SDK",
        "org.graalvm.sdk:nativeimage",
        graal_version,
        "build",
        license_names(nativeimage_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.sdk:nativeimage", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-Polyglot",
        "org.graalvm.polyglot:polyglot",
        graal_version,
        "shipped",
        license_names(polyglot_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.polyglot:polyglot", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-Collections",
        "org.graalvm.sdk:collections",
        graal_version,
        "shipped",
        license_names(collections_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.sdk:collections", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalJS-Language",
        "org.graalvm.js:js-language",
        graal_version,
        "shipped",
        license_names(js_language_license),
        created,
        "https://github.com/oracle/graaljs",
        external_refs=[purl_ref(maven_purl("org.graalvm.js:js-language", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalPy-Polyglot",
        "org.graalvm.polyglot:python",
        graal_version,
        "shipped",
        license_names(polyglot_python_license),
        created,
        "https://github.com/oracle/graalpython",
        external_refs=[purl_ref(maven_purl("org.graalvm.polyglot:python", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalPy-Python-POM",
        "org.graalvm.python:python",
        graal_version,
        "shipped",
        license_names(graal_python_license),
        created,
        "https://github.com/oracle/graalpython",
        external_refs=[purl_ref(maven_purl("org.graalvm.python:python", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalPy-Language",
        "org.graalvm.python:python-language",
        graal_version,
        "shipped",
        license_names(python_language_license),
        created,
        "https://github.com/oracle/graalpython",
        external_refs=[purl_ref(maven_purl("org.graalvm.python:python-language", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalPy-Resources",
        "org.graalvm.python:python-resources",
        graal_version,
        "shipped",
        license_names(python_resources_license),
        created,
        "https://github.com/oracle/graalpython",
        external_refs=[purl_ref(maven_purl("org.graalvm.python:python-resources", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-Regex",
        "org.graalvm.regex:regex",
        graal_version,
        "shipped",
        license_names(regex_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.regex:regex", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-TruffleAPI",
        "org.graalvm.truffle:truffle-api",
        graal_version,
        "shipped",
        license_names(truffle_api_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.truffle:truffle-api", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-ProfilerTool",
        "org.graalvm.tools:profiler-tool",
        graal_version,
        "shipped",
        license_names(profiler_tool_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.tools:profiler-tool", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-ShadowedJSON",
        "org.graalvm.shadowed:json",
        graal_version,
        "shipped",
        license_names(shadowed_json_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.shadowed:json", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-ICU4J",
        "org.graalvm.shadowed:icu4j",
        graal_version,
        "shipped",
        license_names(icu4j_license),
        created,
        "https://github.com/unicode-org/icu",
        external_refs=[purl_ref(maven_purl("org.graalvm.shadowed:icu4j", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-XZ",
        "org.graalvm.shadowed:xz",
        graal_version,
        "shipped",
        license_names(xz_license),
        created,
        "https://tukaani.org/xz/java.html",
        external_refs=[purl_ref(maven_purl("org.graalvm.shadowed:xz", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-TruffleNFI",
        "org.graalvm.truffle:truffle-nfi",
        graal_version,
        "shipped",
        license_names(truffle_nfi_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.truffle:truffle-nfi", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-TruffleNFILibffi",
        "org.graalvm.truffle:truffle-nfi-libffi",
        graal_version,
        "shipped",
        license_names(truffle_nfi_libffi_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.truffle:truffle-nfi-libffi", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-TruffleNFIPanama",
        "org.graalvm.truffle:truffle-nfi-panama",
        graal_version,
        "shipped",
        license_names(truffle_nfi_panama_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.truffle:truffle-nfi-panama", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-TruffleRuntime",
        "org.graalvm.truffle:truffle-runtime",
        graal_version,
        "shipped",
        license_names(truffle_runtime_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.truffle:truffle-runtime", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-JNIUtils",
        "org.graalvm.sdk:jniutils",
        graal_version,
        "shipped",
        license_names(jniutils_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.sdk:jniutils", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-TruffleCompiler",
        "org.graalvm.truffle:truffle-compiler",
        graal_version,
        "shipped",
        license_names(truffle_compiler_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.truffle:truffle-compiler", graal_version))],
    ),
    package(
        "SPDXRef-Package-BouncyCastle-BCProv",
        "org.bouncycastle:bcprov-jdk18on",
        "1.78.1",
        "shipped",
        license_names(bouncycastle_bcprov_license),
        created,
        "https://github.com/bcgit/bc-java",
        external_refs=[purl_ref(maven_purl("org.bouncycastle:bcprov-jdk18on", "1.78.1"))],
    ),
    package(
        "SPDXRef-Package-BouncyCastle-BCPKIX",
        "org.bouncycastle:bcpkix-jdk18on",
        "1.78.1",
        "shipped",
        license_names(bouncycastle_bcpkix_license),
        created,
        "https://github.com/bcgit/bc-java",
        external_refs=[purl_ref(maven_purl("org.bouncycastle:bcpkix-jdk18on", "1.78.1"))],
    ),
    package(
        "SPDXRef-Package-BouncyCastle-BCUtil",
        "org.bouncycastle:bcutil-jdk18on",
        "1.78.1",
        "shipped",
        license_names(bouncycastle_bcutil_license),
        created,
        "https://github.com/bcgit/bc-java",
        external_refs=[purl_ref(maven_purl("org.bouncycastle:bcutil-jdk18on", "1.78.1"))],
    ),
    package(
        "SPDXRef-Package-SCI",
        "org.babashka:sci",
        "0.12.51",
        "shipped",
        license_names(sci_license),
        created,
        "https://github.com/babashka/SCI",
        external_refs=[purl_ref(maven_purl("org.babashka:sci", "0.12.51"))],
    ),
    package(
        "SPDXRef-Package-Clojure",
        "org.clojure:clojure",
        "1.10.3",
        "shipped",
        license_names(clojure_license),
        created,
        "https://github.com/clojure/clojure",
        external_refs=[purl_ref(maven_purl("org.clojure:clojure", "1.10.3"))],
    ),
    package(
        "SPDXRef-Package-Clojure-SpecAlpha",
        "org.clojure:spec.alpha",
        "0.2.194",
        "shipped",
        license_names(spec_alpha_license),
        created,
        "https://github.com/clojure/spec.alpha",
        external_refs=[purl_ref(maven_purl("org.clojure:spec.alpha", "0.2.194"))],
    ),
    package(
        "SPDXRef-Package-Clojure-CoreSpecsAlpha",
        "org.clojure:core.specs.alpha",
        "0.2.56",
        "shipped",
        license_names(core_specs_alpha_license),
        created,
        "https://github.com/clojure/core.specs.alpha",
        external_refs=[purl_ref(maven_purl("org.clojure:core.specs.alpha", "0.2.56"))],
    ),
    package(
        "SPDXRef-Package-Edamame",
        "borkdude:edamame",
        "1.5.37",
        "shipped",
        license_names(edamame_license),
        created,
        "https://github.com/borkdude/edamame",
        external_refs=[purl_ref(maven_purl("borkdude:edamame", "1.5.37"))],
    ),
    package(
        "SPDXRef-Package-Clojure-ToolsReader",
        "org.clojure:tools.reader",
        "1.5.2",
        "shipped",
        license_names(tools_reader_license),
        created,
        "https://github.com/clojure/tools.reader",
        external_refs=[purl_ref(maven_purl("org.clojure:tools.reader", "1.5.2"))],
    ),
    package(
        "SPDXRef-Package-SCI-ImplTypes",
        "org.babashka:sci.impl.types",
        "0.0.2",
        "shipped",
        "Eclipse Public License 1.0",
        created,
        "https://clojars.org/org.babashka/sci.impl.types",
        external_refs=[purl_ref(maven_purl("org.babashka:sci.impl.types", "0.0.2"))],
    ),
    package(
        "SPDXRef-Package-GraalLocking",
        "borkdude:graal.locking",
        "0.0.2",
        "shipped",
        license_names(graal_locking_license),
        created,
        "https://github.com/borkdude/graal.locking",
        external_refs=[purl_ref(maven_purl("borkdude:graal.locking", "0.0.2"))],
    ),
    package(
        "SPDXRef-Package-LuaJ-JME",
        "org.luaj:luaj-jme",
        "3.0.1",
        "shipped",
        license_names(luaj_jme_license),
        created,
        "https://sourceforge.net/projects/luaj/",
        external_refs=[purl_ref(maven_purl("org.luaj:luaj-jme", "3.0.1"))],
    ),
    package(
        "SPDXRef-Package-TruffleRuby",
        "dev.truffleruby:truffleruby",
        truffleruby_version,
        "shipped",
        license_names(truffleruby_license),
        created,
        "https://github.com/oracle/truffleruby",
        external_refs=[purl_ref(maven_purl("dev.truffleruby:truffleruby", truffleruby_version))],
    ),
    package(
        "SPDXRef-Package-TruffleRuby-Runtime",
        "dev.truffleruby.internal:runtime",
        truffleruby_version,
        "shipped",
        license_names(truffleruby_runtime_license),
        created,
        "https://github.com/oracle/truffleruby",
        external_refs=[purl_ref(maven_purl("dev.truffleruby.internal:runtime", truffleruby_version))],
    ),
    truffleruby_resources_package,
    package(
        "SPDXRef-Package-TruffleRuby-Annotations",
        "dev.truffleruby.internal:annotations",
        truffleruby_version,
        "shipped",
        license_names(truffleruby_annotations_license),
        created,
        "https://github.com/oracle/truffleruby",
        external_refs=[purl_ref(maven_purl("dev.truffleruby.internal:annotations", truffleruby_version))],
    ),
    package(
        "SPDXRef-Package-TruffleRuby-Shared",
        "dev.truffleruby.internal:shared",
        truffleruby_version,
        "shipped",
        license_names(truffleruby_shared_license),
        created,
        "https://github.com/oracle/truffleruby",
        external_refs=[purl_ref(maven_purl("dev.truffleruby.internal:shared", truffleruby_version))],
    ),
    package(
        "SPDXRef-Package-TruffleRuby-Joni",
        "dev.truffleruby.shadowed:joni",
        truffleruby_version,
        "shipped",
        license_names(truffleruby_joni_license),
        created,
        "https://github.com/oracle/truffleruby",
        external_refs=[purl_ref(maven_purl("dev.truffleruby.shadowed:joni", truffleruby_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-JCodings",
        "org.graalvm.shadowed:jcodings",
        graal_version,
        "shipped",
        license_names(jcodings_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.shadowed:jcodings", graal_version))],
    ),
    package(
        "SPDXRef-Package-GraalVM-Word-SDK",
        "org.graalvm.sdk:word",
        graal_version,
        "build",
        license_names(word_license),
        created,
        "https://github.com/oracle/graal",
        external_refs=[purl_ref(maven_purl("org.graalvm.sdk:word", graal_version))],
    ),
    package(
        "SPDXRef-Package-JUnit",
        "org.junit.jupiter:junit-jupiter",
        "5.14.1",
        "test",
        "Eclipse Public License 2.0",
        created,
        "https://junit.org/junit5/",
        external_refs=[purl_ref(maven_purl("org.junit.jupiter:junit-jupiter", "5.14.1"))],
    ),
]

blockers = [
    f"{item['name']} has unknown shipped license"
    for item in packages
    if "ecritum-scope=shipped" in item["annotations"][0]["comment"]
    and "release-blocker=true" in item["annotations"][0]["comment"]
]
strict_blockers = blockers + POM_METADATA_ERRORS
if args.strict:
    strict_blockers.extend(first_party_license_errors(args.first_party_license_file))
    strict_blockers.extend(graalvm_evidence_errors(args.graalvm_home, graal_version))

# M12-002 Slice 2: Ruby is a default shipped language (Clojure + JavaScript +
# Lua + Python + Ruby) in the single default artifact, with LLVM excluded per
# ADR-0028.
document_namespace = "https://ecritum.dev/spdx/ecritum-license-inventory"
included_runtimes = "clojure,javascript,lua,python,ruby"

document_annotations = [
    {
        "annotationType": "OTHER",
        "annotator": "Tool: scripts/license-report.py",
        "annotationDate": created,
        "comment": f"ecritum-sbom-format=SPDX-2.3 JSON; artifact-kind=default; included-runtimes={included_runtimes}; unknown shipped licenses block release; "
        + "blockers="
        + json.dumps(blockers + POM_METADATA_ERRORS, separators=(",", ":")),
    }
]

document = {
    "spdxVersion": "SPDX-2.3",
    "dataLicense": "CC0-1.0",
    "SPDXID": "SPDXRef-DOCUMENT",
    "name": "Ecritum license inventory",
    "documentNamespace": document_namespace,
    "creationInfo": {
        "created": created,
        "creators": ["Tool: scripts/license-report.py"],
    },
    "documentDescribes": ["SPDXRef-Package-EcritumRuntime"],
    "packages": packages,
    "annotations": document_annotations,
}

if args.notices:
    print(render_markdown_notices(document, blockers), end="")
else:
    print(json.dumps(document, indent=2, sort_keys=True))
if args.strict and strict_blockers:
    for blocker in strict_blockers:
        print(f"release license blocker: {blocker}", file=sys.stderr)
raise SystemExit(1 if args.strict and strict_blockers else 0)
