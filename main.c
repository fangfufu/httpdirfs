#include "network.h"

/* must be included before including <fuse.h> */
#define FUSE_USE_VERSION 26
#include <fuse.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *BASE_URL;

static void fs_usage();
static void *fs_init(struct fuse_conn_info *conn);
static int fs_getattr(const char *path, struct stat *stbuf);
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi);
static int fs_open(const char *path, struct fuse_file_info *fi);
static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi);


static struct fuse_operations fs_oper = {
    .getattr	= fs_getattr,
    .readdir	= fs_readdir,
    .open		= fs_open,
    .read		= fs_read,
    .init       = fs_init
};

int main(int argc, char **argv) {
    /*
     * Copied from:
     * https://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/src/bbfs.c
     */
    if ((argc < 3)) {
        fs_usage();
    }

    //Parse the command line options
    char password[256];
    char username[256];
    int c;
    //Hold the position of the username and password arguments so 
    //we can remove them later (fuse doesn't like them)
    int passpos = -1;
    int userpos = -1;
    for (c = 0; c < argc; c++)
    {
	    if (!strcmp(argv[c], "-p"))
	    {
	        strcpy(password, argv[c + 1]);
            passpos = c;
	    }
	    if (!strcmp(argv[c], "-u"))
	    {
            userpos = c;
	        strcpy(username, argv[c + 1]);
	    }
    }
    fprintf(stderr, "Username and password are %s:%s\n", username, password);

    //Remove the url from the command line options.
    BASE_URL = argv[argc-2];
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;


    //This holds the final url adjusted for passwords
    char passurl[512];
    char noheaderurl[256];

    if ((username != NULL) && (password != NULL))
    {
	
	    char* doeshavehttpheader = strstr(BASE_URL, "://");
        
	    //XXX - Forces http by default.
	    strcpy(passurl, "http://");
	    strcat(passurl, username);
        strcat(passurl, ":");
        strcat(passurl, password);
        strcat(passurl, "@");
        //Get rid of the http/s crap and put it on the request url.
        if (doeshavehttpheader != NULL)
	    {
            strcpy(noheaderurl, doeshavehttpheader);
            strcat(passurl, noheaderurl+3);
	    }
        else
        {
            strcpy(noheaderurl, BASE_URL);
            strcat(passurl, noheaderurl);
        }
        BASE_URL = passurl;

        //Assemble the arguments into a form fuse likes, excluding the password and username arguments (args in total)
        int argcfuse = argc-4;
        char** argvfuse = malloc(argcfuse);
        //XXX Allocate 64 characters for each argument, should probably make dynamic
        int k;
        int argnum = 0;
        for (k = 0; k < argc; k++)
        {
            argvfuse[argnum] = malloc(64);
            //Skip over the argument if its a password or username.
            if ((k == passpos) || (k == userpos) || (k == userpos+1) || (k == passpos + 1))
            {
                continue;
            }
            //Otherwise put it in the array
            else
            {
                strcpy(argvfuse[argnum], argv[k]);
                argnum++;
            }

        }
        return fuse_main(argcfuse, argvfuse, &fs_oper, NULL);
    }



    
    return fuse_main(argc, argv, &fs_oper, NULL);
}

static void fs_usage()
{
    fprintf(stderr,
            "usage:  mount-http-dir [options] URL mount_point\n");
    exit(EXIT_FAILURE);
}

static void *fs_init(struct fuse_conn_info *conn)
{
    (void) conn;
    network_init(BASE_URL);
    return NULL;
}

/** \brief return the attributes for a single file indicated by path */
static int fs_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;
    memset(stbuf, 0, sizeof(struct stat));

    if (!strcmp(path, "/")) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 1;
    } else {
        Link *link = path_to_Link(path);
        if (!link) {
            return -ENOENT;
        }
        struct timespec spec;
        spec.tv_sec = link->time;
        stbuf->st_mtim = spec;
        switch (link->type) {
            case LINK_DIR:
                stbuf->st_mode = S_IFDIR | 0755;
                stbuf->st_nlink = 1;
                break;
            case LINK_FILE:
                stbuf->st_mode = S_IFREG | 0444;
                stbuf->st_nlink = 1;
                stbuf->st_size = link->content_length;
                break;
            default:
                return -ENOENT;
        }
    }
    return res;
}

/** \brief read the directory indicated by the path*/
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t dir_add,
                      off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    Link *link;
    LinkTable *linktbl;


    if (!strcmp(path, "/")) {
        linktbl = ROOT_LINK_TBL;
    } else {
        link = path_to_Link(path);
        if (!link) {
            return -ENOENT;
        }
        linktbl = link->next_table;
        if (!linktbl) {
            linktbl = LinkTable_new(link->f_url);
            if(!linktbl) {
                return -ENOENT;
            }
        }
    }

    /* start adding the links */
    dir_add(buf, ".", NULL, 0);
    dir_add(buf, "..", NULL, 0);
    for (int i = 1; i < linktbl->num; i++) {
        link = linktbl->links[i];
        if (link->type != LINK_INVALID) {
            dir_add(buf, link->p_url, NULL, 0);
        }
    }

    return 0;
}

/** \brief open a file indicated by the path */
static int fs_open(const char *path, struct fuse_file_info *fi)
{
    if (!path_to_Link(path)) {
        return -ENOENT;
    }

    if ((fi->flags & 3) != O_RDONLY) {
        return -EACCES;
    }

    return 0;
}

/** \brief read a file */
static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi)
{
    (void) fi;

    long received = path_download(path, buf, size, offset);

    return received;
}
