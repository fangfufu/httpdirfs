# HTTPDirFS - HTTP Directory Filesystem with a permanent cache, and Airsonic / Subsonic server support!

Have you ever wanted to mount those HTTP directory listings as if it was a
partition? Look no further, this is your solution.  HTTPDirFS stands for Hyper
Text Transfer Protocol Directory Filesystem.

The performance of the program is excellent. HTTP connections are reused through
curl-multi interface. The FUSE component runs in the multithreaded mode.

There is a permanent cache system which can cache all the file segments you have
downloaded, so you don't need to these segments again if you access them later.
This feature is triggered by the ``--cache`` flag. This is similar to the
``--vfs-cache-mode full`` feature of
[rclone mount](https://rclone.org/commands/rclone_mount/#vfs-cache-mode-full)

There is support for Airsonic / Subsonic server. This allows you to mount a
remote music collection locally.

If you only want to access a single file, there is also a simplified
Single File Mode. This can be especially useful if the web server does not
present a HTTP directory listing.

## Installation
Please note if you install HTTDirFS from a repository, it can be outdated.
### Debian 11 "Bullseye"
HTTPDirFS is available as a package in Debian 11 "Bullseye", If you are on
Debian Bullseye, you can simply run the following
command as ``root``:

	apt install httpdirfs

For more information on the status of HTTDirFS in Debian, please refer to
[Debian package tracker](https://tracker.debian.org/pkg/httpdirfs-fuse)

### Arch Linux
HTTPDirFS is available in the
[Arch User Repository](https://aur.archlinux.org/packages/httpdirfs).

### FreeBSD
HTTPDirFS is available in the
[FreeBSD Ports Collection](https://www.freshports.org/sysutils/fusefs-httpdirfs/).

## Compilation
### Ubuntu
Under Ubuntu 18.04.4 LTS, you need the following packages:

    libgumbo-dev libfuse-dev libssl-dev libcurl4-openssl-dev uuid-dev

### Debian 11 "Bullseye" and Debian 10 "Buster"
Under Debian 10 "Buster" and newer versions, you need the following packages:

    libgumbo-dev libfuse-dev libssl-dev libcurl4-openssl-dev uuid-dev

### FreeBSD
The following dependencies are required from either pkg or ports:

Packages:

    gmake fusefs-libs gumbo e2fsprogs-libuuid curl expat pkgconf help2man

If you want to be ableto build the documentation ("gmake doc") you also need
doxygen (devel/doxygen).

Ports:

    devel/gmake sysutils/fusefs-libs devel/gumbo misc/e2fsprogs-libuuid ftp/curl textproc/expat2 devel/pkgconf devel/doxygen misc/help2man

**Note:** If you want brotli compression support, you will need to install curl
from ports and enable the option.

You can then build + install with:

    ./configure
    gmake
    sudo gmake install

Alternatively, you may use the FreeBSD [ports(7)](https://man.freebsd.org/ports/7)
infrastructure to build HTTPDirFS from source with the modifications you need.

### macOS
You need to install some packages from Homebrew:

    brew install macfuse curl gumbo-parser openssl pkg-config help2man

If you want to be able to build the documentation ("make doc") you also need
help2man, doxygen, and graphviz.

Build and install:

    ./configure
    make
    sudo make install

Apple's command-line build tools are usually installed as part of setting up 
Homebrew. HTTPDirFS will be installed in ``/usr/local``.

## Usage

	./httpdirfs -f --cache $URL $MOUNT_POINT

An example URL would be
[Debian CD Image Server](https://cdimage.debian.org/debian-cd/). The ``-f`` flag
keeps the program in the foreground, which is useful for monitoring which URL
the filesystem is visiting.

### Useful options

HTTPDirFS options:

    -u  --username          HTTP authentication username
    -p  --password          HTTP authentication password
    -P  --proxy             Proxy for libcurl, for more details refer to
                            https://curl.haxx.se/libcurl/c/CURLOPT_PROXY.html
        --proxy-username    Username for the proxy
        --proxy-password    Password for the proxy
        --cache             Enable cache (default: off)
        --cache-location    Set a custom cache location
                            (default: "${XDG_CACHE_HOME}/httpdirfs")
        --dl-seg-size       Set cache download segment size, in MB (default: 8)
                            Note: this setting is ignored if previously
                            cached data is found for the requested file.
        --max-seg-count     Set maximum number of download segments a file
                            can have. (default: 128*1024)
                            With the default setting, the maximum memory usage
                            per file is 128KB. This allows caching files up
                            to 1TB in size using the default segment size.
        --max-conns         Set maximum number of network connections that
                            libcurl is allowed to make. (default: 10)
        --retry-wait        Set delay in seconds before retrying an HTTP request
                            after encountering an error. (default: 5)
        --user-agent        Set user agent string (default: "HTTPDirFS")
        --no-range-check    Disable the built-in check for the server's support
                            for HTTP range requests
        --insecure-tls      Disable licurl TLS certificate verification by
                            setting CURLOPT_SSL_VERIFYHOST to 0
        --single-file-mode  Single file mode - rather than mounting a whole
                            directory, present a single file inside a virtual
                            directory.

For mounting a Airsonic / Subsonic server:

        --sonic-username    The username for your Airsonic / Subsonic server
        --sonic-password    The password for your Airsonic / Subsonic server
        --sonic-id3         Enable ID3 mode - this present the server content in
                            Artist/Album/Song layout
        --sonic-insecure    Authenticate against your Airsonic / Subsonic server
                            using the insecure username / hex encoded password
                            scheme

Useful FUSE options:

    -d   -o debug          enable debug output (implies -f)
    -f                     foreground operation
    -s                     disable multi-threaded operation

## Airsonic / Subsonic server support
The Airsonic / Subsonic server support is dedicated the my Debian package
maintainer Jerome Charaoui.You can mount the music collection on your
Airsonic / Subsonic server (*sonic), and browse them using your favourite file
browser.

You simply have to supply both ``--sonic-username`` and ``--sonic-password`` to
trigger the *sonic server mode. For example:

    ./httpdirfs -f --cache --sonic-username $USERNAME --sonic-password $PASSWORD $URL $MOUNT_POINT

You definitely want to enable the cache for this one, otherwise it is painfully
slow.

There are two ways of mounting your *sonic server
- the index mode
- and the ID3 mode.

In the index mode, the filesystem is presented based on the listing on the
``Index`` link in your *sonic's home page.

In ID3 mode, the filesystem is presented using the following hierarchy:
 0. Root
 1. Alphabetical indices of the artists' names
 2. The arists' names
 3. All of the albums by a single artist
 4. All the songs in an album.

By default, *sonic server is mounted in the index mode. If you want to mount in
ID3 mode, please use the ``--sonic-id3`` flag.

Please note that the cache feature is unaffected by how you mount your *sonic
server. If you mounted your server in index mode, the cache is still valid in
ID3 mode, and vice versa.

HTTPDirFS is also known to work with the following applications, which implement
some or all of Subsonic API:

- [Funkwhale](https://funkwhale.audio/) (requires ``--sonic-id3`` and
``--no-range-check``, more information in
[issue #45](https://github.com/fangfufu/httpdirfs/issues/45))
- [LMS](https://github.com/epoupon/lms) (requires ``--sonic-insecure`` and
``--no-range-check``, more information in
[issue #46](https://github.com/fangfufu/httpdirfs/issues/46). To mount the
[demo instance](https://lms.demo.poupon.io/), you might also need
``--insecure-tls``)
- [Navidrome](https://github.com/navidrome/navidrome), more information in
[issue #51](https://github.com/fangfufu/httpdirfs/issues/51).

## Single file mode
If you just want to access a single file, you can specify
``--single-file-mode``. This effectively creates a virtual directory that
contains one single file. This operating mode is similar to the unmaintained
[httpfs](http://httpfs.sourceforge.net/).

e.g.

    ./httpdirfs -f --cache --single-file-mode https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-11.0.0-amd64-netinst.iso mnt

This can be useful if the web server does not present a HTTP directory listing.
This feature was implemented due to Github
[issue #86](https://github.com/fangfufu/httpdirfs/issues/86)

## Permanent cache system
You can cache the files you have accessed permanently on your hard drive by
using the ``--cache`` flag. The file it caches persist across sessions.

By default, the cache files are stored under ``${XDG_CACHE_HOME}/httpdirfs``,
which by default is ``${HOME}/.cache/httpdirfs``. Each HTTP directory gets its
own cache folder, they are named using the escaped URL of the HTTP directory.

Once a segment of the file has been downloaded once, it won't be downloaded
again.

Please note that due to the way the permanent cache system is implemented. The
maximum download speed is around 15MiB/s, as measured using my localhost as the
web server. However after you have accessed a file once, accessing it again will
be the same speed as accessing your hard drive.

If you have any patches to make the initial download go faster, please submit a
pull request.

The permanent cache system relies on sparse allocation. Please make sure your
filesystem supports it. Otherwise your hard drive / SSD will get heavy I/O from
cache file creation. For a list of filesystem that supports sparse allocation,
please refer to
[Wikipedia](https://en.wikipedia.org/wiki/Comparison_of_file_systems#Allocation_and_layout_policies).

## Configuration file support
This program has basic support for using a configuration file. By default, the
configuration file which the program reads is
``${XDG_CONFIG_HOME}/httpdirfs/config``, which by
default is at ``${HOME}/.config/httpdirfs/config``. You will have to create the
sub-directory and the configuration file yourself. In the configuration file,
please supply one option per line. For example:

	--username test
	--password test
	-f

Alternatively, you can specify your own configuration file by using the
``--config`` option.

### Log levels
You can control how much log HTTPDirFS outputs by setting the
``HTTPDIRFS_LOG_LEVEL`` environmental variable. For details of the different
types of log that are supported, please refer to
[log.h](https://github.com/fangfufu/httpdirfs/blob/master/src/log.h) and
[log.c](https://github.com/fangfufu/httpdirfs/blob/master/src/log.c).

## The Technical Details
For the normal HTTP directories, this program downloads the HTML web pages/files
using [libcurl](https://curl.haxx.se/libcurl/), then parses the listing pages
using [Gumbo](https://github.com/google/gumbo-parser), and presents them using
[libfuse](https://github.com/libfuse/libfuse).

For *sonic servers, rather than using the Gumbo parser, this program parse
*sonic servers' XML responses using
[expat](https://github.com/libexpat/libexpat).

The cache system stores the metadata and the downloaded file into two
separate directories. It uses ``uint8_t`` arrays to record which segments of the
file had been downloaded.

Note that HTTPDirFS requires the server to support HTTP Range Request, some
servers support this features, but does not present ``"Accept-Ranges: bytes`` in
the header responses. HTTPDirFS by default checks for this header field. You can
disable this check by using the ``--no-range-check`` flag.

## Other projects which incorporate HTTPDirFS
- [Curious Container](https://www.curious-containers.cc/docs/red-connector-http#mount-dir)
has a Python wrapper for mounting HTTPDirFS.

## Press Coverage
- Linux Format - Issue [264](https://www.linuxformat.com/archives?issue=264), July 2020

## Acknowledgement
- First of all, I would like to thank
[Jerome Charaoui](https://github.com/jcharaoui) for being the Debian Maintainer
for this piece of software. Thank you so much for packaging it!
- I would like to thank
[Cosmin Gorgovan](https://scholar.google.co.uk/citations?user=S7UZ6MAAAAAJ&hl=en)
for the technical and moral support. Your wisdom is much appreciated!
- I would like to thank [Edenist](https://github.com/edenist) for providing FreeBSD
compatibility patches.
- I would like to thank [hiliev](https://github.com/hiliev) for providing macOS
compatibility patches.
- I would like to thank [-Archivist](https://www.reddit.com/user/-Archivist/)
for not providing FTP or WebDAV access to his server. This piece of software was
written in direct response to his appalling behaviour.
