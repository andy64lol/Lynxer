# Repository Understanding

## Purpose

Understand the repository as a whole before making changes.

Do not assume that a single file represents the entire project. Read the repository structure, relevant source files, configuration, documentation, tests, build system, dependencies, and existing conventions before modifying code.

## Core Rule

Before changing anything:

1. Inspect the repository tree.
2. Identify the project's language(s), framework(s), build system, package manager, and entry points.
3. Read the main documentation.
4. Read configuration and build files.
5. Read the relevant source code.
6. Read related tests.
7. Trace how the relevant components interact.
8. Check existing conventions and architecture.
9. Only then decide how to implement the requested change.

Do not immediately start editing after finding the first relevant file.

## Repository Discovery

Start by inspecting the repository.

Look for:

* README files
* Documentation
* Source directories
* Test directories
* Examples
* Build scripts
* CI workflows
* Package manifests
* Dependency files
* Configuration files
* Generated-code directories
* Scripts
* Assets
* Compiler/interpreter entry points
* Public APIs
* Native bindings
* Platform-specific code

Examples of important files:

* `README.md`
* `CONTRIBUTING.md`
* `Cargo.toml`
* `Cargo.lock`
* `Makefile`
* `CMakeLists.txt`
* `package.json`
* `pyproject.toml`
* `requirements.txt`
* `go.mod`
* `.github/workflows/*`
* configuration files
* test configuration

## Read the Whole Repository

"Read the whole repository" means building a useful understanding of all important project components.

Do not blindly dump every binary, generated file, dependency, build artifact, cache, or vendored library into context.

Prioritize:

1. Human-written source code
2. Project documentation
3. Tests
4. Build configuration
5. CI configuration
6. Dependency manifests
7. Examples
8. Scripts
9. Public interfaces

Skip or summarize:

* `.git/`
* build output
* caches
* downloaded dependencies
* generated binaries
* large generated files
* lockfiles when they are not relevant to the task
* vendored third-party source unless the task requires it

If the repository is small, read essentially everything.

If the repository is large, inspect everything structurally and read all files relevant to the requested change.

## Architecture Understanding

Determine:

* What the program does
* Where execution begins
* How input enters the system
* How data flows through the system
* Which modules depend on which
* Where important state is stored
* Where errors are handled
* Where public APIs are defined
* How components communicate
* How the project is built
* How tests are executed
* How releases are produced

Create a mental model of the architecture before editing.

## Follow Dependencies

When a file references another component, inspect that component when it is relevant.

For example:

```text
main
 └── parser
      └── AST
           └── compiler
                └── runtime
```

Do not modify `parser` without understanding how its output is consumed by the compiler.

Do not modify an API without checking its callers.

Do not modify a data structure without checking where it is created, modified, serialized, and consumed.

## Search Before Editing

Before changing a symbol, search the repository for:

* Its definition
* Its usages
* Related functions
* Related types
* Tests
* Documentation
* Configuration
* Error messages
* CLI references

Prefer repository-wide search over guessing.

## Tests

Inspect existing tests before implementing changes.

Determine:

* What behavior is currently tested
* Which test framework is used
* How tests are organized
* Whether fixtures exist
* Whether integration tests exist
* Whether regression tests are expected

After making a change, run the most relevant tests.

If possible, also run:

* formatter
* compiler/type checker
* linter
* unit tests
* integration tests
* project-specific validation

## Existing Conventions

Follow the repository's existing style.

Inspect:

* Naming conventions
* File organization
* Error handling
* Comments
* API design
* Formatting
* Import organization
* Module structure
* Test style

Do not introduce a completely different style just because it is personally preferred.

## Do Not Rewrite Unnecessarily

Make the smallest change that correctly solves the requested problem.

Do not:

* Rewrite unrelated code
* Refactor unrelated modules
* Rename unrelated APIs
* Replace dependencies without a reason
* Change architecture unnecessarily
* Reformat unrelated files

Preserve existing behavior unless the task explicitly requires changing it.

## Before Implementation

Before editing, internally establish:

```text
Repository:
- Purpose:
- Main language(s):
- Build system:
- Package manager:
- Entry point:
- Main modules:
- Test system:
- Relevant configuration:
- Relevant dependencies:
- Relevant files:
- Existing behavior:
- Requested behavior:
```

Use this understanding to determine the implementation.

## After Implementation

After editing:

1. Review the diff.
2. Check for accidental changes.
3. Verify formatting.
4. Run relevant tests.
5. Run the build/type checker if applicable.
6. Check that public APIs remain consistent.
7. Check that documentation is still accurate.
8. Report what changed and what was verified.

## Important

Never claim to understand the repository without inspecting it.

Never claim a change is correct merely because the modified file looks correct.

Repository-level changes require repository-level understanding.
