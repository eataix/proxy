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
#include "utils.h"
#include <stdio.h>

#define HTTP_SCHEME_PREFIX "http://"

int
indexOf(const char *s1, const char *s2, const int num,
        const int caseinsensitive)
{
    int             s1_length;
    int             s2_length;
    int             i;

    s1_length = strlen(s1);
    s2_length = strlen(s2);
    if (s1_length < s2_length)
        return -1;

    for (i = 0; i <= s1_length - s2_length && i < num; i++) {
        if (caseinsensitive == 1) {
            if (strncasecmp(s1 + i, s2, s2_length) == 0)
                return i;
        } else {
            if (strncasecmp(s1 + i, s2, s2_length) == 0)
                return i;
        }
    }

    return -1;
}

int
process_request_line(char *hostname, char *port, char *buffer,
                     const int count)
{
    char           *p,
                   *b,
                   *hn,
                   *pp;

    for (p = buffer, b = buffer; *p != ' ' && b - buffer < count;
         p++, b++);

    if (b - buffer >= count)
        return -1;

    *b = *p;
    p++;
    b++;

    if (!isalnum(*p) && *p != '*') {
        printf("not *");
        return -1;
    }

    /*
     * RFC 2616 Section 3.2.2
     * Comparisons of scheme names MUST be case-insensitive;
     */
    if (strncasecmp(b, HTTP_SCHEME_PREFIX, strlen(HTTP_SCHEME_PREFIX)) !=
        0) {
        return -1;
    }

    p += strlen(HTTP_SCHEME_PREFIX);

    hn = hostname;
    pp = port;

    while (*p != '/' && *p != ' ') {
        if (*p == ':') {
            while (*p != '/' && *p != ' ') {
                *pp = *p;
                pp++;
                p++;
            }
            break;
        }
        *hn = *p;
        hn++;
        p++;
    }

    if (*p == ' ') {
        *b = '/';
        b++;
    }

    do {
        *b = *p;
        b++;
        p++;
    } while (*(b - 1) != '\n' && *(b - 2) != '\r');

    if (hn - hostname == 0) {
        return -1;
    }

    *hn = '\0';

    if (pp - port == 0) {
        strcpy(pp, "80");
    } else {
        *pp = '\0';
    }

    return b - buffer;
}

int
lastIndexOf(const char *s1, const char ch)
{
    const char     *p;
    int             s1_length;

    s1_length = strlen(s1);
    if (s1_length == 0)
        return -1;

    for (p = s1 + s1_length - 1;; p--) {
        if (ch == *p) {
            return p - s1;
        }
        if (p == s1) {
            break;
        }
    }
    return -1;
}

BOOLEAN
contains(const char *s1, const char *s2, int caseinsensitive)
{
    int             s1_length;
    int             s2_length;
    int             i;

    s1_length = strlen(s1);
    s2_length = strlen(s2);
    if (s1_length < s2_length)
        return FALSE;

    for (i = 0; i <= s1_length - s2_length; i++) {
        if (caseinsensitive == 1) {
            if (strncasecmp(s1 + i, s2, s2_length) == 0)
                return TRUE;
        } else {
            if (strncasecmp(s1 + i, s2, s2_length) == 0)
                return TRUE;
        }
    }

    return FALSE;
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

size_t
mystrlen(const char *str)
{
    const char     *p;
    p = str;
    while (*p != '\r') {
        if (*p == '\0') {
            return -1;
        }
        p++;
    }
    return p - str + 2;
}

char           *
removeAll(const char *s1, const char *s2, const int caseinsensitive)
{
    char           *newstr;
    int             s1_length;
    int             s2_length;
    int             i;
    int             j;

    newstr = malloc(mystrlen(s1) + strlen("\r\n"));
    if (newstr == NULL) {
        return NULL;
    }

    s1_length = strlen(s1);
    s2_length = strlen(s2);

    if (s1_length < s2_length)
        return NULL;

    for (i = 0, j = 0; i < s1_length; i++) {
        if (caseinsensitive == 1) {
            if (strncasecmp(s1 + i, s2, s2_length) == 0) {
                i += s2_length;
                continue;
            }
            memcpy(newstr + j, s1 + i, 1);
            j++;
        } else {
            if (strncasecmp(s1 + i, s2, s2_length) == 0) {
                i += s2_length;
                continue;
            }
            memcpy(newstr + j, s1 + i, 1);
            j++;
        }
    }

    return newstr;
}
