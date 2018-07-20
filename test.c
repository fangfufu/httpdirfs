#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "link.h"
#include "test.h"
#include "http.h"


int http_test()
{
    return 0;
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
    LinkTable *linktbl = LinkTable_new();
    HTML_to_LinkTable(output->root, linktbl);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    LinkTable_print(linktbl);
    LinkTable_free(linktbl);
    printf("--- end of gumbo_test ---\n\n");
}
