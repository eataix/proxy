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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include "dbg.h"
#include "http.h"
#include "utils.h"

int
process_request_line(char *hostname, char *port, char *buffer,
                     const int count)
{
    char           *p,
                   *b,
                   *hn,
                   *pp;
    p = buffer;
    hn = hostname;
    pp = port;

    while (*p++ != ' ');

    check(isalnum(*p) || *p == '*', "Invalid format");

    b = p;

    /*
     * RFC 2616 Section 3.2.2
     * Comparisons of scheme names MUST be case-insensitive;
     */
    if (strncasecmp(p, HTTP_SCHEME_PREFIX, strlen(HTTP_SCHEME_PREFIX))
        != 0) {
        log_info("Not absolute url. %c", *p);
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

    if (*p == ' ') {
        *b = '/';
        b++;
    }

    do {
        *b++ = *p++;
    } while (*(b - 1) != '\n' && *(b - 2) != '\r');

    return b - buffer;
  error:
    return -1;
}

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
