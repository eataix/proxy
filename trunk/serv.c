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

void copybytes(char *s1, const char *s2, const int num) {
        char *p1 = s1;
        const char *p2 = s2;
        while (*p2 != '\0') {
                memcpy(s1, s2, 1);
        }
        return;
}
        int
main(int argc, char *argv[])
{
        int             sfd, newfd, newfd2;
        struct addrinfo hints,
                        *servinfo;
        struct sockaddr_in their_addr;
        socklen_t       addr_size;
        char            *buffer;
        int             byte_count;
        char            reply[100];
        char            *header[20];
        int index = 0;
        char *host = NULL;
        int optval;
        int optlen;
        char *optval2;
        int length;

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

        optval = 1;
        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));


        int server = -1;
        struct addrinfo *serveraddrinfo;

        host = malloc(100);
connect:
        memset(host, 0, strlen(host));
        addr_size = sizeof(their_addr);
        check((newfd = accept(sfd, (struct sockaddr *)&their_addr, &addr_size)) != -1, "cannot accept");

        while ((byte_count = readLine(newfd, reply, 100)) != -1) {
                if (strstr(reply, "Host: ") != NULL) {
                        char *ptr = reply + strlen("Host: ");
                        while (*ptr != '\r') {
                                strncat(host, ptr, 1);
                                ptr++;
                        }
                        strncat(host, "\0", 1);
                        printf("%s %d\n", host, strlen(host));

                        printf("prepare to resolve");
                        memset(&hints, 0, sizeof(hints));
                        hints.ai_family = AF_INET;
                        hints.ai_socktype =  SOCK_STREAM;
                        if (getaddrinfo(host, "http", &hints, &serveraddrinfo) == 0){
                                printf("resolved");
                        }
                        server = socket(serveraddrinfo->ai_family, serveraddrinfo->ai_socktype,
                                        serveraddrinfo->ai_protocol);
                        if (server != -1) {
                                printf("created socket");
                        }
                        if (connect(server, serveraddrinfo->ai_addr, serveraddrinfo->ai_addrlen) == 0) {
                                printf("connected");
                                write(server, buffer, strlen(buffer));
                        }
                }
                if (server != -1) {
                        write(server, reply, strlen(reply));
                }
                buffer = strdup(reply);
                if (byte_count == 2) {
                        int count;
                        char ch;
                        while ((count = read(server, &ch, 1)) != 0 && count != -1) {
                                write(newfd, &ch, 1);
                        }
                        break;
                }
        }
        goto connect;

        return EXIT_SUCCESS;

error:
        return EXIT_FAILURE;
}
