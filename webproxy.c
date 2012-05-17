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
#include <ctype.h>
#include <time.h>

#include "dbg.h"
#include "readline.h"
#include "config.h"
#include "utils.h"

#define BUFFER_SIZE INT32_MAX
// #define RATE_LIMIT 2000
// #define FACTOR (USECOND_PER_SECOND / RATE_LIMIT)
#define USECOND_PER_SECOND 1000000
#define BYTES_TO_KBYTES(A) (A / 1024)
#define KBYTES_TO_BYTES(A) (A * 1024)
#define _POSIX_C_SOURCE

struct peer {
    int             socketfd;
    int             flag;
    char           *buffer;
    int             bytes_read;
    char           *hostname;
};

struct config_sect *conf;

void
sigHandler(int sig)
{
    if (sig == SIGINT) {
        printf("Catch interrupt.\n");
        kill(0, SIGTERM);
    } else if (sig == SIGTERM) {
        printf("Catch termination.\n");
        sleep(2);
        config_destroy(conf);
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

int
process(char *str, const int str_len)
{
    const char     *prefix = "http://";
    int             length = strlen(prefix);

    char           *p = malloc(str_len + 1);
    memset(p, 0, sizeof(*p));
    memcpy(p, str, str_len);


    int             i,
                    j;
    for (i = 0, j = 0; i < str_len && j < str_len; i++, j++) {
        if (strncasecmp(p + j, prefix, length - 1) == 0) {
            for (j += length;; j++) {
                if (p[j] == '/') {
                    break;
                }
                if (p[j] == ' ') {
                    str[i++] = '/';
                    break;
                }
            }
        }
        str[i] = p[j];
    }

    return i;
}

void
proxy(int sfd)
{
    int             byte_count = 0;
    char           *hostname = NULL;

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
    int             line_count = 0;

    client->socketfd = sfd;
    // fdmax = sfd;


    for (;;) {
        byte_count =
            readLine(client->socketfd, client->buffer + client->bytes_read,
                     500);
        line_count++;

        if (line_count == 1) {
            byte_count =
                process(client->buffer + client->bytes_read, byte_count);
        }

        if (strncmp(client->buffer + client->bytes_read, "Host: ", 6) == 0) {
            printf("host");
            hostname =
                extract_header(client->buffer + client->bytes_read,
                               "Host: ");
            serveri->socketfd = make_socket(hostname, "http");
            serveri->hostname = hostname;
            // fcntl(sfd, F_SETFL, O_NONBLOCK);
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


    struct timeval  tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;

    client->bytes_read = 0;

    FD_ZERO(&master);
    FD_SET(sfd, &master);
    fdmax = client->socketfd;
    for (;;) {
        read_fds = master;

        if (select(fdmax + 1, &read_fds, NULL, NULL, &tv) == -1) {
            printf("cannot select \n");
            _exit(1);
        }

        if (FD_ISSET(client->socketfd, &read_fds)) {
            // printf("read line from client\n");
            byte_count =
                recv(client->socketfd, client->buffer + client->bytes_read,
                     1024, 0);

            if (byte_count == -1) {
                _exit(1);
            }

            client->bytes_read += byte_count;

            byte_count =
                send(serveri->socketfd, client->buffer, client->bytes_read,
                     0);

            if (byte_count == 0) {
                _exit(1);
            }

            client->bytes_read = 0;

            continue;
        }

        if (client->bytes_read != 0)
            send(serveri->socketfd, client->buffer, client->bytes_read, 0);
        break;

        // printf("select\n");
    }

    int             rate = -1;

    while (conf != NULL) {
        if (strcasecmp(conf->name, "rates") == 0) {
            struct config_token *token = conf->tokens;
            while (token != NULL) {
                if (endswith(serveri->hostname, token->token, 1) == 0) {
                    printf("a hit : %d\n", atoi(token->value));
                    rate = atoi(token->value);

                } else {
                    printf("%s != %s\n", serveri->hostname, token->token);
                }
                token = token->next;
            }
        }
        conf = conf->next;
    }


    tv.tv_sec = 10;
    tv.tv_usec = 0;

    serveri->bytes_read = 0;

    FD_ZERO(&master);
    FD_SET(serveri->socketfd, &master);
    fdmax = serveri->socketfd;

    struct timeval  prev;
    time_t          second;
    suseconds_t     usecond;
    int             count;


    struct timespec sleeptime;
    sleeptime.tv_sec = 0;
    sleeptime.tv_nsec = 0;

    int current_rate;

    for (;;) {
        gettimeofday(&prev, NULL);

        read_fds = master;

        if (select(fdmax + 1, &read_fds, NULL, NULL, &tv) == -1) {
            printf("cannot select \n");
            _exit(1);
        }

        int             factor = USECOND_PER_SECOND / rate;


        if (serveri->socketfd != -1
            && FD_ISSET(serveri->socketfd, &read_fds)) {
            tv.tv_sec = 2;

            if (rate == -1) {
                byte_count = recv(serveri->socketfd,
                                  serveri->buffer + serveri->bytes_read,
                                  BUFFER_SIZE, 0);
            } else {
                byte_count = recv(serveri->socketfd,
                                  serveri->buffer + serveri->bytes_read,
                                  KBYTES_TO_BYTES(rate), 0);
            }

            // printf("read: %d bytes", byte_count);

            if (byte_count == -1) {
                _exit(1);
            }

            serveri->bytes_read += byte_count;

            byte_count =
                send(client->socketfd, serveri->buffer,
                     serveri->bytes_read, 0);

            if (byte_count <= 0) {
                _exit(1);
            }

            serveri->bytes_read = 0;
            second = prev.tv_sec;
            usecond = prev.tv_usec;
            gettimeofday(&prev, NULL);


            if (rate != -1) {
                count =
                    (useconds_t) (factor * BYTES_TO_KBYTES(byte_count)) -
                    ((useconds_t) (prev.tv_sec - second)) *
                    USECOND_PER_SECOND - (prev.tv_usec - usecond);
                sleeptime.tv_nsec = count * 1000;
                if (count >= 0)
                    nanosleep(&sleeptime, NULL);
            }

            continue;
        }

        if (serveri->bytes_read != 0)
            send(client->socketfd, serveri->buffer, serveri->bytes_read,
                 0);
        break;

    }

    printf("finish\n");
    free(serveri->buffer);
    free(client->buffer);
    close(serveri->socketfd);
    close(client->socketfd);
    _exit(0);

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
    setbuf(stdout, NULL);

    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    conf = config_load("example.conf");
    config_dump(conf);


    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    check(getaddrinfo(NULL, "8080", &hints, &servinfo) == 0,
          "cannot getaddrinfo");

    sfd =
        socket(servinfo->ai_family, servinfo->ai_socktype,
               servinfo->ai_protocol);

    optval = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    check(bind(sfd, servinfo->ai_addr, servinfo->ai_addrlen) != -1,
          "cannot bind.");

    check(listen(sfd, 100) != -1, "Cannot listen");

    while (1) {
        check((newfd = accept(sfd, NULL, NULL)) != -1, "cannot accept");

        switch (fork()) {
        case 0:
            // printf("fork");
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
