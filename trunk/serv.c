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
    char           *buffer = NULL;
    char           *hostname = NULL;
    struct addrinfo hints,
                   *server;
    int             total_byte_read = 0;
    char           *startOfRead = buffer;
    char           *ptr;
    int             content_length = 0;
    struct timeval  tv;
    fd_set          readfds;

    signal(SIGTERM, childSigHandler);

    check((buffer =
           malloc(BUFFER_SIZE)) != NULL,
          "Cannot allocate memory for buffer");

    for (;;) {
        startOfRead = buffer + total_byte_read;
        byte_count =
            readLine(sfd, buffer + total_byte_read, BUFFER_SIZE - 1);

        if (byte_count == -1) {
            _exit(1);
        }

        total_byte_read += byte_count;

        if (byte_count == 2) {
            break;
        }

    }

    if ((ptr = strnstr(buffer, "Content-Length: ",
                       total_byte_read)) != NULL) {
        content_length = atoi(extract_header(ptr, "Content-Length: "));
    }

    if ((ptr = strnstr(buffer, "Host: ", total_byte_read)) != NULL) {
        hostname = extract_header(ptr, "Host: ");
    }

    int             i;
    for (i = 0; i < content_length; i++) {
        recv(sfd, buffer + total_byte_read + i, 1, 0);
        total_byte_read++;
    }

    printf("Will now connect to %s\n", hostname);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, "http", &hints, &server) == 0) {
        printf("resolved\n");
    }

    int             serversocket =
        socket(server->ai_family, server->ai_socktype,
               server->ai_protocol);

    if (serversocket != -1) {
        printf("Created a new file descriptor\n");
    }

    if (connect(serversocket, server->ai_addr, server->ai_addrlen)
        == 0) {
        printf("Connected.\n");
    }

    send(serversocket, buffer, total_byte_read, 0);


    FD_ZERO(&readfds);
    FD_SET(serversocket, &readfds);

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    select(serversocket + 1, &readfds, NULL, NULL, &tv);

    int             chunk = 0;

    total_byte_read = 0;
    content_length = 0;
    memset(buffer, 0, total_byte_read);
    if (FD_ISSET(serversocket, &readfds)) {
        for (;;) {
            startOfRead = buffer + total_byte_read;
            byte_count =
                readLine(serversocket, buffer + total_byte_read,
                         BUFFER_SIZE);
            if (byte_count == -1)
                _exit(1);

            total_byte_read += byte_count;

            if (byte_count == 2)
                break;

       }
    } else {
        printf("timeout\n");
        close(serversocket);
        close(sfd);
        _exit(1);
    }

    if ((ptr = strnstr(buffer, "Content-Length: ", total_byte_read)) != NULL) {
            content_length = atoi(extract_header(ptr, "Content-Length: "));
    }

    if (strnstr(buffer, "Transfer-Encoding: chunked\r\n", total_byte_read) !=
                    NULL) {
            printf("***************chunked*********\n");
            chunk = 1;
    }

    printf("%s", buffer);

    FD_ZERO(&readfds);
    FD_SET(serversocket, &readfds);
    if (content_length != 0) {
        tv.tv_sec = 3;
        select(serversocket + 1, &readfds, NULL, NULL, &tv);
        if (FD_ISSET(serversocket, &readfds)) {
            for (i = 0; i < content_length; i++) {
                recv(serversocket, buffer + total_byte_read, 1, 0);
                total_byte_read++;
            }
        } else {
            printf("timeout\n");
        }
    }

    if (chunk) {
        for (;;) {
            startOfRead = buffer + total_byte_read;
            byte_count =
                readLine(serversocket, buffer + total_byte_read,
                         BUFFER_SIZE);
            if (byte_count == -1)
                _exit(1);

            total_byte_read += byte_count;

            if (byte_count == 2 && startOfRead[0] == '\r'
                && startOfRead[1] == '\n') {
                printf("break\n");
                break;
            }
        }
    }


    printf("About to send %d bytes of data.\n", total_byte_read);

    fd_set          writefds;
    FD_ZERO(&writefds);
    FD_SET(sfd, &writefds);
    select(sfd + 1, NULL, &writefds, NULL, NULL);
    int             byte_sent;
    printf("About to send %d bytes of data.\n", total_byte_read);
    byte_sent = send(sfd, buffer, total_byte_read, 0);
    printf("%d bytes have been sent.", byte_sent);
    close(sfd);
    _exit(0);

  error:
    close(sfd);
    _exit(1);
}

int
main(int argc, char *argv[])
{
    int             sfd,
                    newfd;
    struct addrinfo hints,
                   *servinfo;
    struct sockaddr_in their_addr;
    socklen_t       addr_size;
    int             optval;

    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    check(getaddrinfo(NULL, "80", &hints, &servinfo) == 0,
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
        addr_size = sizeof(their_addr);
        // check((newfd = accept(sfd, (struct sockaddr *) &their_addr,
        // &addr_size)) != -1, "cannot accept");
        check((newfd = accept(sfd, NULL, NULL)) != -1, "cannot accept");

        switch (fork()) {
        case 0:
            proxy(newfd);
            break;
        default:
            close(newfd);
        }
        // printf("finish");
    }

    return EXIT_SUCCESS;

  error:
    return EXIT_FAILURE;
}
