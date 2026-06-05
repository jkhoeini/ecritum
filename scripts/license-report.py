#!/usr/bin/env python3
import argparse
import datetime as dt
import json
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


POM_NS = {"m": "http://maven.apache.org/POM/4.0.0"}


def text_at(root, path):
    node = root.find(path, POM_NS)
    return node.text.strip() if node is not None and node.text else None


def pom_license(path):
    if not path.exists():
        return None
    root = ET.parse(path).getroot()
    license_node = root.find("m:licenses/m:license", POM_NS)
    return {
        "name": text_at(license_node, "m:name") if license_node is not None else None,
        "url": text_at(license_node, "m:url") if license_node is not None else None,
    }


def spdx_license_expression(license_name):
    mapping = {
        "Universal Permissive License, Version 1.0": "UPL-1.0",
        "Eclipse Public License 1.0": "EPL-1.0",
        "Eclipse Public License 2.0": "EPL-2.0",
    }
    if license_name in (None, "NOASSERTION", "UNKNOWN"):
        return "NOASSERTION"
    return mapping.get(license_name, "NOASSERTION")


def package(spdx_id, name, version, scope, license_name, created, download_location="NOASSERTION"):
    license_expression = spdx_license_expression(license_name)
    blocker = scope == "shipped" and license_expression == "NOASSERTION"
    return {
        "SPDXID": spdx_id,
        "name": name,
        "versionInfo": version,
        "downloadLocation": download_location,
        "filesAnalyzed": False,
        "licenseConcluded": license_expression,
        "licenseDeclared": license_expression,
        "copyrightText": "NOASSERTION",
        "annotations": [
            {
                "annotationType": "OTHER",
                "annotator": "Tool: scripts/license-report.py",
                "annotationDate": created,
                "comment": f"ecritum-scope={scope}; release-blocker={str(blocker).lower()}; license-name={license_name or 'NOASSERTION'}",
            }
        ]
    }


parser = argparse.ArgumentParser(description="Emit the M1 SPDX JSON license inventory.")
parser.add_argument("--strict", action="store_true", help="Exit nonzero when shipped license blockers exist.")
parser.add_argument("--native-pom", default="native/pom.xml")
parser.add_argument("--m2", default=str(Path.home() / ".m2" / "repository"))
args = parser.parse_args()

native_pom = Path(args.native_pom)
m2 = Path(args.m2)
graal_version = "25.0.2"
if native_pom.exists():
    root = ET.parse(native_pom).getroot()
    graal_version = text_at(root, "m:properties/m:graalvm.version") or graal_version

nativeimage_license = pom_license(m2 / "org" / "graalvm" / "sdk" / "nativeimage" / graal_version / f"nativeimage-{graal_version}.pom")
word_license = pom_license(m2 / "org" / "graalvm" / "sdk" / "word" / graal_version / f"word-{graal_version}.pom")
sci_license = pom_license(m2 / "org" / "babashka" / "sci" / "0.12.51" / "sci-0.12.51.pom")
clojure_license = pom_license(m2 / "org" / "clojure" / "clojure" / "1.10.3" / "clojure-1.10.3.pom")
spec_alpha_license = pom_license(m2 / "org" / "clojure" / "spec.alpha" / "0.2.194" / "spec.alpha-0.2.194.pom")
core_specs_alpha_license = pom_license(m2 / "org" / "clojure" / "core.specs.alpha" / "0.2.56" / "core.specs.alpha-0.2.56.pom")
edamame_license = pom_license(m2 / "borkdude" / "edamame" / "1.5.37" / "edamame-1.5.37.pom")
tools_reader_license = pom_license(m2 / "org" / "clojure" / "tools.reader" / "1.5.2" / "tools.reader-1.5.2.pom")
graal_locking_license = pom_license(m2 / "borkdude" / "graal.locking" / "0.0.2" / "graal.locking-0.0.2.pom")
if "SOURCE_DATE_EPOCH" in os.environ:
    created_at = dt.datetime.fromtimestamp(int(os.environ["SOURCE_DATE_EPOCH"]), dt.timezone.utc)
else:
    created_at = dt.datetime.now(dt.timezone.utc)
created = created_at.replace(microsecond=0).isoformat().replace("+00:00", "Z")

