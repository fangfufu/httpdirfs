# HTTPDirFS

Have you ever wanted to mount those HTTP directory listings as if it was a partition? Look no further, this is your solution.  HTTPDirFS stands for Hyper Text Transfer Protocol Directory Filesystem

The performance of the program is excellent, due to the use of curl-multi interface. HTTP connections are reused, and HTTP pipelining is used when available. I haven't benchmarked it, but I feel this is faster than ``rclone mount``. The FUSE component itself also runs in multithreaded mode.

Furthermore, a permanent cache system has been implemented to cache all the files you have downloaded. This is triggered by the ``--cache`` flag

## BUG
The permanent cache system seems to have problem when you have randomly seek across the file during the initial download process. I am not sure what causes the problem. It is probably some sort of concurrency issue. The mutexes I set up doesn't seem to help with the problem.

This feature is also very slow. When downloading from localhost, it peaks at about 1.5 MiB/s. I am not entirely sure why. 

## Compilation
This program was developed under Debian Stretch. If you are using the same operating system as me, you need ``libgumbo-dev``, ``libfuse-dev``, ``libssl1.0-dev`` and ``libcurl4-openssl-dev``.

If you run Debian Stretch, and you have OpenSSL 1.0.2 installed, and you get warnings that look like below during compilation,

    network.c:70:22: warning: ‘thread_id’ defined but not used [-Wunused-function]
    static unsigned long thread_id(void)
                         ^~~~~~~~~
    network.c:57:13: warning: ‘lock_callback’ defined but not used [-Wunused-function]
    static void lock_callback(int mode, int type, char *file, int line)
                ^~~~~~~~~~~~~
    /usr/bin/ld: warning: libcrypto.so.1.0.2, needed by /usr/lib/gcc/x86_64-linux-gnu/6/../../../x86_64-linux-gnu/libcurl.so, may conflict with libcrypto.so.1.1

then you need to check if ``libssl1.0-dev`` had been installed properly. If you get these compilation warnings, this program will ocassionally crash if you connect to HTTPS website. This is because OpenSSL 1.0.2 needs those functions for thread safety, whereas OpenSSL 1.1 does not. If you have ``libssl-dev`` rather than ``libssl1.0-dev`` installed, those call back functions will not be linked properly.

If you have OpenSSL 1.1 and the associated development headers installed, then you can safely ignore these warning messages. If you are on Debian Buster, you will definitely get these warning messages, and you can safely ignore them. 

## Usage

	./httpdirfs -f $URL $YOUR_MOUNT_POINT

An example URL would be [Debian CD Image Server](https://cdimage.debian.org/debian-cd/). The ``-f`` flag keeps the program in the foreground, which is useful for monitoring which URL the filesystem is visiting. 

Other useful options:

    -u   --username        HTTP authentication username
    -p   --password        HTTP authentication password
    -P   --proxy           Proxy for libcurl, for more details refer to
        https://curl.haxx.se/libcurl/c/CURLOPT_PROXY.html
            --proxy-username      Username for the proxy
            --proxy-password      Password for the proxy
            --cache           Set the cache folder
## Permanent cache system
You can now cache all the files you have looked at permanently on your hard
drive by using the ``--cache`` flag. The file it caches persist across sessions For example:

    mkdir cache mnt
    httpdirfs --cache cache http://cdimage.debian.org/debian-cd/ mnt

Once a segment of the file has been downloaded once, it won't be downloaded again. So the first time you use the file it is slow, the subsequent access is fast. You can also retrieve your partially or fully downloaded file from ``cache/metadata``.

## Configuration file support
There is now rudimentary config file support. The configuration file that the program will read is ``${XDG_CONFIG_HOME}/httpdirfs/config``. If ``${XDG_CONFIG_HOME}`` is not set, it will default to ``${HOME}/.config``. So by default you need to put the configuration file at ``${HOME}/.config/httpdirfs/config``. You will have to create the sub-directory and the configuration file yourself. In the configuration file, please supply one option per line. For example: 

	$ cat ${HOME}/.config/httpdirfs/config
	--username test
	--password test
	-f

## SSL Support
If you run the program in the foreground, when it starts up, it will output the SSL engine version string. Please verify that your libcurl is linked against OpenSSL, as the pthread mutex functions are designed for OpenSSL.

The SSL engine version string looks something like this:

        libcurl SSL engine: OpenSSL/1.0.2l

## The Technical Details
I noticed that most HTTP directory listings don't provide the file size for the web page itself. I suppose this makes perfect sense, as they are generated on the fly. Whereas the actual files have got file sizes. So the listing pages can be treated as folders, and the rest are files. 

This program downloads the HTML web pages/files using [libcurl](https://curl.haxx.se/libcurl/), then parses the listing pages using [Gumbo](https://github.com/google/gumbo-parser), and presents them using [libfuse](https://github.com/libfuse/libfuse)
