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

______________________________________________________________________

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

______________________________________________________________________

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

1. Install the git hooks:

   ```bash
   pre-commit install
   ```

#### Usage

Hooks run automatically on `git commit`. To run all checks manually on all
files:

```bash
pre-commit run --all-files
```

> [!IMPORTANT]
> The `clang-tidy` hook requires `compile_commands.json` to be present in the
> `builddir`. If you haven't configured the project yet, run:
>
> ```bash
> meson setup builddir
> ```

> [!IMPORTANT]
> **Manual Hooks Requirement Before Pushing:** While basic unit tests and short
> integration tests run automatically on every commit, the slow/intensive
> integration tests (`integration_long`) are placed in the `manual` stage to
> keep the default local commit feedback loop fast.
>
> **You are required to manually run the full test suite hooks before pushing
> your changes:**
>
> ```bash
> # Run all tests using GCC
> pre-commit run all-tests-gcc --hook-stage manual --all-files
>
> # Run all tests (GCC Debug)
> pre-commit run all-tests-gcc-debug --hook-stage manual --all-files
>
> # Run all tests using Clang
> pre-commit run all-tests-clang --hook-stage manual --all-files
>
> # Run all tests (Clang Debug)
> pre-commit run all-tests-clang-debug --hook-stage manual --all-files
> ```
>
> Note that the Clang-based hooks require `clang` to be installed (e.g.,
> `sudo apt install clang` on Debian/Ubuntu systems).

### Running Tests

We support three test suites configured and managed via Meson:

1. **`unit_test`**: Runs all C-level unit tests (`test_util`, `test_cache`,
   `test_config`, `test_link`).
1. **`integration_short`**: Runs fast integration tests (excludes the large 1 GB
   file) verifying core mounting, directory traversal, read-only structures, and
   integrity. Completes in **~3 seconds**.
1. **`integration_long`**: Runs resource-intensive cache integration tests
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

- **Unit tests step**: `meson test -C builddir --no-suite integration --verbose`
  (runs only the `unit_test` suite)
- **Integration tests (short) step**:
  `meson test -C builddir --suite integration_short --verbose` (runs fast
  integration tests)
- **Integration tests (long) step**:
  `meson test -C builddir --suite integration_long --verbose` (runs
  resource-intensive integration tests)

##### 3. Required Pre-Push Validation

Before pushing changes, developers are **required** to manually run the full
suite using the manual pre-commit hooks to catch potential cache regressions and
memory leaks locally:

```bash
# Run GCC-based compilation and all tests (unit_test, integration_short, integration_long)
pre-commit run all-tests-gcc --hook-stage manual --all-files

# Run GCC-based compilation in Debug mode (with leak detection enabled) and all tests
pre-commit run all-tests-gcc-debug --hook-stage manual --all-files

# Run Clang-based compilation and all tests
pre-commit run all-tests-clang --hook-stage manual --all-files

# Run Clang-based compilation in Debug mode (with leak detection enabled) and all tests
pre-commit run all-tests-clang-debug --hook-stage manual --all-files
```

______________________________________________________________________

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

______________________________________________________________________

## Logging and Error Handling

`httpdirfs` uses a centralized logging system defined in `log.h`.

- Use the `lprintf(type, ...)` macro for logging. It automatically includes the
  file name, function name, and line number.
- Fatal errors should use the `fatal` log level.
- `lprintf(fatal, ...)` will automatically call `exit_failure()`, which prints a
  backtrace and terminates the program.

______________________________________________________________________

## Wrapper Functions

To ensure consistent error handling, memory tracking, and safe concurrency, the
project uses custom macro and function wrappers around standard library calls.
**Developers should always use these wrappers instead of their standard C
counterparts.**

### 1. Memory Management Wrappers

Memory allocation wrappers automatically handle allocation failures by printing
a fatal backtrace and exiting, eliminating the need for manual null-checks after
every call. They also track allocations to prevent memory leaks in debug
configurations.

| Standard Function | Wrapper Macro         | Notes & Safety Rules                                                                     |
| :---------------- | :-------------------- | :--------------------------------------------------------------------------------------- |
| `calloc`          | `CALLOC(nmemb, size)` | Automatically terminates with a `fatal` error on allocation failure.                     |
| `realloc`         | `REALLOC(ptr, size)`  | Safely handles resizing and tracks allocations.                                          |
| `strdup`          | `STRDUP(s)`           | Duplicates string with error checking and tracking.                                      |
| `strndup`         | `STRNDUP(s, n)`       | Duplicates at most `n` characters with error checking and tracking.                      |
| `realpath`        | `REALPATH(p, r)`      | Resolving path names with wrapper checks.                                                |
| `free`            | `FREE(ptr)`           | Automatically sets the pointer variable to `NULL` after freeing to prevent double-frees. |

> [!WARNING]
> **Important rules for `FREE(ptr)`:**
>
> - `FREE` modifies the pointer itself by setting it to `NULL`. This will fail
>   to compile if used on const-qualified pointer variables (e.g.,
>   `char * const p`).
> - When used on function parameters (e.g.,
>   `void func(void *ptr) { FREE(ptr); }`), it only nullifies the local
>   parameter copy. The caller's original pointer remains unchanged (and
>   dangling), so be sure to nullify the caller's pointer manually.

### 2. Concurrency and Synchronization Wrappers

All pthread mutexes, condition variables, and semaphores must be manipulated via
wrappers. These wrappers perform automatic validation of return codes and assert
that no errors occur during locking, unlocking, waiting, or signaling.

#### Pthread Mutexes

- **Initialize**: `PTHREAD_MUTEX_INIT(mutex, attr)` (wraps `pthread_mutex_init`)
- **Lock**: `PTHREAD_MUTEX_LOCK(mutex)` (wraps `pthread_mutex_lock`)
- **Unlock**: `PTHREAD_MUTEX_UNLOCK(mutex)` (wraps `pthread_mutex_unlock`)
- **Destroy**: `PTHREAD_MUTEX_DESTROY(mutex)` (wraps `pthread_mutex_destroy`)

#### Pthread Condition Variables

- **Initialize**: `PTHREAD_COND_INIT(cond, attr)` (wraps `pthread_cond_init`)
- **Wait**: `PTHREAD_COND_WAIT(cond, mutex)` (wraps `pthread_cond_wait`)
- **Broadcast**: `PTHREAD_COND_BROADCAST(cond)` (wraps `pthread_cond_broadcast`)
- **Destroy**: `PTHREAD_COND_DESTROY(cond)` (wraps `pthread_cond_destroy`)

#### POSIX Semaphores

- **Initialize**: `SEM_INIT(sem, pshared, value)` (wraps `sem_init`)
- **Wait**: `SEM_WAIT(sem)` (wraps `sem_wait`)
- **Post**: `SEM_POST(sem)` (wraps `sem_post`)
- **Destroy**: `SEM_DESTROY(sem)` (wraps `sem_destroy`)

______________________________________________________________________

## Documentation

### Doxygen

We use Doxygen to generate comprehensive API documentation.

To generate the documentation:

1. Enter your build directory.
1. Run the Doxygen target:

```bash
meson compile doxygen
```

The documentation will be generated in the `doxygen` directory at the root of
the repository.
