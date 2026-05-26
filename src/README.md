# Developer Guide

This document outlines the coding standards, development workflows, and
documentation practices for the `httpdirfs` project.

## Coding Standards

Adhering to a consistent coding style ensures the codebase remains readable and
maintainable.

### Naming Conventions

| Category                    | Convention                     | Example                    |
| :-------------------------- | :----------------------------- | :------------------------- |
| **External Variables**      | UPPERCASE                      | `GLOBAL_VAR`               |
| **Static Global Variables** | lowercase                      | `static_var`               |
| **Function Names**          | `TypeName_verb` or `verb_noun` | `Cache_read`, `get_config` |
| **Type Names**              | CamelCase                      | `CacheManager`, `Config`   |

### Indentation and Formatting

The project follows the **Kernighan & Ritchie (K&R)** style. We use
`clang-format` for automatic formatting.

Manual formatting can be triggered via Meson:

```bash
meson compile format
```

---

## Building and Debugging

The project supports two main build configurations: **Debug** and **Release**.

### Debug vs. Release Builds

| Feature                | Debug Build (`--buildtype=debug`)                               | Release Build (`--buildtype=release`)                      |
| :--------------------- | :-------------------------------------------------------------- | :--------------------------------------------------------- |
| **Optimization Level** | None (`-O0`) — code maps exactly to source lines.               | High (`-O3`) — optimized for performance and speed.        |
| **Debug Symbols**      | Enabled (`-g`) — for step-through debugging via GDB/LLDB.       | Omitted — minimal binary size.                             |
| **`DEBUG` Macro**      | Defined (`-DDEBUG`) — enables verbose debug logging by default. | Undefined — standard logging only, maximizing performance. |
| **Use Case**           | Local development, debugging, and bug reporting.                | Production deployment.                                     |

### Creating a Debug Build

By default, the project is configured with a release build configuration
(`buildtype=release`). To create a debug build, configure the build directory
with the `debug` build type:

```bash
meson setup builddir --buildtype=debug
```

Or, if the build directory has already been configured, you can reconfigure it:

```bash
meson configure builddir --buildtype=debug
```

Then compile the project:

```bash
meson compile -C builddir
```

When building with the `debug` build type, the `DEBUG` macro is automatically
defined in `meson.build`. This enables debug-level logging messages by default
in `httpdirfs`.

---

## Development Workflow

We use automated tools to maintain code quality and prevent common errors.

### Pre-commit Hooks

We use `pre-commit` to run a suite of checks before every commit. This includes
code formatting, static analysis, and basic sanity checks.

**Checks performed:**

- Code formatting (`clang-format`)
- Static analysis (`clang-tidy`)
- Spelling checks (`codespell`)
- Markdown formatting (`prettier`)
- Standard file checks (trailing whitespace, merge conflicts, large files)

#### Setup

1. Install `pre-commit`:

   ```bash
   pip install pre-commit
   ```

2. Install the git hooks:
   ```bash
   pre-commit install
   ```

#### Usage

Hooks run automatically on `git commit`. To run all checks manually on all
files:

```bash
pre-commit run --all-files
```

<!-- prettier-ignore -->
> [!IMPORTANT]
> The `clang-tidy` hook requires `compile_commands.json` to be present in the
> `builddir`. If you haven't configured the project yet, run:
>
> ```bash
> meson setup builddir
> ```

<!-- prettier-ignore -->
> [!IMPORTANT]
> **Manual Hooks Requirement Before Pushing:**
> While basic unit tests and short integration tests run automatically on every
> commit, the slow/intensive integration tests (`integration_long`) are placed
> in the `manual` stage to keep the default local commit feedback loop fast.
>
> **You are required to manually run the full test suite hooks before pushing
> your changes:**
>
> ```bash
> # Run all tests using the default compiler (GCC)
> pre-commit run all-tests --hook-stage manual --all-files
>
> # Run all tests (GCC Debug)
> pre-commit run all-tests-debug --hook-stage manual --all-files
>
> # Run all tests using Clang
> pre-commit run all-tests-clang --hook-stage manual --all-files
>
> # Run all tests (Clang Debug)
> pre-commit run all-tests-clang-debug --hook-stage manual --all-files
> ```
> Note that the Clang-based hooks require `clang` to be installed (e.g.,
> `sudo apt install clang` on Debian/Ubuntu systems).

### Running Tests

We support three test suites configured and managed via Meson:

