/*-
 * Copyright (c) 2012, Meitian Huang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution
 *
 * THIS SOFTWARE IS PROVIDED BY Meitian Huang AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AN ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/socket.h>
#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "http.h"
#include "utils.h"

#ifdef __OPENSSL_SUPPORT__
#include "common.h"
#include "server.h"
#endif

ssize_t
#ifdef __OPENSSL_SUPPORT__
readLine(BIO * io, void *buffer, size_t n)
#else
readLine(int sfd, void *buffer, size_t n)
#endif
{
    ssize_t         numRead;
    size_t          totRead;
    char           *buf;
    char            ch;

    if (n <= 0 || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    buf = buffer;

    totRead = 0;

    for (;;) {

#ifdef __OPENSSL_SUPPORT__
        numRead = BIO_read(io, &ch, 1);
#else
        numRead = recv(sfd, &ch, 1, 0);
#endif

        if (numRead == -1) {
            if (errno == EINTR)
                continue;
            else
                return -1;
        } else if (numRead == 0) {
            if (totRead == 0)
                return 0;
            else
                break;
        } else {
            if (totRead < n) {
                totRead++;
                *buf++ = ch;
            }
            if (ch == '\n')
                break;
        }
    }
    return totRead;
}

int
process_request_line(char *hostname, char *port, char *buffer,
                     const int count, const int use_abs)
{
    char           *p,
                   *b,
                   *hn,
                   *pp;
    p = buffer;
    hn = hostname;
    pp = port;

    while (*p++ != ' ');

    if (!isalnum(*p) && *p != '*')
        return -1;

    b = p;

    /*
     * RFC 2616 Section 3.2.2
     * Comparisons of scheme names MUST be case-insensitive;
     */
    if (strncasecmp(p, HTTP_SCHEME_PREFIX, strlen(HTTP_SCHEME_PREFIX))
        != 0) {
        return count;
    }

    p += strlen(HTTP_SCHEME_PREFIX);

    while (*p != '/' && *p != ' ') {
        if (*p == ':') {
            p++;
            while (*p != '/' && *p != ' ') {
                *pp++ = *p++;
            }
            break;
        }
        *hn++ = *p++;
    }

    if (hn == hostname) {
        return -1;
    } else {
        *hn = '\0';
    }

    if (pp == port) {
        strcpy(pp, "80");
    } else {
        *pp = '\0';
    }

    if (use_abs == 1)
        return count;

    if (*p == ' ') {
        *b = '/';
        b++;
    }

    do {
        *b++ = *p++;
    } while (*(b - 1) != '\n' && *(b - 2) != '\r');

    return b - buffer;
}

/*
 * This is a O(1) operation.
 */
BOOLEAN
endswith(const char *s1, const char *s2, const int caseinsensitive)
{
    int             s1_length;
    int             s2_length;
    int             offset;

    s1_length = strlen(s1);
    s2_length = strlen(s2);
    if (s1_length < s2_length)
        return FALSE;

    offset = s1_length - s2_length;

    if (caseinsensitive == 1) {
        if (strncasecmp(s1 + offset, s2, s2_length) == 0)
            return TRUE;
        else
            return FALSE;
    } else {
        if (strncmp(s1 + offset, s2, s2_length) == 0)
            return TRUE;
        else
            return FALSE;
    }
}


/*
 * Extracts the value of HTTP header.
 */
int
extract(char *hostname, char *port, const char *line)
{

    char           *h,
                   *p;

    const char     *ch;

    h = hostname;
    p = port;
    ch = line;

    /*
     * RFC 2616 Section 4.2
     *
     * Each header field consists of a name followed by a colon (":") and the
     * field value. Field names are case-insensitive. The field value MAY be
     * preceded by any amount of LWS, though a single SP is preferred.
     */

    while (*ch != ' ' && *ch != '\t')
        ch++;

    while (*ch == ' ' || *ch == '\t')
        ch++;

    while (*ch != ':' && *ch != '\r')
        *h++ = *ch++;
    *h = '\0';

    if (*ch == ':') {
        ch++;
        while (isdigit(*ch))
            *p++ = *ch++;
        *p = '\0';
    } else {
        strcpy(p, DEFAULT_PORT);
    }

    return 0;
}
