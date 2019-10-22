#include "sonic.h"

#include "util.h"
#include "link.h"
#include "network.h"

#include <expat.h>

#include <string.h>
#include <stdlib.h>

typedef struct {
    char *server;
    char *username;
    char *password;
    char *client;
    char *api_version;
} SonicConfigStruct;

static SonicConfigStruct SONIC_CONFIG;

/**
 * \brief initalise Subsonic configuration struct
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
static char *sonic_gen_auth_str()
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

LinkTable *sonic_LinkTable_new(const int id)
{
    char *url;
    if (id > 0) {
        char *first_part = sonic_gen_url_first_part("getMusicDirectory");
        url = CALLOC(MAX_PATH_LEN + 1, sizeof(char));
        snprintf(url, MAX_PATH_LEN, "%s&id=%d", first_part, id);
        free(first_part);
    } else {
        url = sonic_gen_url_first_part("getIndexes");
    }

    printf("%s\n", url);

    LinkTable *linktbl = LinkTable_alloc(url);

    /* start downloading the base URL */
    MemoryStruct buf = Link_to_MemoryStruct(linktbl->links[0]);
    if (buf.size == 0) {
        LinkTable_free(linktbl);
        return NULL;
    }

    printf("%s", buf.memory);

    free(buf.memory);
    free(url);
    return NULL;
}

// int main(int argc, char **argv)
// {
//     (void) argc;
//     (void) argv;
//
//     sonic_config_init(argv[1], argv[2], argv[3]);
//
//     link_system_init();
//     network_config_init();
//
//     sonic_LinkTable_new(0);
//     sonic_LinkTable_new(3);
// }
