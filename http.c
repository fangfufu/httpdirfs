/**
 * \file http.c
 * \todo WARNING please fix url_feof
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "http.h"

/* we use a global one for convenience */
static CURLM *multi_handle;

static int num_transfers = 0;

/* curl calls this routine to get more data */
static size_t write_callback(char *buffer, size_t size,
                             size_t nitems, void *userp)
{
  char *newbuff;
  size_t rembuff;

  URL_FILE *url = (URL_FILE *)userp;
  size *= nitems;

  rembuff = url->buffer_len - url->buffer_pos; /* remaining space in buffer */

  if(size > rembuff) {
    /* not enough space in buffer */
    newbuff = realloc(url->buffer, url->buffer_len + (size - rembuff));
    if(newbuff == NULL) {
      fprintf(stderr, "callback buffer grow failed\n");
      size = rembuff;
    } else {
      /* realloc succeeded increase buffer size*/
      url->buffer_len += size - rembuff;
      url->buffer = newbuff;
    }
  }

  memcpy(&url->buffer[url->buffer_pos], buffer, size);
  url->buffer_pos += size;

  return size;
}

static size_t header_callback(char *buffer, size_t size,
                              size_t nitems, void *userp)
{
    char *newbuff;
    size_t rembuff;

    URL_FILE *url = (URL_FILE *)userp;
    size *= nitems;

    rembuff = url->header_len - url->header_pos; /* remaining space in buffer */

    if(size > rembuff) {
        /* not enough space in buffer */
        newbuff = realloc(url->header, url->header_len + (size - rembuff));
        if(newbuff == NULL) {
            fprintf(stderr, "callback buffer grow failed\n");
            size = rembuff;
        } else {
            /* realloc succeeded increase buffer size*/
            url->header_len += size - rembuff;
            url->header = newbuff;
            url->header[url->header_len] = '\0';
        }
    }

    memcpy(&url->header[url->header_pos], buffer, size);
    url->header_pos += size;

    char *hf;
    hf = "Accept-Ranges:";
    if (!strncasecmp(buffer, hf, strlen(hf))) {
        url->accept_range = 1;
    }
    hf = "Content-Length: ";
    if (!strncasecmp(buffer, hf, strlen(hf))) {
        /*
         * We are doing this, because libcurl documentation says
         *"Do not assume that the header line is zero terminated!"
         */
        char *tmp = malloc((nitems) * sizeof(char));
        tmp[nitems] = '\0';
        strncpy(tmp, buffer, nitems);
        url->content_length = atoi(strchr(tmp, ' ')+1);
    }
    return size;
}

/* use to attempt to fill the read buffer up to requested number of bytes */
static int fill_buffer(URL_FILE *file, size_t want)
{
  fd_set fdread;
  fd_set fdwrite;
  fd_set fdexcep;
  struct timeval timeout;
  int rc;
  CURLMcode mc; /* curl_multi_fdset() return code */

  /* only attempt to fill buffer if transactions still running and buffer
   * doesn't exceed required size already
   */
  if((!file->still_running) || (file->buffer_pos > want))
    return 0;

  /* attempt to fill buffer */
  do {
    int maxfd = -1;
    long curl_timeo = -1;

    FD_ZERO(&fdread);
    FD_ZERO(&fdwrite);
    FD_ZERO(&fdexcep);

    /* set a suitable timeout to fail on */
    timeout.tv_sec = 60; /* 1 minute */
    timeout.tv_usec = 0;

    curl_multi_timeout(multi_handle, &curl_timeo);
    if(curl_timeo >= 0) {
      timeout.tv_sec = curl_timeo / 1000;
      if(timeout.tv_sec > 1)
        timeout.tv_sec = 1;
      else
        timeout.tv_usec = (curl_timeo % 1000) * 1000;
    }

    /* get file descriptors from the transfers */
    mc = curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

    if(mc != CURLM_OK) {
      fprintf(stderr, "curl_multi_fdset() failed, code %d.\n", mc);
      break;
    }

    /* On success the value of maxfd is guaranteed to be >= -1. We call
       select(maxfd + 1, ...); specially in case of (maxfd == -1) there are
       no fds ready yet so we call select(0, ...) --or Sleep() on Windows--
       to sleep 100ms, which is the minimum suggested value in the
       curl_multi_fdset() doc. */

    if(maxfd == -1) {
      /* Portable sleep for platforms other than Windows. */
      struct timeval wait = { 0, 100 * 1000 }; /* 100ms */
      rc = select(0, NULL, NULL, NULL, &wait);

    } else {
      /* Note that on some platforms 'timeout' may be modified by select().
         If you need access to the original value save a copy beforehand. */
      rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
    }

    switch(rc) {
    case -1:
      /* select error */
      break;

    case 0:
    default:
      /* timeout or readable/writable sockets */
      curl_multi_perform(multi_handle, &file->still_running);
      break;
    }
  } while(file->still_running && (file->buffer_pos < want));
  return 1;
}

