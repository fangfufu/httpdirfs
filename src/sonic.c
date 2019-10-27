#include "sonic.h"

#include "util.h"
#include "link.h"
#include "network.h"

#include <expat.h>

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
 * \brief generate a download request URL
 */
static char *sonic_stream_link(const int id)
{
    char *first_part = sonic_gen_url_first_part("stream");
    char *url = CALLOC(MAX_PATH_LEN + 1, sizeof(char));
    snprintf(url, MAX_PATH_LEN,
             "%s&estimateContentLength=true&format=raw&id=%d", first_part, id);
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
static void XMLCALL XML_parser_index_mode(void *data, const char *elem,
                                               const char **attr)
{
    LinkTable *linktbl = (LinkTable *) data;
    Link *link;
    if (!strcmp(elem, "child")) {
        /* Return from getMusicDirectory */
        link = CALLOC(1, sizeof(Link));
        link->type = LINK_INVALID;
    } else if (!strcmp(elem, "artist")){
        /* Return from getIndexes */
        link = CALLOC(1, sizeof(Link));
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
            link->sonic_id_str = calloc(MAX_FILENAME_LEN, sizeof(char));
            snprintf(link->sonic_id_str, MAX_FILENAME_LEN, "%d",
                     link->sonic_id);
            id_set = 1;
            continue;
        }

        /*
         * "title" is used for directory name,
         * "name" is for top level directories
         */
        if (!strcmp("title", attr[i]) || !strcmp("name", attr[i])) {
            strncpy(link->linkname, attr[i+1], MAX_FILENAME_LEN);
            linkname_set = 1;
            continue;
        }

        /*
         * Path always appears after title, it is used for filename.
         * This is why it is safe to rewrite linkname
         */
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

        if (!strcmp("isDir", attr[i])) {
            if (!strcmp("true", attr[i+1])) {
                link->type = LINK_DIR;
            } else if (!strcmp("false", attr[i+1])) {
                link->type = LINK_FILE;
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

    /* Clean up if linkname is not set */
    if (!linkname_set) {
        free(link);
        return;
    }

    /* Clean up if id is not set */
    if (!id_set) {
        if (linkname_set) {
            free(link->linkname);
        }
        free(link);
        return;
    }

    if (link->type == LINK_DIR) {
        char *url = sonic_getMusicDirectory_link(link->sonic_id);
        strncpy(link->f_url, url, MAX_PATH_LEN);
        free(url);
    } else if (link->type == LINK_FILE) {
        char *url = sonic_stream_link(link->sonic_id);
        strncpy(link->f_url, url, MAX_PATH_LEN);
        free(url);
    } else {
        /* Invalid link */
        free(link->linkname);
        free(link);
        return;
    }

    LinkTable_add(linktbl, link);
}

/**
 * \brief The parser for Sonic ID3 mode
 * \details Please refer to the details for XML_parser_index_mode()
 */
static void XMLCALL XML_parser_id3_mode(void *data, const char *elem,
                                          const char **attr)
{
    LinkTable *linktbl = (LinkTable *) data;
}

/**
 * \brief parse a XML string in order to fill in the LinkTable
 */
static LinkTable *sonic_url_to_LinkTable(const char *url,
                                         XML_StartElementHandler handler)
{
    LinkTable *linktbl = LinkTable_alloc(url);

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

LinkTable *sonic_LinkTable_new_index_mode(const int id)
{
    char *url;
    if (id > 0) {
        url = sonic_getMusicDirectory_link(id);
    } else {
        url = sonic_gen_url_first_part("getIndexes");
    }
    LinkTable *linktbl = sonic_url_to_LinkTable(url, XML_parser_index_mode);
    free(url);
    return linktbl;
}

LinkTable *sonic_LinkTable_new_id3_root()
{
    char *url = sonic_gen_url_first_part("getArtists");
    LinkTable *linktbl = sonic_url_to_LinkTable(url, XML_parser_index_mode);
    free(url);
    return linktbl;
}

LinkTable *sonic_LinkTable_new_id3_mode(char *sonic_id_str)
{
    /* Count the number of '/' */
    int n = 0;
    for (char *c = sonic_id_str; *c; c++) {
        if (*c == '/') {
            n++;
        }
    }

    char *url;
    switch (n) {
        /* Root level */
        case 1:
            break;
        /* Index level */
        case 2:
            break;
        /* Artist level */
        case 3:
            break;
        /* Album level*/
        case 4:
            break;
        /* Invalid */
        default:
            break;
    }
    return NULL;
}

