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
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "readline.h"

        ssize_t
readLine(int sfd, void *buffer, size_t n)
{
        ssize_t         numRead;
        size_t          totRead;
        char           *buf;
        char            ch;

        struct timeval  tv;
        fd_set          readfds;

        tv.tv_sec = 0;
        tv.tv_usec = 50000000;

        if (n <= 0 || buffer == NULL) {
                errno = EINVAL;
                return -1;
        }

        buf = buffer;

        totRead = 0;

        //FD_ZERO(&readfds);
        //FD_SET(sfd, &readfds);
        //select(sfd + 1, &readfds, NULL, NULL, &tv);

        //if (FD_ISSET(sfd, &readfds)) {
                for (;;) {
                        numRead = recv(sfd, &ch, 1, 0);
                        // write(1, &ch, 1);

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
                                if (totRead < n - 1) {
                                        totRead++;
                                        *buf++ = ch;
                                }
                                if (ch == '\n')
                                        break;
                        }
                }
                *buf = '\0';
                return totRead;
        //} else {
        //        return -1;
        //}
}
