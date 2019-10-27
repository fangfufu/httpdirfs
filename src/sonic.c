#include "sonic.h"

#include "util.h"
#include "link.h"
#include "network.h"

#include <expat.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    char *server;
    char *username;
    char *password;
    char *client;
    char *api_version;
} SonicConfigStruct;

static SonicConfigStruct SONIC_CONFIG;

/**
 * \brief initalise Sonic configuration struct
 */
void sonic_config_init(const char *server, const char *username,
                       const char *password)
{
    SONIC_CONFIG.server = strndup(server, MAX_PATH_LEN);
    /* Correct for the extra '/' */
    size_t server_url_len = strnlen(SONIC_CONFIG.server, MAX_PATH_LEN) - 1;
    if (SONIC_CONFIG.server[server_url_len] == '/') {
        SONIC_CONFIG.server[server_url_len] = '\0';
    }
    SONIC_CONFIG.username = strndup(username, MAX_FILENAME_LEN);
    SONIC_CONFIG.password = strndup(password, MAX_FILENAME_LEN);
    SONIC_CONFIG.client = DEFAULT_USER_AGENT;
    /*
     * API 1.13.0 is the minimum version that supports
     * salt authentication scheme
     */
    SONIC_CONFIG.api_version = "1.13.0";
}

/**
 * \brief generate authentication string
 */
static char *sonic_gen_auth_str(void)
{
    char *salt = generate_salt();
    size_t password_len = strnlen(SONIC_CONFIG.password, MAX_FILENAME_LEN);
    size_t password_salt_len = password_len + strnlen(salt, MAX_FILENAME_LEN);
    char *password_salt = CALLOC(password_salt_len + 1, sizeof(char));
    strncat(password_salt, SONIC_CONFIG.password, MAX_FILENAME_LEN);
    strncat(password_salt + password_len, salt, MAX_FILENAME_LEN);
    char *token = generate_md5sum(password_salt);
    char *auth_str = CALLOC(MAX_PATH_LEN + 1, sizeof(char));
    snprintf(auth_str, MAX_PATH_LEN,
                        ".view?u=%s&t=%s&s=%s&v=%s&c=%s",
                        SONIC_CONFIG.username, token, salt,
                        SONIC_CONFIG.api_version, SONIC_CONFIG.client);
    free(salt);
    free(token);
    return auth_str;
}

/**
 * \brief generate the first half of the request URL
 */
static char *sonic_gen_url_first_part(char *method)
{
    char *auth_str = sonic_gen_auth_str();
    char *url = CALLOC(MAX_PATH_LEN + 1, sizeof(char));
    snprintf(url, MAX_PATH_LEN, "%s/rest/%s%s", SONIC_CONFIG.server, method,
             auth_str);
    free(auth_str);
    return url;
}

/**
 * \brief generate a getMusicDirectory request URL
 */
static char *sonic_getMusicDirectory_link(const int id)
{
    char *first_part = sonic_gen_url_first_part("getMusicDirectory");
    char *url = CALLOC(MAX_PATH_LEN + 1, sizeof(char));
    snprintf(url, MAX_PATH_LEN, "%s&id=%d", first_part, id);
    free(first_part);
    return url;
}

/**
 * \brief generate a getArtist request URL
 */
static char *sonic_getArtist_link(const int id)
{
    char *first_part = sonic_gen_url_first_part("getArtist");
    char *url = CALLOC(MAX_PATH_LEN + 1, sizeof(char));
    snprintf(url, MAX_PATH_LEN, "%s&id=%d", first_part, id);
    free(first_part);
    return url;
}

/**
 * \brief generate a getAlbum request URL
 */
static char *sonic_getAlbum_link(const int id)
{
    char *first_part = sonic_gen_url_first_part("getAlbum");
    char *url = CALLOC(MAX_PATH_LEN + 1, sizeof(char));
    snprintf(url, MAX_PATH_LEN, "%s&id=%d", first_part, id);
    free(first_part);
    return url;
}

