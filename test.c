#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "link.h"
#include "test.h"
#include "http.h"


int http_test()
{
    URL_FILE *handle;
    FILE *outf;

    size_t nread;
    char buffer[256];
    const char *url;

    url = "http://127.0.0.1/~fangfufu/test.txt";

    /* ---------------------------Test fgets ----------------------------*/

    /* open the input file */
    handle = url_fopen(url, "hr");
    if(!handle) {
        printf("couldn't url_fopen() %s\n", url);
        return 2;
    }

    /* create the output file for fgets*/
    outf = fopen("fgets_test.txt", "wb");
    if(!outf) {
        perror("couldn't open output file\n");
        return 1;
    }

    /* copy from url line by line with fgets */
    while(!url_feof(handle)) {
        url_fgets(buffer, sizeof(buffer), handle);
        fwrite(buffer, 1, strlen(buffer), outf);
    }

    /* Print the header */
    printf(handle->header);
    printf("\n");
    printf("accept-range: %d\n", handle->accept_range);
    printf("filesize: %d\n", handle->content_length);

    /* close the handles for the fgets test*/
    url_fclose(handle);
    fclose(outf);

    /* ---------------------------Test fread ----------------------------*/


    /* open the input file again */
    handle = url_fopen(url, "r");
    if(!handle) {
        printf("couldn't url_fopen() testfile\n");
        return 2;
    }

    /* create the output file for fread test*/
    outf = fopen("fread_test.txt", "wb");
    if(!outf) {
        perror("couldn't open fread output file\n");
        return 1;
    }

    /* Copy from url with fread */
    do {
        nread = url_fread(buffer, 1, sizeof(buffer), handle);
        fwrite(buffer, 1, nread, outf);
    } while(nread);

    /* close the handles for the fgets test*/
    url_fclose(handle);
    fclose(outf);

    /* ---------------------------Test rewind ----------------------------*/
    /* open the input file again */
    handle = url_fopen(url, "r");
    if(!handle) {
        printf("couldn't url_fopen() testfile\n");
        return 2;
    }

    /* create the output file for rewind test*/
    outf = fopen("rewind_test.txt", "wb");
    if(!outf) {
        perror("couldn't open fread output file\n");
        return 1;
    }

    /* Copy from url with fread */
    do {
        nread = url_fread(buffer, 1, sizeof(buffer), handle);
        fwrite(buffer, 1, nread, outf);
    } while(nread);

    url_rewind(handle);
    fprintf(outf, "\n-------------------\n");

    /*
     * read the URL again after rewind:
     *  - copy from url line by line with fgets
     */
    while(!url_feof(handle)) {
        url_fgets(buffer, sizeof(buffer), handle);
        fwrite(buffer, 1, strlen(buffer), outf);
    }

    buffer[0]='\n';
    fwrite(buffer, 1, 1, outf);

    nread = url_fread(buffer, 1, sizeof(buffer), handle);
    fwrite(buffer, 1, nread, outf);

    url_fclose(handle);

    fclose(outf);

    return 0;/* all done */
}
void url_test()
{
    printf("--- start of url_test ---\n");
    char *url1 = "http://www.google.com/";
    char *url2 = "http://www.google.com";
    char *cat_url1 = url_append(url1, "fangfufu");
    char *cat_url2 = url_append(url2, "fangfufu");
    printf("%d %s\n", (int) strlen(cat_url1), cat_url1);
    printf("%d %s\n", (int) strlen(cat_url2), cat_url2);
    printf("--- end of url_test ---\n\n");
}

void gumbo_test(int argc, char **argv)
{
    printf("--- start of gumbo_test ---\n");
    if (argc != 2) {
        fprintf(stderr, "Usage: find_links <html filename>.\n");
    }
    const char* filename = argv[1];

    FILE *fp;
    fp = fopen(filename, "r");

    if (!fp) {
        fprintf(stderr, "File %s not found!\n", filename);
    }

    fseek(fp, 0L, SEEK_END);
    unsigned long filesize = ftell(fp);
    rewind(fp);

    char* contents = (char*) malloc(sizeof(char) * filesize);
    if (fread(contents, 1, filesize, fp) != filesize) {
        fprintf(stderr, "Read error, %s\n", strerror(errno));
    }
    fclose(fp);

    GumboOutput* output = gumbo_parse(contents);
    ll_t *links = linklist_new();
    html_to_linklist(output->root, links);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    linklist_print(links);
    linklist_free(links);
    printf("--- end of gumbo_test ---\n\n");
}
