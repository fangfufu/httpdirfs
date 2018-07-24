# HTTPDirFS

Have you ever wanted to mount those HTTP directory listings as if it was a partition? Look no further, this is your solution.  HTTPDirFS stands for Hyper Text Transfer Protocol Directory Filesystem

The performance of the program is excellent, due to the use of curl-multi interface. HTTP connections are reused, and HTTP pipelining is used when available. I haven't benchmarked it, but I feel this is faster than ``rclone mount``. The FUSE component itself also runs in multithreaded mode.

## Compilation
This program was developed under Debian Stretch. If you are using the same operation system as me, you need ``libgumbo-dev``, ``libfuse-dev`` and ``libcurl4-openssl-dev``.

## Usage

		./httpdirfs -f $URL $YOUR_MOUNT_POINT

An example URL would be [Debian CD Image Server](https://cdimage.debian.org/debian-cd/). The ``-f`` flag keeps the program in the foreground, which is useful for monitoring which URL the filesystem is visiting. 

If you run the program in the foreground, when it starts up, it will output the SSL engine version string. Please verify that your libcurl is linked agains OpenSSL, otherwise it might crash when you are downloading from HTTPS websites.

The SSL engine version string looks something like this:

        libcurl SSL engine: OpenSSL/1.0.2l

## The Technical Details
I noticed that most HTTP directory listings don't provide the file size for the web page itself. I suppose this makes perfect sense, as they are generated on the fly. Whereas the actual files have got file sizes. So the listing pages can be treated as folders, and the rest are files. 

This program downloads the HTML web pages/files using [libcurl](https://curl.haxx.se/libcurl/), then parses the listing pages using [Gumbo](https://github.com/google/gumbo-parser), and presents them using [libfuse](https://github.com/libfuse/libfuse)

## LICENSE
	This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
