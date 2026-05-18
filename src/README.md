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

---

## Logging and Error Handling

`httpdirfs` uses a centralized logging system defined in `log.h`.

- Use the `lprintf(type, ...)` macro for logging. It automatically includes the
  file name, function name, and line number.
- Fatal errors should use the `fatal` log level.
- `lprintf(fatal, ...)` will automatically call `exit_failure()`, which prints a
  backtrace and terminates the program.

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
