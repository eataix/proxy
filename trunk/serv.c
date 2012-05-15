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
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>

#include "dbg.h"
#include "readline.h"

#define BUFFER_SIZE INT16_MAX

void
sigHandler(int sig)
{
    if (sig == SIGINT) {
        printf("Catch interrupt.\n");
        kill(0, SIGTERM);
    } else if (sig == SIGTERM) {
        sleep(2);
        printf("Catch termination.\n");
        exit(EXIT_SUCCESS);
    }
}

void
childSigHandler(int sig)
{
    if (sig == SIGTERM) {
        printf("%d is going to terminate\n", getpid());
        _exit(0);
    }
}

void
copybytes(char *s1, const char *s2, const int num)
{
    char           *p1 = s1;
    const char     *p2 = s2;
    while (*p2 != '\0') {
        memcpy(s1, s2, 1);
    }
    return;
}
int
main(int argc, char *argv[])
{
    int             sfd,
                    newfd,
                    newfd2;
    struct addrinfo hints,
                   *servinfo;
    struct sockaddr_in their_addr;
    socklen_t       addr_size;
    char           *buffer;
    int             byte_count;
    char            reply[100];
    int             index = 0;
    char           *host = NULL;
    int             optval;
    int             optlen;
    char           *optval2;

    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    check(getaddrinfo(NULL, "3310", &hints, &servinfo) == 0,
          "cannot getaddrinfo");

    sfd =
        socket(servinfo->ai_family, servinfo->ai_socktype,
               servinfo->ai_protocol);

    optval = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    check(bind(sfd, servinfo->ai_addr, servinfo->ai_addrlen) != -1,
          "cannot bind.");

    check(listen(sfd, 10) != -1, "Cannot listen");


    host = malloc(100);

    while (1) {
        memset(host, 0, strlen(host));
        addr_size = sizeof(their_addr);
        check((newfd =
               accept(sfd, (struct sockaddr *) &their_addr,
                      &addr_size)) != -1, "cannot accept");

        if (fork() == 0) {
            signal(SIGTERM, childSigHandler);
            buffer = malloc(BUFFER_SIZE);
            byte_count = recv(newfd, buffer, BUFFER_SIZE - 1, 0);
            buffer[byte_count] = '\0';

            printf("%s", buffer);

            if ((host = strcasestr(buffer, "Host: ")) == NULL) {
                return EXIT_FAILURE;
            }
            // printf("Host is: %d", (host - buffer));

            int             length = strcspn(host, "\r\n") - 6;
            char           *hostname = malloc(length + 1);
            *hostname = '\0';
            strncat(hostname, host + 6, length);

            printf("Will now connect to %s\n", hostname);

            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;


            struct addrinfo *server;
            if (getaddrinfo(hostname, "http", &hints, &server) == 0) {
                // printf("resolved\n");
            }

            int             serversocket =
                socket(server->ai_family, server->ai_socktype,
                       server->ai_protocol);

            if (serversocket != -1) {
                // printf("Created a new file descriptor\n");
            }

            if (connect(serversocket, server->ai_addr, server->ai_addrlen)
                == 0) {
                // printf("Connected.\n");
            }

            send(serversocket, buffer, byte_count, 0);

            struct timeval  tv;
            fd_set          readfds;

            tv.tv_sec = 1;
            tv.tv_usec = 0;
            int             total_bytes = 0;

            while (1) {
                FD_ZERO(&readfds);
                FD_SET(serversocket, &readfds);
                select(serversocket + 1, &readfds, NULL, NULL, &tv);

                if (FD_ISSET(serversocket, &readfds)) {
                    byte_count =
                        recv(serversocket, buffer + total_bytes,
                             BUFFER_SIZE, 0);
                    if (byte_count == 0)
                        break;
                    else if (byte_count == -1)
                        break;
                    total_bytes += byte_count;
                } else {
                    break;
                }
            }

            // printf("About to send.\n");
            send(newfd, buffer, total_bytes, 0);
            close(newfd);
            _exit(0);
        }
        close(newfd);
        // printf("finish");
    }

    return EXIT_SUCCESS;

  error:
    return EXIT_FAILURE;
}
