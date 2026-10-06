[![CodeQL](https://github.com/fangfufu/httpdirfs/actions/workflows/codeql.yml/badge.svg)](https://github.com/fangfufu/httpdirfs/actions/workflows/codeql.yml)
[![CodeFactor](https://www.codefactor.io/repository/github/fangfufu/httpdirfs/badge)](https://www.codefactor.io/repository/github/fangfufu/httpdirfs)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/30af0a5b4d6f4a4d83ddb68f5193ad23)](https://app.codacy.com/gh/fangfufu/httpdirfs/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=fangfufu_httpdirfs&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=fangfufu_httpdirfs)
[![pre-commit.ci status](https://results.pre-commit.ci/badge/github/fangfufu/httpdirfs/master.svg)](https://results.pre-commit.ci/latest/github/fangfufu/httpdirfs/master)

# HTTPDirFS - HTTP Directory Filesystem

HTTPDirFS is a filesystem that allows you to mount arbitrary websites using the
FUSE framework. It comes with a cache system, and Airsonic / Subsonic server
support

HTTPDirFS parses HTML directory listings using the Gumbo HTML5 parser. It
extracts descriptive filenames from anchor text (`<a>`), resolves name
collisions with progressive URL path escalation.

If you only want to access a single file, there is also a simplified Single File
Mode. This can be especially useful if the web server does not present a HTTP
directory listing.

There is also support for Airsonic / Subsonic server. This allows you to mount a
remote music collection locally.

The cache system caches the file segments you have accessed, so you don't need
to download those segments again if you access them later. This feature is
triggered by the `--cache` flag. This is similar to the `--vfs-cache-mode full`
feature of
[rclone mount](https://rclone.org/commands/rclone_mount/#vfs-cache-mode-full)

## Usage

Basic usage:

```
./httpdirfs -f --cache $URL $MOUNT_POINT
```

An example URL would be
[Debian CD Image Server](https://cdimage.debian.org/debian-cd/). The `-f` flag
keeps the program in the foreground, which is useful for monitoring which URL
the filesystem is visiting.

For more usage related help, run

```
./httpdirfs --help
```

or

```
man httpdirfs
```

Please note that the man page only works if you have installed HTTPDirFS
properly.

The full usage flags is also documented in the [usage](docs/usage.md) page.

### Single file mode

If you just want to access a single file, you can specify `--single-file-mode`.
This effectively creates a virtual directory that contains one single file. This
operating mode is similar to the unmaintained
[httpfs](http://httpfs.sourceforge.net/).

e.g.

```
./httpdirfs -f --cache --single-file-mode https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-11.0.0-amd64-netinst.iso mnt
```

This can be useful if the web server does not present a HTTP directory listing.

### Airsonic / Subsonic server support

The Airsonic / Subsonic server support is dedicated the my Debian package
maintainer Jerome Charaoui.You can mount the music collection on your Airsonic /
Subsonic server (\*sonic), and browse them using your favourite file browser.
For more information on how to use it, please refer to the
[usage](USAGE.md#airsonic--subsonic-mounting-options) page.

### The cache system

> [!WARNING]
> HTTPDirFS 1.4.x contains a breaking change to the format of the cache system.
> Please delete your existing cache with `--cache-clear` or remove
> `~/.cache/httpdirfs` (this is the default location) before using HTTPDirFS
> 1.4.x.

You can cache the files you have accessed on your storage device by using the
`--cache` flag. The files it caches persist across sessions. You can clear the
entire cache using `--cache-clear`, or clear only a specific server using
`--cache-clear-host <URL_OR_HOST>`.

Once a segment of the file has been downloaded once, it won't be downloaded
again as long as the server still reports the same `Last-Modified` timestamp and
content length. Subsequent reads are served offline at local storage speed.
Directory listings (and files whose remote metadata cannot be verified) are
considered stale, and refetched from the server, once they are older than
`--refresh-timeout` seconds (default: 3600).

By default, the cache files are stored under `${XDG_CACHE_HOME}/httpdirfs`,
`${HOME}/.cache/httpdirfs`, or `./.cache/httpdirfs` in the current working
directory, whichever is found first. By default, `${XDG_CACHE_HOME}/httpdirfs`
is normally `${HOME}/.cache/httpdirfs`. A custom cache location root can be
supplied with `--cache-location`.

The cache system relies on sparse allocation. Please make sure your filesystem
supports it. Otherwise your local storage device will get heavy I/O from cache
file creation. For a list of filesystem that supports sparse allocation, please
refer to
[Wikipedia](https://en.wikipedia.org/wiki/Comparison_of_file_systems#Allocation_and_layout_policies).

### Configuration file support

This program has basic support for using a configuration file. By default, the
configuration file which the program reads is
`${XDG_CONFIG_HOME}/httpdirfs/config`, which by default is at
`${HOME}/.config/httpdirfs/config`. You will have to create the sub-directory
and the configuration file yourself. In the configuration file, please supply
one option per line. For example:

```
--username test
--password test
-f
```

Alternatively, you can specify your own configuration file by using the
`--config` option.

### Log levels

You can control how much log HTTPDirFS outputs by setting the
`HTTPDIRFS_LOG_LEVEL` environmental variable. For details of the different types
of log that are supported, please refer to
[log.h](https://github.com/fangfufu/httpdirfs/blob/master/src/log.h) and
[log.c](https://github.com/fangfufu/httpdirfs/blob/master/src/log.c).

### Diagnostics directory (`.httpdirfs` directory)

Every directory listing in the mounted filesystem exposes a hidden virtual
`.httpdirfs` directory. It contains:

- **`CONTENT`**: The raw HTML payload of the directory listing page as served by
  the web server.
- **`HEADER`**: The raw HTTP response headers of the directory listing request.

These virtual files are useful for debugging how a web server presents a
directory, for example when HTTPDirFS appears to misparse a listing. The
`.httpdirfs` directory is virtual only: it does not exist on the remote server
and is never stored in the cache.

## Compilation

For important development related documentation, please refer to
[the Development Guideline](docs/development.md).

### Debian 13 "Trixie"

Under Debian 13 "Trixie" and newer versions, you need the following
dependencies:

```
libgumbo-dev libfuse3-dev libssl-dev libcurl4-openssl-dev uuid-dev help2man
libexpat1-dev pkg-config meson clang-format
```

You can then compile the program similar to how you compile a typical program
that uses the Meson build system:

```
meson setup builddir
cd builddir
meson compile
```

To install the program, do the following:

```
sudo meson install
```

To uninstall the program, do the following:

```
sudo ninja uninstall
```

To clean the build directory, run:

```
ninja clean
```

For more information, please refer to this
[tutorial](https://mesonbuild.com/Tutorial.html).

### macOS

Under macOS, you can use [Homebrew](https://brew.sh/) to install the
dependencies:

```
brew install gumbo-parser openssl curl expat meson pkg-config ossp-uuid
brew install --cask macfuse
```

To compile the program, you might need to set the `PKG_CONFIG_PATH` so that
`meson` can find `openssl`:

```
export PKG_CONFIG_PATH="$(brew --prefix openssl@3)/lib/pkgconfig:$(brew --prefix)/lib/pkgconfig:$PKG_CONFIG_PATH"
meson setup builddir
cd builddir
meson compile
```

Please note that while macOS build instructions are provided, macOS build
testing is primarily done via GitHub Actions CI, as I do not have regular access
to a physical Mac.

### Other operating systems

I don't have the resources to test out compilation for Linux distributions other
than Debian. I also do not have the resources to test out compilation for
FreeBSD. Therefore, I have removed the instruction on how to compile for FreeBSD
in the README for now. Please feel free to send me a pull request to add them
back in. It is known that HTTPDirFS
[does compile](https://github.com/fangfufu/httpdirfs/issues/165) on FreeBSD.

## Installation

### Debian 13 "Trixie"

HTTPDirFS is developed on Debian, and it is available as a package in Debian 13
"Trixie". If you are on Debian Trixie, you can simply run the following command
as `root`:

```
apt install httpdirfs
```

For more information on the status of HTTDirFS in Debian, please refer to
[Debian package tracker](https://tracker.debian.org/pkg/httpdirfs-fuse)

### Other distributions

Please note if you install HTTDirFS from a repository, it may be outdated.

[![Packaging status](https://repology.org/badge/vertical-allrepos/fusefs%3Ahttpdirfs.svg)](https://repology.org/project/fusefs%3Ahttpdirfs/versions)

## The technical details

The technical details are documented in the [technical.md](docs/technical.md)
page.

## Press Coverage

- Linux Format - Issue [264](https://www.linuxformat.com/archives?issue=264),
  July 2020

## Contributors

Thanks for your contribution to the project!

[![Contributors Avatars](https://contributors-img.web.app/image?repo=fangfufu/httpdirfs)](https://github.com/fangfufu/httpdirfs/graphs/contributors)
[![Contributors Count](https://img.shields.io/github/contributors-anon/fangfufu/httpdirfs?style=for-the-badge&logo=httpdirfs)](https://github.com/fangfufu/httpdirfs/graphs/contributors)

## Special Acknowledgement

- First of all, I would like to thank
  [Jerome Charaoui](https://github.com/jcharaoui) for being the Debian
  Maintainer for this piece of software. Thank you so much for packaging it!
- I would like to thank
  [Cosmin Gorgovan](https://scholar.google.co.uk/citations?user=S7UZ6MAAAAAJ&hl=en)
  for the technical and moral support. Your wisdom is much appreciated!
- I would like to thank [Edenist](https://github.com/edenist) for providing
  FreeBSD compatibility patches.
- I would like to thank [hiliev](https://github.com/hiliev) for providing macOS
  compatibility patches.
- I would like to thank [Jonathan Kamens](https://github.com/jikamens) for
  providing a whole bunch of code improvements and the improved build system.
- I would like to thank [-Archivist](https://www.reddit.com/user/-Archivist/)
  for not providing FTP or WebDAV access to his server. This piece of software
  was written in direct response to his appalling behaviour.

## License

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but **WITHOUT
ANY WARRANTY**; without even the implied warranty of **MERCHANTABILITY** or
**FITNESS FOR A PARTICULAR PURPOSE**. See the GNU General Public License for
more details.