packages = [
    package(
        "SPDXRef-Package-EcritumRuntime",
        "EcritumRuntime.xcframework",
        "0.1.0-dev",
        "shipped",
        "NOASSERTION",
        created,
    ),
    package(
        "SPDXRef-Package-GraalVM-NativeImage-EmbeddedRuntime",
        "GraalVM Native Image embedded runtime code",
        graal_version,
        "shipped",
        "NOASSERTION",
        created,
        "https://www.graalvm.org/",
    ),
    package(
        "SPDXRef-Package-GraalVM-NativeImage-SDK",
        "org.graalvm.sdk:nativeimage",
        graal_version,
        "build",
        nativeimage_license["name"] if nativeimage_license else None,
        created,
        "https://github.com/oracle/graal",
    ),
    package(
        "SPDXRef-Package-SCI",
        "org.babashka:sci",
        "0.12.51",
        "shipped",
        sci_license["name"] if sci_license else None,
        created,
        "https://github.com/babashka/SCI",
    ),
    package(
        "SPDXRef-Package-Clojure",
        "org.clojure:clojure",
        "1.10.3",
        "shipped",
        clojure_license["name"] if clojure_license else None,
        created,
        "https://github.com/clojure/clojure",
    ),
    package(
        "SPDXRef-Package-Clojure-SpecAlpha",
        "org.clojure:spec.alpha",
        "0.2.194",
        "shipped",
        spec_alpha_license["name"] if spec_alpha_license else None,
        created,
        "https://github.com/clojure/spec.alpha",
    ),
    package(
        "SPDXRef-Package-Clojure-CoreSpecsAlpha",
        "org.clojure:core.specs.alpha",
        "0.2.56",
        "shipped",
        core_specs_alpha_license["name"] if core_specs_alpha_license else None,
        created,
        "https://github.com/clojure/core.specs.alpha",
    ),
    package(
        "SPDXRef-Package-Edamame",
        "borkdude:edamame",
        "1.5.37",
        "shipped",
        edamame_license["name"] if edamame_license else None,
        created,
        "https://github.com/borkdude/edamame",
    ),
    package(
        "SPDXRef-Package-Clojure-ToolsReader",
        "org.clojure:tools.reader",
        "1.5.2",
        "shipped",
        tools_reader_license["name"] if tools_reader_license else None,
        created,
        "https://github.com/clojure/tools.reader",
    ),
    package(
        "SPDXRef-Package-SCI-ImplTypes",
        "org.babashka:sci.impl.types",
        "0.0.2",
        "shipped",
        "Eclipse Public License 1.0",
        created,
        "https://clojars.org/org.babashka/sci.impl.types",
    ),
    package(
        "SPDXRef-Package-GraalLocking",
        "borkdude:graal.locking",
        "0.0.2",
        "shipped",
        graal_locking_license["name"] if graal_locking_license else None,
        created,
        "https://github.com/borkdude/graal.locking",
    ),
    package(
        "SPDXRef-Package-GraalVM-Word-SDK",
        "org.graalvm.sdk:word",
        graal_version,
        "build",
        word_license["name"] if word_license else None,
        created,
        "https://github.com/oracle/graal",
    ),
    package(
        "SPDXRef-Package-JUnit",
        "org.junit.jupiter:junit-jupiter",
        "5.14.1",
        "test",
        "Eclipse Public License 2.0",
        created,
        "https://junit.org/junit5/",
    ),
]

blockers = [
    f"{item['name']} has unknown shipped license"
    for item in packages
    if "ecritum-scope=shipped" in item["annotations"][0]["comment"]
    and "release-blocker=true" in item["annotations"][0]["comment"]
]

document = {
    "spdxVersion": "SPDX-2.3",
    "dataLicense": "CC0-1.0",
    "SPDXID": "SPDXRef-DOCUMENT",
    "name": "Ecritum M1 license inventory",
    "documentNamespace": "https://ecritum.dev/spdx/m1/ecritum-license-inventory",
    "creationInfo": {
        "created": created,
        "creators": ["Tool: scripts/license-report.py"],
    },
    "documentDescribes": ["SPDXRef-Package-EcritumRuntime"],
    "packages": packages,
    "annotations": [
        {
            "annotationType": "OTHER",
            "annotator": "Tool: scripts/license-report.py",
            "annotationDate": created,
            "comment": "ecritum-sbom-format=SPDX-2.3 JSON; unknown shipped licenses block release; "
            + "blockers="
            + json.dumps(blockers, separators=(",", ":")),
        }
    ],
}

print(json.dumps(document, indent=2, sort_keys=True))
if args.strict and blockers:
    for blocker in blockers:
        print(f"release license blocker: {blocker}", file=sys.stderr)
raise SystemExit(1 if args.strict and blockers else 0)
