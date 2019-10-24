# HTTPDirFS - HTTP Directory Filesystem with a permanent cache 

Have you ever wanted to mount those HTTP directory listings as if it was a
partition? Look no further, this is your solution.  HTTPDirFS stands for Hyper
Text Transfer Protocol Directory Filesystem.

The performance of the program is excellent. HTTP connections are reused due to
the use of curl-multi interface. The FUSE component runs in multithreaded mode.

There is a permanent cache system which can cache all the file segments you have
downloaded, so you don't need to these segments again if you access them later.
This feature is triggered by the ``--cache`` flag. This makes this filesystem
much faster than ``rclone mount``.

## News
HTTPDirFS now supports mounting Airsonic / Subsonic servers!!!

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

Subsonic options:
        --sonic-username    The username for your Airsonic / Subsonic server
        --sonic-password    The username for your Airsonic / Subsonic server

FUSE options:

    -d   -o debug          enable debug output (implies -f)
    -f                     foreground operation
    -s                     disable multi-threaded operation

## Permanent cache system
You can cache all the files you have looked at permanently on your hard drive by
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
please refer to [Wikipedia](https://en.wikipedia.org/wiki/Comparison_of_file_systems#Allocation_and_layout_policies).

## Airsonic / Subsonic server support
This is a new feature to 1.2.0. Now you can mount the music collection on your
Airsonic / Subsonic server, and browse them using your favourite file browser.
You simply have to supply both ``--sonic-username`` and ``--sonic-password`` to
trigger the Airsonic / Subsonic server mode. For example:

    ./httpdirfs -f --cache --sonic-username $USERNAME --sonic-password $PASSWORD $URL $MOUNT_POINT

You definitely want to enable the cache for this one, otherwise it is painfully
slow.

## Configuration file support
This program has basic support for using a configuration file. The configuration
file that the program reads is ``${XDG_CONFIG_HOME}/httpdirfs/config``, which by
default is at ``${HOME}/.config/httpdirfs/config``. You will have to create the
sub-directory and the configuration file yourself. In the configuration file,
please supply one option per line. For example:

	--username test
	--password test
	-f

## Compilation
### Debian 10 "Buster" and newer versions
Under Debian 10 "Buster" and newer versions, you need the following packages:

    libgumbo-dev libfuse-dev libssl-dev libcurl4-openssl-dev

### Debian 9 "Stretch"
Under Debian 9 "Stretch", you need the following packages:

    libgumbo-dev libfuse-dev libssl1.0-dev libcurl4-openssl-dev

If you get the following warnings during compilation,

    /usr/bin/ld: warning: libcrypto.so.1.0.2, needed by /usr/lib/gcc/x86_64-linux-gnu/6/../../../x86_64-linux-gnu/libcurl.so, may conflict with libcrypto.so.1.1

then this program will crash if you connect to HTTPS website. You need to check
if you have ``libssl1.0-dev`` installed rather than ``libssl-dev``.
This is you likely have the binaries of OpenSSL 1.0.2 installed alongside with
the header files for OpenSSL 1.1. The header files for OpenSSL 1.0.2 link in
additional mutex related callback functions, whereas the header files for
OpenSSL 1.1 do not.


### Debugging Mutexes
By default the debugging output associated with mutexes are not compiled. To
enable them, compile the program with the ``-DCACHE_LOCK_DEBUG``, the
``-DNETWORK_LOCK_DEBUG`` and/or the ``-DLINK_LOCK_DEBUG`` CPPFLAGS, e.g.

    make CPPFLAGS=-DCACHE_LOCK_DEBUG

## The Technical Details
This program downloads the HTML web pages/files using
[libcurl](https://curl.haxx.se/libcurl/), then parses the listing pages using
[Gumbo](https://github.com/google/gumbo-parser), and presents them using
[libfuse](https://github.com/libfuse/libfuse).

The cache system stores the metadata and the downloaded file into two
separate directories. It uses ``uint8_t`` arrays to record which segments of the
file had been downloaded.

## Other projects which incorporate HTTPDirFS
- [Curious Container](https://www.curious-containers.cc/docs/red-connector-http#mount-dir)
has a Python wrapper for mounting HTTPDirFS.

## Acknowledgement
- First of all, I would like to thank
[Jerome Charaoui](https://github.com/jcharaoui) for being the Debian Maintainer
for this piece of software. Thank you so much for packaging it!
- I would like to thank
[Cosmin Gorgovan](https://scholar.google.co.uk/citations?user=S7UZ6MAAAAAJ&hl=en)
for the technical and moral support. Your wisdom is much appreciated!
- I would like to thank [-Archivist](https://www.reddit.com/user/-Archivist/)
for not providing FTP or WebDAV access to his server. This piece of software was
written in direct response to his appalling behaviour.
