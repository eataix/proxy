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
#include <fcntl.h>

#include "dbg.h"
#include "readline.h"

#define BUFFER_SIZE INT16_MAX

struct peer {
        int             socketfd;
        int             flag;
        char           *buffer;
        int             bytes_read;
};

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

        int
make_socket(const char *name, const char *port)
{
        struct addrinfo hints,
                        *ai,
                        *p;
        int             s;
        printf("prepare to connect to %s\n", name);

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (name == NULL) {
                hints.ai_flags = AI_PASSIVE;
        }

        if (getaddrinfo(name, port, &hints, &ai) != 0) {
                printf("Cannot getaddrinfo");
                _exit(1);
        }

        for (p = ai; p != NULL; p = p->ai_next) {
                s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (s < 0)
                        continue;
                printf("created a file discriptor\n");
                if (connect(s, p->ai_addr, p->ai_addrlen) != 0)
                        continue;
                printf("Connected\n");
                return s;
        }
        return -1;
}

        char           *
extract_header(const char *line, const char *key)
{
        int             length;
        char           *header = NULL;
        int             prefixLength;

        prefixLength = strlen(key);

        length = strcspn(line, "\r\n") - prefixLength;

        header = malloc(length + 1);
        check(header != NULL, "Cannot allocate memory to extra");
        *header = '\0';
        strncat(header, line + prefixLength, length);
        return header;

error:
        return NULL;
}

        void
proxy(int sfd)
{
        int             byte_count = 0;
        char           *hostname = NULL;
        char                ch;

        signal(SIGTERM, childSigHandler);

        struct peer    *client = malloc(sizeof(*client));
        struct peer    *serveri = malloc(sizeof(*serveri));

        memset(client, 0, sizeof(*client));
        memset(serveri, 0, sizeof(*serveri));

        serveri->socketfd = -1;

        check((client->buffer = malloc(BUFFER_SIZE)) != NULL,
                        "Cannot allocate memory for buffer");
        check((serveri->buffer = malloc(BUFFER_SIZE)) != NULL,
                        "Cannot allocate memory for buffer");

        fd_set          master,
                        read_fds;

        int             fdmax;

        FD_ZERO(&master);
        FD_ZERO(&read_fds);

        FD_SET(sfd, &master);
        client->socketfd = sfd;
        //fdmax = sfd;


        for (;;) {
                byte_count = readLine(client->socketfd, client->buffer + client->bytes_read,
                                500);
                if (strncmp(client->buffer + client->bytes_read, "Host: ", 6) ==0) {
                        printf("host");
                        hostname = extract_header(client->buffer + client->bytes_read, "Host: ");
                        serveri->socketfd = make_socket(hostname, "http");
                        fcntl(sfd, F_SETFL, O_NONBLOCK);
                        FD_SET(serveri->socketfd, &master);
                }
                client->bytes_read += byte_count;
                if (byte_count == 2) {
                        break;
                }
        }

        if (serveri->socketfd == -1) {
                _exit(EXIT_FAILURE);
        }

        send(serveri->socketfd, client->buffer, client->bytes_read, 0);

        fdmax =
                serveri->socketfd >
                client->socketfd ? serveri->socketfd : client->socketfd;

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        client->bytes_read = 0;
        serveri->bytes_read = 0;

        for (;;) {
                read_fds = master;
                if (select(fdmax + 1, &read_fds, NULL, NULL, &tv) == -1) {
                        printf("cannot select \n");
                        _exit(1);
                }

                if (FD_ISSET(client->socketfd, &read_fds)) {
                        //printf("read line from client\n");
                        byte_count =
                                recv(client->socketfd, client->buffer + client->bytes_read, 1, 0);

                        if (byte_count == -1) {
                                _exit(1);
                        }

                        client->bytes_read++;

                        byte_count = send(serveri->socketfd, client->buffer, client->bytes_read, 0);

                        if (byte_count == 0) {
                                _exit(1);
                        }

                        client->bytes_read = 0;

                        continue;
                }

                if (FD_ISSET(serveri->socketfd, &read_fds)) {
                        byte_count =
                                recv(serveri->socketfd, serveri->buffer + serveri->bytes_read, 1, 0);

                        if (byte_count == -1) {
                                _exit(1);
                        }

                        serveri->bytes_read++;

                        if (serveri->bytes_read < 5)
                                continue;

                        byte_count = send(client->socketfd, serveri->buffer, serveri->bytes_read, 0);

                        if (byte_count <= 0) {
                                _exit(1);
                        }

                        serveri->bytes_read = 0;

                        continue;
                }

                if (serveri->bytes_read != 0)
                        send(client->socketfd, serveri->buffer, serveri->bytes_read, 0);

                if (client->bytes_read != 0)
                        send(serveri->socketfd, client->buffer, client->bytes_read, 0);

                printf("finish\n");
                close(serveri->socketfd);
                close(client->socketfd);
                _exit(0);

                //printf("select\n");
        }

error:
        close(serveri->socketfd);
        close(client->socketfd);
        _exit(1);
}

        int
main()
{
        int             sfd,
                        newfd;
        struct addrinfo hints,
                        *servinfo;
        int             optval;
        //setbuf(stdout, NULL);

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

        while (1) {
                check((newfd = accept(sfd, NULL, NULL)) != -1, "cannot accept");

                switch (fork()) {
                        case 0:
                                //printf("fork");
                                proxy(newfd);
                                break;
                        default:
                                close(newfd);
                                continue;
                }
                // printf("finish");
        }

        return EXIT_SUCCESS;

error:
        return EXIT_FAILURE;
}