/**
 * \brief generate a download request URL
 */
static char *sonic_stream_link(const int id)
{
    char *first_part = sonic_gen_url_first_part("stream");
    char *url = CALLOC(MAX_PATH_LEN + 1, sizeof(char));
    snprintf(url, MAX_PATH_LEN,
             "%s&format=raw&id=%d", first_part, id);
    free(first_part);
    return url;
}

/**
 * \brief The parser for Sonic index mode
 * \details This is the callback function called by the the XML parser.
 * \param[in] data user supplied data, in this case it is the pointer to the
 * LinkTable.
 * \param[in] elem the name of this element, it should be either "child" or
 * "artist"
 * \param[in] attr Each attribute seen in a start (or empty) tag occupies
 * 2 consecutive places in this vector: the attribute name followed by the
 * attribute value. These pairs are terminated by a null pointer.
 * \note we are using strcmp rather than strncmp, because we are assuming the
 * parser terminates the strings properly, which is a fair assumption,
 * considering how mature expat is.
 */
static void XMLCALL XML_parser_index(void *data, const char *elem,
                                               const char **attr)
{
    LinkTable *linktbl = (LinkTable *) data;
    Link *link;
    if (!strcmp(elem, "child") || !strcmp(elem, "artist")) {
        link = CALLOC(1, sizeof(Link));
        /* Initialise to LINK_DIR, as the LINK_FILE is set later. */
        link->type = LINK_DIR;
    } else {
        /* The element does not contain directory structural information */
        return;
    }

    int id_set = 0;
    int linkname_set = 0;

    for (int i = 0; attr[i]; i += 2) {
        if (!strcmp("id", attr[i])) {
            link->sonic_id = atoi(attr[i+1]);
            link->sonic_song_id_str = calloc(MAX_FILENAME_LEN, sizeof(char));
            snprintf(link->sonic_song_id_str, MAX_FILENAME_LEN, "%d",
                     link->sonic_id);
            id_set = 1;
            continue;
        }

        if (!strcmp("path", attr[i])) {
            memset(link->linkname, 0, MAX_FILENAME_LEN);
            /* Skip to the last '/' if it exists */
            char *s = strrchr(attr[i+1], '/');
            if (s) {
                strncpy(link->linkname, s + 1, MAX_FILENAME_LEN);
            } else {
                strncpy(link->linkname, attr[i+1], MAX_FILENAME_LEN);
            }
            linkname_set = 1;
            continue;
        }

        /*
         * "title" is used for directory name,
         * "name" is for top level directories
         * N.B. "path" attribute is given the preference
         */
        if (!linkname_set) {
            if (!strcmp("title", attr[i]) || !strcmp("name", attr[i])) {
                strncpy(link->linkname, attr[i+1], MAX_FILENAME_LEN);
                linkname_set = 1;
                continue;
            }
        }

        if (!strcmp("isDir", attr[i])) {
            if (!strcmp("false", attr[i+1])) {
                link->type = LINK_FILE;
                char *url = sonic_stream_link(link->sonic_id);
                strncpy(link->f_url, url, MAX_PATH_LEN);
                free(url);
            }
            continue;
        }

        if (!strcmp("created", attr[i])) {
            struct tm *tm = calloc(1, sizeof(struct tm));
            strptime(attr[i+1], "%Y-%m-%dT%H:%M:%S.000Z", tm);
            link->time = mktime(tm);
            free(tm);
            continue;
        }

        if (!strcmp("size", attr[i])) {
            link->content_length = atoll(attr[i+1]);
            continue;
        }
    }

    /* Clean up if linkname or id is not set */
    if (!linkname_set || !id_set) {
        free(link);
        return;
    }

    LinkTable_add(linktbl, link);
}

/**
 * \brief parse a XML string in order to fill in the LinkTable
 */