/* use to remove want bytes from the front of a files buffer */
static int use_buffer(URL_FILE *file, size_t want)
{
  /* sort out buffer */
  if((file->buffer_pos - want) <= 0) {
    /* ditch buffer - write will recreate */
    free(file->buffer);
    file->buffer = NULL;
    file->buffer_pos = 0;
    file->buffer_len = 0;
  }
  else {
    /* move rest down make it available for later */
    memmove(file->buffer,
            &file->buffer[want],
            (file->buffer_pos - want));

    file->buffer_pos -= want;
  }
  return 0;
}

static void start_fetching(URL_FILE *file)
{
    /* lets start the fetch */
    curl_multi_perform(multi_handle, &file->still_running);

    /* if still_running is 0 now, we should close the file descriptor */
    if (url_feof(file)) {
        url_fclose(file);
    }
}

URL_FILE *url_fopen(const char *url, const char *operation)
{
    URL_FILE *file;

    file = calloc(1, sizeof(URL_FILE));
    if (!file) {
        fprintf(stderr, "url_fopen: URL_FILE memory allocation failure.");
        return NULL;
    }

    file->handle = curl_easy_init();

    curl_easy_setopt(file->handle, CURLOPT_URL, url);
    curl_easy_setopt(file->handle, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(file->handle, CURLOPT_TCP_KEEPALIVE, 1L);
    /* By default we don't want to download anything */
    curl_easy_setopt(file->handle, CURLOPT_NOBODY, 1L);

    for (const char *c = operation; *c; c++) {
        switch (*c) {
            case 'r':
                curl_easy_setopt(file->handle,
                                 CURLOPT_WRITEDATA, file);
                curl_easy_setopt(file->handle,
                                 CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(file->handle,
                                 CURLOPT_NOBODY, 0L);
                break;
            case 'h':
                curl_easy_setopt(file->handle,
                                 CURLOPT_HEADERDATA, file);
                curl_easy_setopt(file->handle,
                                 CURLOPT_HEADERFUNCTION, header_callback);
                break;
            default:
                fprintf(stderr, "url_fopen: invalid operation %c", *c);
                break;
        }
    }

    if (!multi_handle) {
        multi_handle = curl_multi_init();
    }

    curl_multi_add_handle(multi_handle, file->handle);

    return file;
}

CURLMcode url_fclose(URL_FILE *file)
{
    /* make sure the easy handle is not in the multi handle anymore */
    CURLMcode ret = curl_multi_remove_handle(multi_handle, file->handle);

    /* cleanup */
    curl_easy_cleanup(file->handle);

    free(file->buffer);/* free any allocated buffer space */
    free(file->header);
    free(file);
    file = NULL;

    return ret;
}

int url_feof(URL_FILE *file)
{
    return (!file->buffer_pos) && (!file->still_running);
}

size_t url_fread(void *ptr, size_t size, size_t nmemb, URL_FILE *file)
{
    size_t want = nmemb * size;

    fill_buffer(file, want);

    /* check if there's data in the buffer - if not fill_buffer()
        * either errored or EOF */
    if(!file->buffer_pos)
        return 0;

    /* ensure only available data is considered */
    if(file->buffer_pos < want)
        want = file->buffer_pos;

    /* xfer data to caller */
    memcpy(ptr, file->buffer, want);

    use_buffer(file, want);

    want = want / size;     /* number of items */

    return want;
}

char *url_fgets(char *ptr, size_t size, URL_FILE *file)
{
    size_t want = size - 1;/* always need to leave room for zero termination */
    size_t loop;

    fill_buffer(file, want);

    /* check if there's data in the buffer - if not fill either errored or
        * EOF */
    if(!file->buffer_pos)
        return NULL;

    /* ensure only available data is considered */
    if(file->buffer_pos < want)
        want = file->buffer_pos;

    /*buffer contains data */
    /* look for newline or eof */
    for(loop = 0; loop < want; loop++) {
        if(file->buffer[loop] == '\n') {
        want = loop + 1;/* include newline */
        break;
        }
    }

    /* xfer data to caller */
    memcpy(ptr, file->buffer, want);
    ptr[want] = 0;/* always null terminate */

    use_buffer(file, want);

    return ptr;/*success */
}

void url_rewind(URL_FILE *file)
{
    /* halt transaction */
    curl_multi_remove_handle(multi_handle, file->handle);

    /* restart */
    curl_multi_add_handle(multi_handle, file->handle);

    /* ditch buffer - write will recreate - resets stream pos*/
    free(file->buffer);
    file->buffer = NULL;
    file->buffer_pos = 0;
    file->buffer_len = 0;

    free(file->header);
    file->header = NULL;
    file->header_len = 0;
    file->header_pos = 0;
}

