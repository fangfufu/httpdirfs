#include "util.h"

#include <openssl/md5.h>
#include <uuid/uuid.h>

#include <string.h>
#include <stdlib.h>

#define MD5_HASH_LEN 32
#define SALT_LEN 36
/**
 * \brief generate the salt for authentication string
 * \details this effectively generates a UUID string, which we use as the salt
 * \return a pointer to a 37-char array with the salt.
 */
char *generate_salt()
{
    char *out;
    out = calloc(SALT_LEN + 1, sizeof(char));
    uuid_t uu;
    uuid_generate(uu);
    uuid_unparse(uu, out);
    return out;
}

/**
 * \brief generate the md5sum of a string
 * \param[in] str a character array for the input string
 * \return a pointer to a 33-char array with the salt
 */
char *generate_md5sum(const char *str)
{
    MD5_CTX c;
    unsigned char md5[MD5_DIGEST_LENGTH];
    size_t len = strnlen(str, MAX_PATH_LEN);
    char *out = calloc(MD5_HASH_LEN + 1, sizeof(char));

    MD5_Init(&c);
    MD5_Update(&c, str, len);
    MD5_Final(md5, &c);

    for(int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(out + 2 * i, "%02x", md5[i]);
    }
    return out;
}

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    char *salt = generate_salt();
    char *md5sum = generate_md5sum(salt);

    printf("%s\n%s\n", salt, md5sum);
}
