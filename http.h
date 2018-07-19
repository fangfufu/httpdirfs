#ifndef HTTP_H
#define HTTP_H

#include <curl/curl.h>

typedef struct {
    CURL *handle;               /* handle */

    int still_running;          /* Is background url fetch still in progress */

    char *buffer;               /* buffer to store cached data*/
    size_t buffer_len;          /* currently allocated buffers length */
    size_t buffer_pos;          /* end of data in buffer*/

    char *header;               /* character array to store the header */
    size_t header_len;          /* the current header length */
    size_t header_pos;          /* end of header in buffer */

    int accept_range;           /* does it accept range request */
    int content_length;         /* the length of the content */

} URL_FILE;

URL_FILE *url_fopen(const char *url, const char *operation);

int url_fclose(URL_FILE *file);

int url_feof(URL_FILE *file);

size_t url_fread(void *ptr, size_t size, size_t nmemb, URL_FILE *file);

/*
 * \brief fgets implemented using libcurl.
 * \details This is probably not the function that you want to use,
 *      because it doesn't work well with binary!
 */
char *url_fgets(char *ptr, size_t size, URL_FILE *file);

void url_rewind(URL_FILE *file);

#endif
