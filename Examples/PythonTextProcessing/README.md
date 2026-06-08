# PythonTextProcessing

## Purpose

Process host-provided text in sandboxed Python and return a structured summary
the host prints. This is the text-analytics use case: the host owns a document,
Python computes line/word/character counts, the single most-common word, and an
upper-cased headline, then returns a dict. Runs under the deny-by-default policy
with NO capabilities granted.

## Demonstrated API

- `EcritumRuntime(.init(languages: [.python]))` — a deny-by-default,
  single-language runtime.
- `namespace.register(.init("document")) { call in ... }` returning an
  `EcritumValue.string` — called from Python as `ecritum.app.document()`.
- `context.eval(EcritumScript(source, language: .python, sourceName: "analyze.py"))`.
- Returning a Python dict that maps back to `EcritumValue.object`, then reading
  it on the host.

The Python program uses only builtins (`str` methods, `dict`, `sorted`, `len`).
The sandbox seals `__import__`, so `import` statements are rejected; the example
deliberately stays import-free. The most-common word is resolved deterministically
(highest count, then lexicographic) so ties never produce nondeterministic output.

## Build / Run

```sh
# from the repository root, ensure the local artifact exists:
mise exec -- just xcframework   # only if dist/local/EcritumRuntime.xcframework is missing

# build + run via the rollup recipe:
mise exec -- just example-python-text-processing

# or directly:
cd Examples/PythonTextProcessing
mise exec -- swift build
slice="macos-$(uname -m)"
DYLD_FRAMEWORK_PATH="../../dist/local/EcritumRuntime.xcframework/$slice" \
  .build/$(uname -m)-apple-macosx/debug/PythonTextProcessing
```

## Expected output

```
Text analysis:
  characters = 152
  headline = "RELEASE NOTES FOR ECRITUM"
  lines = 4
  topWord = "ecritum"
  topWordCount = 2
  words = 24
```

## Security notes

No capabilities are granted. The runtime uses the default
`EcritumPermissionPolicy.defaultDeny` (filesystem, network, process,
environment, clock, random, and log all denied). The document is passed as a
host-provided value rather than read from disk, so the example needs no
filesystem read root or any other capability — it is fully capability-free.
