# AGENTS.md

This file defines how coding agents should work in the PiCDPlayer repository.

It is not the source of truth for product specifications or architecture.
Use the repository documentation for those details.

## 1. Project context

PiCDPlayer is a Raspberry Pi based physical audio CD player.

Development is normally performed on macOS.
Linux/aarch64 builds are produced in the repository's containerized
cross-build environment.
Raspberry Pi is the runtime and hardware-integration target.

Do not treat successful macOS or container execution as evidence that
hardware-dependent behavior works on the Raspberry Pi.

## 2. Repository and documentation

Before making changes, inspect the existing implementation and read the
documentation relevant to the task.

Start with:

- `docs/README.md` — documentation index
- `<source>` — application source
- `<tests>` — automated tests
- `<scripts>` — build/deployment tooling

Follow references from `docs/README.md` rather than relying on this file
for architecture or product specifications.

In particular, consult the relevant documentation before changing:

- architecture or component boundaries
- Player/CD-DA behavior
- UI behavior or interfaces
- enrichment/metadata behavior
- build, packaging, installation, or deployment
- previously documented design decisions

## 3. Working principles

Before editing:

1. Understand the requested outcome and completion conditions.
2. Inspect the relevant existing implementation.
3. Read the applicable documentation.
4. Identify the smallest change required.

While editing:

- Keep changes within the requested scope.
- Do not modify unrelated code.
- Follow existing conventions unless the task explicitly changes them.
- Do not introduce abstractions solely for hypothetical future needs.
- Do not invent undocumented project behavior.

After editing:

1. Run applicable automated validation.
2. Inspect the complete diff.
3. Verify each requested condition explicitly.
4. Check whether documentation needs updating.
5. Report anything that could not be verified.

Do not infer success from the absence of errors.
Prefer observable evidence over assumptions about what should have happened.

## 4. Sources of truth

Use the component that owns a behavior as its source of truth.

- CMake defines build and installation contents.
- Build tooling defines supported build procedures.
- Packaging/deployment tooling defines installation and deployment behavior.
- systemd units define service behavior.
- Project documentation defines documented architecture and design decisions.

Do not duplicate authoritative configuration in another layer merely to
make a task pass.

When behavior changes, update the layer that owns that behavior.

## 5. Validation

Use the strongest validation available for the change.

Examples:

- compilation/build checks for source changes
- automated tests where available
- static or schema validation for configuration
- repository searches for mechanical replacements
- `git diff` / `git diff --check` for all changes

A command completing successfully is not sufficient evidence that the
requested outcome was achieved.

When a requirement can be checked mechanically, check it mechanically.

## 6. Hardware-dependent changes

Containerized Linux builds validate Linux/aarch64 build compatibility,
not Raspberry Pi hardware integration.

The following require target-side verification when affected:

- optical-drive behavior
- CD-DA reading
- ALSA/audio output
- HDMI/CEC
- DRM/KMS/display behavior
- systemd/runtime integration
- other hardware-dependent behavior

Clearly distinguish:

- verified locally
- verified in the Linux/aarch64 build environment
- verified on the Raspberry Pi
- not verified

Never report hardware behavior as verified unless it was actually tested
on the target.

## 7. Safety

Avoid destructive operations unless explicitly required.

In particular:

- do not use destructive synchronization such as unrestricted
  `rsync --delete`
- do not overwrite unrelated target files
- do not modify host or target configuration outside the task scope
- do not bypass repository build/deployment tooling without a documented reason

Prefer reversible changes.

## 8. Handling ambiguity and inconsistencies

If the issue description, implementation, documentation, tests, or this
file disagree:

1. Do not silently choose one.
2. Inspect relevant implementation, documentation, and history.
3. Identify the inconsistency explicitly.
4. Resolve it only when there is sufficient evidence.
5. Otherwise stop and report the ambiguity.

Do not invent a project convention to resolve missing information.

## 9. Completion criteria

A task is complete only when:

- the requested behavior is implemented
- applicable validation has passed
- the resulting diff has been inspected
- unrelated behavior has not intentionally changed
- relevant documentation is consistent with the implementation
- verification limitations are reported

When reporting completion, summarize:

- what changed
- what was validated
- where it was validated
- what remains unverified