static LinkTable *sonic_url_to_LinkTable(const char *url,
                                         XML_StartElementHandler handler,
                                         int depth)
{
    LinkTable *linktbl = LinkTable_alloc(url);
    linktbl->links[0]->sonic_depth = depth;

    /* start downloading the base URL */
    DataStruct xml = Link_to_DataStruct(linktbl->links[0]);
    if (xml.size == 0) {
        LinkTable_free(linktbl);
        return NULL;
    }

    XML_Parser parser = XML_ParserCreate(NULL);
    XML_SetUserData(parser, linktbl);

    XML_SetStartElementHandler(parser, handler);

    if (XML_Parse(parser, xml.data, xml.size, 1) == XML_STATUS_ERROR) {
        fprintf(stderr,
                "sonic_XML_to_LinkTable(): Parse error at line %lu: %s\n",
                XML_GetCurrentLineNumber(parser),
                XML_ErrorString(XML_GetErrorCode(parser)));
    }

    XML_ParserFree(parser);

    free(xml.data);

    LinkTable_print(linktbl);

    return linktbl;

}

LinkTable *sonic_LinkTable_new_index(const int id)
{
    char *url;
    if (id > 0) {
        url = sonic_getMusicDirectory_link(id);
    } else {
        url = sonic_gen_url_first_part("getIndexes");
    }
    LinkTable *linktbl = sonic_url_to_LinkTable(url, XML_parser_index, 0);
    free(url);
    return linktbl;
}


static void XMLCALL XML_parser_id3_root(void *data, const char *elem,
                                   const char **attr)
{
    LinkTable *root_linktbl = (LinkTable *) data;
    LinkTable *this_linktbl = NULL;

    /* Set the current linktbl, if we have more than head link. */
    if (root_linktbl->num > 1) {
        this_linktbl = root_linktbl->links[root_linktbl->num - 1]->next_table;
    }

    int id_set = 0;
    int linkname_set = 0;
    Link *link;
    if (!strcmp(elem, "index")) {
        /* Add a subdirectory */
        link = CALLOC(1, sizeof(Link));
        link->type = LINK_DIR;
        for (int i = 0; attr[i]; i += 2) {
            if (!strcmp("name", attr[i])) {
                strncpy(link->linkname, attr[i+1], MAX_FILENAME_LEN);
                linkname_set = 1;
                /* Allocate a new LinkTable */
                link->next_table = LinkTable_alloc("/");
            }
        }
        /* Make sure we don't add an empty directory */
        if (linkname_set) {
            LinkTable_add(root_linktbl, link);
        } else {
            free(link);
        }
        return;
    } else if (!strcmp(elem, "artist")) {
        link = CALLOC(1, sizeof(Link));
        link->type = LINK_DIR;
        /* This table should be a level 3 album table */
        link->sonic_depth = 3;
        for (int i = 0; attr[i]; i += 2) {
            if (!strcmp("name", attr[i])) {
                strncpy(link->linkname, attr[i+1], MAX_FILENAME_LEN);
                linkname_set = 1;
                continue;
            }

            if (!strcmp("id", attr[i])) {
                link->sonic_id = atoi(attr[i+1]);
                id_set = 1;
                continue;
            }
        }

        /* Clean up if linkname is not set */
        if (!linkname_set || !id_set) {
            free(link);
            return;
        }

        LinkTable_add(this_linktbl, link);
    }
    /* If we reach here, this element does not contain directory structural
     * information */
}

