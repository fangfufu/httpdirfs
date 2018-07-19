## Basic ideas
# Syntax
mount-http-directory-listing url cache mount_pt

# Libraries used
libcurl libgumbo libfuse


- Seek an URL
- If fail, continue
- Download it
- Allocate random directory
- Parse it
    - For each link, seek
    - if fail
        - directory
    - else
        - actual file

Things to write:
1) Link parser
2) libcurl
3) fuser
