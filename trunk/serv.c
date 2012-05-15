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
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#include "dbg.h"
#include "readline.h"

int
main(int argc, char *argv[])
{
    int             sfd, newfd, newfd2;
    struct addrinfo hints,
                   *servinfo;
    struct sockaddr_in their_addr;
    socklen_t       addr_size;
    char            buffer[100];
    int             byte_count;
    char            reply[100];
    char            *header[20];
    int index = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    check(getaddrinfo(NULL, "3310", &hints, &servinfo) == 0,
          "cannot getaddrinfo");

    sfd =
        socket(servinfo->ai_family, servinfo->ai_socktype,
               servinfo->ai_protocol);

    check(bind(sfd, servinfo->ai_addr, servinfo->ai_addrlen) != -1,
          "cannot bind.");

    check(listen(sfd, 10) != -1, "Cannot listen");


connect:
    addr_size = sizeof(their_addr);
    check((newfd = accept(sfd, (struct sockaddr *)&their_addr, &addr_size)) != -1, "cannot accept");

    while ((byte_count = readLine(newfd, reply, 100)) != -1) {
            if (byte_count == 2 && reply[0] == '\r' && reply[1] == '\n')
                    break;
            if (strncmp(reply, "Host: ", 4) == 0) {
                    write(1, reply, byte_count);
            }

    }
    goto connect;

    return EXIT_SUCCESS;

  error:
    return EXIT_FAILURE;
}