static void XMLCALL XML_parser_id3(void *data, const char *elem,
                                        const char **attr)
{
    LinkTable *linktbl = (LinkTable *) data;
    Link *link;

    /*
     * Please refer to the documentation at the function prototype of
     * sonic_LinkTable_new_id3()
     */
    if (!strcmp(elem, "album") && linktbl->links[0]->sonic_depth == 3) {
        link = CALLOC(1, sizeof(Link));
        link->type = LINK_DIR;
        /* This table should be a level 3 album table */
        link->sonic_depth = 4;
    } else if (!strcmp(elem, "song") && linktbl->links[0]->sonic_depth == 4) {
        link = CALLOC(1, sizeof(Link));
        link->type = LINK_FILE;
    } else {
        return;
    }

    int id_set = 0;
    int linkname_set = 0;

    int track = 0;
    char *title = "";
    char *suffix = "";
    for (int i = 0; attr[i]; i += 2) {
        if (!strcmp("id", attr[i])) {
            link->sonic_id = atoi(attr[i+1]);
            link->sonic_song_id_str = calloc(MAX_FILENAME_LEN, sizeof(char));
            snprintf(link->sonic_song_id_str, MAX_FILENAME_LEN, "%d",
                     link->sonic_id);
            id_set = 1;
            continue;
        }

        if (!strcmp("size", attr[i])) {
            link->content_length = atoll(attr[i+1]);
            continue;
        }

        if (!strcmp("created", attr[i])) {
            struct tm *tm = calloc(1, sizeof(struct tm));
            strptime(attr[i+1], "%Y-%m-%dT%H:%M:%S.000Z", tm);
            link->time = mktime(tm);
            free(tm);
            continue;
        }

        /* This is used by the album table */
        if (!strcmp("name", attr[i])) {
            strncpy(link->linkname, attr[i+1], MAX_FILENAME_LEN);
            linkname_set = 1;
            continue;
        }

        if (!strcmp("path", attr[i])) {
            memset(link->linkname, 0, MAX_FILENAME_LEN);
            /* Skip to the last '/' if it exists */
            char *s = strrchr(attr[i+1], '/');
            if (s) {
                strncpy(link->linkname, s + 1, MAX_FILENAME_LEN);
            } else {
                strncpy(link->linkname, attr[i+1], MAX_FILENAME_LEN);
            }
            linkname_set = 1;
            continue;
        }

        if (!strcmp("track", attr[i])) {
            track = atoi(attr[i+1]);
        }

        if (!strcmp("title", attr[i])) {
            title = (char *) attr[i+1];
        }

        if (!strcmp("suffix", attr[i])) {
            suffix = (char *) attr[i+1];
        }
    }

    if (!linkname_set && strlen(title) > 0 && strlen(suffix) > 0) {
        snprintf(link->linkname, MAX_FILENAME_LEN, "%02d - %s.%s",
                 track, title, suffix);
        linkname_set = 1;
    }

    if (!linkname_set || !id_set) {
        free(link);
        return;
    }

    if (link->type == LINK_FILE) {
        char *url = sonic_stream_link(link->sonic_id);
        strncpy(link->f_url, url, MAX_PATH_LEN);
        free(url);
    }

    LinkTable_add(linktbl, link);
}

LinkTable *sonic_LinkTable_new_id3(int depth, int id)
{
    char *url;
    LinkTable *linktbl = ROOT_LINK_TBL;
    switch (depth) {
        /* Root table */
        case 0:
            url = sonic_gen_url_first_part("getArtists");
            linktbl = sonic_url_to_LinkTable(url, XML_parser_id3_root, 0);
            free(url);
            break;
        /* Album table - get all the albums of an artist */
        case 3:
            url = sonic_getArtist_link(id);
            linktbl = sonic_url_to_LinkTable(url, XML_parser_id3, depth);
            free(url);
            break;
        /* Song table - get all the songs of an album */
        case 4:
            url = sonic_getAlbum_link(id);
            linktbl = sonic_url_to_LinkTable(url, XML_parser_id3, depth);
            free(url);
            break;
        default:
            /*
             * We shouldn't reach here.
             */
            fprintf(stderr, "sonic_LinkTable_new_id3(): case %d.\n", depth);
            exit_failure();
            break;
    }
    return linktbl;
}

