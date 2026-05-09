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

The project follows the **Kernighan & Ritchie (K&R)** style. We use `astyle` for
automatic formatting.

Manual formatting can be triggered via Meson:

```bash
meson compile format
```

---

## Development Workflow

We use automated tools to maintain code quality and prevent common errors.

### Pre-commit Hooks

We use `pre-commit` to run a suite of checks before every commit. This includes
code formatting, static analysis, and basic sanity checks.

**Checks performed:**

- Code formatting (`astyle`)
- Static analysis (`clang-tidy`)
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

> [!IMPORTANT] The `clang-tidy` hook requires `compile_commands.json` to be
> present in the `builddir`. If you haven't configured the project yet, run:
>
> ```bash
> meson setup builddir
> ```

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