1. **`unit_test`**: Runs all C-level unit tests (`test_util`, `test_cache`,
   `test_config`, `test_link`).
2. **`integration_short`**: Runs fast integration tests (excludes the large 1 GB
   file) verifying core mounting, directory traversal, read-only structures, and
   integrity. Completes in **~3 seconds**.
3. **`integration_long`**: Runs resource-intensive cache integration tests
   involving concurrent multithreaded reads of a 1 GB file. Completes in **~25
   seconds**.

#### Test Execution Environments

##### 1. Default/Local Pre-Commit Behavior

To speed up local development feedback, only the fast tests are executed by
default.

- **Command**: Running a plain `meson test -C builddir` or triggering the
  default local `git commit` hook.
- **Behavior**: Runs the `unit_test` and `integration_short` suites. The
  `integration_long` suite is explicitly excluded by default via the default
  test setup in `tests/meson.build`.

##### 2. CI Pipeline Behavior

The GitHub Actions workflow (`build.yml`) validates the entire test suite on
every pull request and push to the master branch using GCC and Clang
configurations:

- **Unit tests step**: `meson test -C builddir --no-suite integration` (runs
  only the `unit_test` suite)
- **Integration tests step**:
  `meson test -C builddir --suite integration --verbose` (runs both
  `integration_short` and `integration_long` suites)

##### 3. Required Pre-Push Validation

Before pushing changes, developers are **required** to manually run the full
suite using the manual pre-commit hooks to catch potential cache regressions and
memory leaks locally:

```bash
# Run GCC-based compilation and all tests (unit_test, integration_short, integration_long)
pre-commit run all-tests --hook-stage manual --all-files

# Run GCC-based compilation in Debug mode (with leak detection enabled) and all tests
pre-commit run all-tests-debug --hook-stage manual --all-files

# Run Clang-based compilation and all tests
pre-commit run all-tests-clang --hook-stage manual --all-files

# Run Clang-based compilation in Debug mode (with leak detection enabled) and all tests
pre-commit run all-tests-clang-debug --hook-stage manual --all-files
```

---

#### Running Specific Suites Manually via Meson

##### Setup

Before running tests, ensure that the build directory is configured:

```bash
meson setup builddir
```

##### Run Unit Tests Individually

If you want to run only the unit tests:

```bash
meson test -C builddir --suite unit_test
```

You can also execute specific unit tests by target name:

```bash
meson test -C builddir test_util
meson test -C builddir test_cache
meson test -C builddir test_config
meson test -C builddir test_link
```

##### Run Short Integration Tests Individually

```bash
meson test -C builddir --suite integration_short
```

##### Run Long Integration Tests Individually

```bash
meson test -C builddir --suite integration_long
```

##### Verbose Output and Debugging

By default, Meson hides test output unless a test fails. To see detailed test
results, use the following options:

- **Print logs on failure only:**
  ```bash
  meson test -C builddir --print-errorlogs
  ```
- **Verbose output (all stdout/stderr):**
  ```bash
  meson test -C builddir -v
  ```

#### Integration Tests Runner Prerequisites

The integration tests require the following system dependencies:

- **FUSE** (with `/dev/fuse` access)
- `fusermount3` (or `fusermount`)
- **Python 3**

##### Run Integration Tests Manually

Alternatively, you can run the test runner script directly by providing the path
to the compiled `httpdirfs` binary and the desired mode flag:

```bash
# Run short integration tests
./tests/integration/run_integration_test.sh --short builddir/httpdirfs

# Run long integration tests
./tests/integration/run_integration_test.sh --long builddir/httpdirfs
```

---

## Logging and Error Handling

`httpdirfs` uses a centralized logging system defined in `log.h`.

- Use the `lprintf(type, ...)` macro for logging. It automatically includes the
  file name, function name, and line number.
- Fatal errors should use the `fatal` log level.
- `lprintf(fatal, ...)` will automatically call `exit_failure()`, which prints a
  backtrace and terminates the program.

---

## Memory Management

The project uses wrappers for memory allocation to ensure consistent error
handling.

- Use `CALLOC(nmemb, size)` instead of `calloc()`. It automatically handles
  allocation failures by logging a fatal error.
- Use `FREE(ptr)` instead of `free()`.

---

## Documentation

### Doxygen

We use Doxygen to generate comprehensive API documentation.

To generate the documentation:

1. Enter your build directory.
2. Run the Doxygen target:

```bash
meson compile doxygen
```

The documentation will be generated in the `doxygen` directory at the root of
the repository.
