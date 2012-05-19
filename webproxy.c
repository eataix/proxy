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
#define USECOND_PER_SECOND 1000000
#define BYTES_TO_KBYTES(A) (A / 1024)
#define KBYTES_TO_BYTES(A) (A * 1024)
#define _POSIX_C_SOURCE

struct peer {
    int             socketfd;
    char           *buffer;
    int             bytes_read;
    char           *hostname;
};

struct config_sect *conf;


/*
 * Signal handler of the parent process.
 */
void
sigHandler(int sig)
{
    if (sig == SIGINT) {
        /*
         * If the user press "Ctrl-C", terminate all the child processes.
         */
        printf("Catch interrupt.\n");
        kill(0, SIGTERM);
    } else if (sig == SIGTERM) {
        printf("Catch termination.\n");
        sleep(2);
        config_destroy(conf);
        exit(EXIT_SUCCESS);
    }
}

/*
 * Signal handler of the child process.
 */
void
childSigHandler(int sig)
{
    if (sig == SIGTERM) {
        /*
         * Clean up.
         */
        printf("%d is going to terminate\n", getpid());
        /****************************** WARNING ****************************
         * The child process uses _exit(2) to terminate. The resources
         * may not be fully released. Its parent should use exit(2) to
         * terminate and releases the resources (e.g., file descriptors).
         ******************************************************************/
        _exit(0);
    }
}

/*
 * Establishes a connection with the really server.
 * Returns the socket file descriptor with the server.
 * Returns -1 on failure.
 */
int
make_socket(const char *name, const char *port)
{
    struct addrinfo hints,
                   *ai,
                   *p;
    int             sfd = -1;

    printf("Prepare to connect to host: %s port:%s\n", name, port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    check(getaddrinfo(name, port, &hints, &ai) == 0, "Cannot getaddrinfo");

    for (p = ai; p != NULL; p = p->ai_next) {
        sfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sfd == -1)
            continue;
        printf("created a file discriptor\n");
        if (connect(sfd, p->ai_addr, p->ai_addrlen) != 0)
            continue;
        printf("Connected\n");
        freeaddrinfo(ai);
        return sfd;
    }

  error:
    return -1;
}

/*
 * Extracts the value of HTTP header.
 * e.g., from "Host: example.com\r\n" to "example.com"
 */
char           *
extract_header(const char *line, const char *key)
{
    int             length;
    char           *header = NULL;
    int             prefix_length;

    prefix_length = strlen(key);
    length = strcspn(line, "\r\n") - prefix_length;

    header = malloc(length + 1);
    check(header != NULL, "Cannot allocate memory to extra");

    *header = '\0';
    strncat(header, line + prefix_length, length);

    return header;

  error:
    return NULL;
}

/*
 * Converts the absolute URI to relative URI according to RFC2616.
 * e.g., from "GET http://example.com HTTP/1.1"
 *         to "GET / HTTP/1.1"
 */
int
process(char *str, const int str_len)
{
    const char     *prefix = "http://";
    const int       prefix_length = strlen(prefix);

    char           *p = NULL;
    int             i,
                    j;

    p = malloc(str_len + 1);
    check(p != NULL, "Cannot allocate memory.");
    memset(p, 0, sizeof *p);
    memcpy(p, str, str_len);

    for (i = 0, j = 0; i < str_len && j < str_len; i++, j++) {
        /*
         * If find "http://", skip copying until found "/" or " " (space).
         */
        if (strncasecmp(p + j, prefix, prefix_length) == 0) {
            for (j += prefix_length;; j++) {
                if (p[j] == '/') {
                    break;
                }
                /*
                 * RFC2616 5.1.2. Request-URI
                 * ...if none is present in the original URI, it MUST be given as
                 * "/" (the server root).
                 */
                if (p[j] == ' ') {
                    str[i] = '/';
                    i++;
                    break;
                }
            }
        }
        /*
         * Resume copying
         */
        str[i] = p[j];
    }

    free(p);
    return i;

  error:
    free(p);
    return -1;
}

/*
 * Parses the Host field of HTTP request header.
 * e.g., "Host: example.com:8080" to hostname = example.com & port = 8080.
 */
void
parsehostname(const char *raw_hostname, char **hostname, char **port)
{
    char           *newstr;
    newstr = strdup(raw_hostname);

    char           *p;

    if ((p = strtok(newstr, ":")) == NULL) {
        *hostname = strdup(newstr);
        *port = "http";
    } else {
        *hostname = strdup(p);
        if ((p = strtok(NULL, ":")) == NULL) {
            *port = "http";
        } else {
            *port = strdup(p);
        }
    }

    printf("host: %s port: %s\n", *hostname, *port);
}

void
proxy(int sfd)
{
    int             byte_count = 0;
    char           *raw_hostname = NULL,
        *hostname = NULL,
        *port = NULL;

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
            printf("host\n");
            raw_hostname =
                extract_header(client->buffer + client->bytes_read,
                               "Host: ");
            parsehostname(raw_hostname, &hostname, &port);
            printf("host: %s port: %s\n", hostname, port);
            serveri->socketfd = make_socket(hostname, port);
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

    char           *p;
    for (p = client->buffer; (p - client->buffer) < client->bytes_read;
         p++)
        putchar(*p);


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
