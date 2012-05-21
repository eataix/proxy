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
#include <assert.h>

#include <sys/stat.h>
#include <fcntl.h>              /* Defines O_* constants */
#include <sys/stat.h>           /* Defines mode constants */
#include <sys/mman.h>

#include "dbg.h"
#include "readline.h"
#include "config.h"
#include "utils.h"


/*
 * RFC 2616 3.2.2
 * "If the port is empty or not given, port 80 is assumed."
 */
#define DEFAULT_PORT ("80")
#define NUM_RECORD (2)

#define BUFFER_SIZE (INT16_MAX)
#define USECOND_PER_SECOND (1000000)
#define BYTES_TO_KBYTES(A) (A / 1024)
#define KBYTES_TO_BYTES(A) (A * 1024)

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE
#endif

#define HOST_PREFIX        ("Host:")
#define HOST_PREFIX_LENGTH (strlen(HOST_PREFIX))

#define HTTP_CONTINUE_MESSAGE        "HTTP/1.1 100 Continue\r\n\r\n"
#define HTTP_CONTINUE_MESSAGE_LENGTH strlen(HTTP_CONTINUE_MESSAGE)


struct peer {
    int             socketfd;
    char           *buffer;
    int             bytes_read;
    char           *hostname;
};

struct record {
    int             valid;
    char            hostname[30];
    struct addrinfo addr;
    struct sockaddr_storage sock;
    struct timeval  tv;
};

struct config_sect *conf;

char           *addr;
int             cache_size;


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
        shm_unlink("dnscache");
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
    struct record  *ptr;

    printf("%ld Prepare to connect to host: %s port:%s\n", (long) getpid(),
           name, port);

    {
        int            *i;
        i = (int *) addr;

        while (*i == 1) {
            usleep(USECOND_PER_SECOND / 2);
        }
    }

    for (ptr = (struct record *) (addr + sizeof(int));
         (char *) ptr - (char *) addr < cache_size; ptr++) {

        if (ptr->valid == 0)    // End of record.
            break;

        if (strcasecmp(ptr->hostname, name) == 0) {
            sfd =
                socket(ptr->addr.ai_family, ptr->addr.ai_socktype,
                       ptr->addr.ai_protocol);
            if (sfd == -1)
                goto error;
            if (connect(sfd, ptr->addr.ai_addr, ptr->addr.ai_addrlen) != 0)
                goto error;
            printf("Reusing dns cache%s\n", name);
            gettimeofday(&(ptr->tv), NULL);
            return sfd;
        } else {
            printf("cache has: %s\n", ptr->hostname);
        }
    }

    printf("Have not found matched\n");

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags |= AI_CANONNAME;

    check(getaddrinfo(name, port, &hints, &ai) == 0, "Cannot getaddrinfo");

    for (p = ai; p != NULL; p = p->ai_next) {
        sfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sfd == -1)
            continue;
        printf("created a file discriptor\n");
        if (connect(sfd, p->ai_addr, p->ai_addrlen) != 0)
            continue;

        printf("Connected to %s\n", p->ai_canonname);

        {
            int            *i;
            i = (int *) addr;
            *i = 1;
        }

        {
            int             hit;
            hit = 0;
            for (ptr = (struct record *) (addr + sizeof(int));
                 (char *) ptr - (char *) addr < cache_size; ptr++) {
                printf("loop %lu %lu\n", (char *) ptr - (char *) addr,
                       cache_size);
                if (ptr->valid == 1) {
                    continue;
                }
                hit = 1;
                ptr->valid = 1;
                strcpy(ptr->hostname, p->ai_canonname);
                memcpy(&(ptr->sock), p->ai_addr, sizeof(*(p->ai_addr)));
                memcpy(&(ptr->addr), p, sizeof(*p));
                ptr->addr.ai_addr = (struct sockaddr *) &(ptr->sock);
                ptr->addr.ai_canonname = ptr->hostname;
                gettimeofday(&(ptr->tv), NULL);
                break;
            }

            if (hit == 0) {
                struct record  *earlist =
                    (struct record *) (addr + sizeof(int));

                for (ptr = (struct record *) (addr + sizeof(int));
                     (char *) ptr - (char *) addr < cache_size; ptr++) {
                    if (ptr->tv.tv_sec < earlist->tv.tv_sec) {
                        earlist = ptr;
                    }
                }

                memset(earlist, 0, sizeof(*earlist));
                earlist->valid = 1;
                strcpy(earlist->hostname, p->ai_canonname);
                memcpy(&(earlist->sock), p->ai_addr,
                       sizeof(*(p->ai_addr)));
                memcpy(&(earlist->addr), p, sizeof(*(p)));
                earlist->addr.ai_addr =
                    (struct sockaddr *) &(earlist->sock);
                earlist->addr.ai_canonname = earlist->hostname;
            }
        }

        {
            int            *i;
            i = (int *) addr;
            *i = 0;
        }

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
extract_header(const char *line, const char *key, const int prefix_length)
{
    int             length;
    char           *header = NULL;
    const char     *ptr;

    assert(strncasecmp(line, key, prefix_length) == 0);

    /*
     * RFC 2616 Section 4.2
     *
     * Each header field consists of a name followed by a colon (":") and the
     * field value. Field names are case-insensitive. The field value MAY be
     * preceded by any amount of LWS, though a single SP is preferred.
     */

    /*
     * Skips LWS.
     */
    for (ptr = line + prefix_length; *ptr == ' ' || *ptr == '\t'; ptr++);

    length = strcspn(ptr, "\r\n");

    header = malloc(length + 1);
    check(header != NULL, "Cannot allocate memory to extra");

    *header = '\0';
    strncat(header, ptr, length);
    printf("key: %s, value: %s\n", key, header);

    return header;

  error:
    return NULL;
}

/*
 * Parses the Host field of HTTP request header.
 * e.g., "Host: example.com:8080" to hostname = example.com & port = 8080.
 *
 * If cannot find port, use "80".
 */
void
parsehostname(const char *raw_hostname, char *hostname, char *port)
{
    char           *newstr;
    char           *p;
    newstr = strdup(raw_hostname);

    if ((p = strtok(newstr, ":")) == NULL) {
        strcpy(hostname, newstr);
        strcpy(port, DEFAULT_PORT);
    } else {
        strcpy(hostname, p);
        if ((p = strtok(NULL, ":")) == NULL) {
            strcpy(port, DEFAULT_PORT);
        } else {
            strcpy(port, p);
        }
    }

    free(newstr);

    printf("host: %s port: %s\n", hostname, port);
}

void
proxy(int sfd)
{
    struct peer    *client = NULL;
    struct peer    *serveri = NULL;

    fd_set          master,
                    read_fds;
    int             fdmax;

    int             byte_count;

    char           *raw_hostname = NULL,
        *hostname = NULL,
        *port = NULL;

    struct timeval  tv;
    int             rate;
    int             factor;

    struct timeval  current_time;
    struct timespec ts;
    time_t          prev_second;
    suseconds_t     prev_usecond;
    int             sleep_time;
    int             server_flag;

    struct config_sect *p;

    signal(SIGTERM, childSigHandler);

    client = malloc(sizeof(*client));
    check(client != NULL, "Cannot allocate memory.");
    memset(client, 0, sizeof(*client));

    client->socketfd = sfd;
    client->buffer = malloc(BUFFER_SIZE);
    check(client->buffer != NULL, "Cannot allocate memory for buffer");
    client->bytes_read = 0;

    serveri = malloc(sizeof(*serveri));
    check(serveri != NULL, "Cannot allocate memory.");
    memset(serveri, 0, sizeof(*serveri));

    serveri->socketfd = -1;
    serveri->buffer = malloc(BUFFER_SIZE);
    check(serveri->buffer != NULL, "Cannot allocate memory for buffer");
    serveri->bytes_read = 0;

    hostname = malloc(200);
    check(hostname != NULL, "Cannot allocate memory.");
    memset(hostname, 0, sizeof(*hostname));

    port = malloc(20);
    check(port != NULL, "Cannot allocate memory.");
    memset(port, 0, sizeof(*port));

  start:
    byte_count = 0;
    rate = -1;
    server_flag = 0;

    FD_ZERO(&master);
    FD_SET(client->socketfd, &master);
    fdmax = client->socketfd;

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    for (;;) {
        read_fds = master;
        select(fdmax + 1, &read_fds, NULL, NULL, &tv);

        if (!FD_ISSET(client->socketfd, &read_fds)) {
            goto error;
        }

        byte_count =
            readLine(client->socketfd, client->buffer + client->bytes_read,
                     KBYTES_TO_BYTES(2));

        check(byte_count != -1, "Cannot read.")

            if (byte_count == 0)
            goto cleanup;

        /*
         * RFC 2616 Section 5.1.2
         *
         * The absoluteURI form is REQUIRED when the request is being made to a
         * proxy.
         *
         * To allow for transition to absoluteURIs in all requests in future
         * versions of HTTP, all HTTP/1.1 servers MUST accept the absoluteURI
         * form in requests, even though HTTP/1.1 clients will only generate
         * them in requests to proxies.
         *
         */
        if (strncasecmp
            (client->buffer + client->bytes_read, HOST_PREFIX,
             HOST_PREFIX_LENGTH) == 0) {
            printf("host %s %s\n", serveri->hostname, hostname);
            FREEMEM(raw_hostname);
            raw_hostname =
                extract_header(client->buffer + client->bytes_read,
                               HOST_PREFIX, HOST_PREFIX_LENGTH);
            check(raw_hostname != NULL, "The header is malformed");
            parsehostname(raw_hostname, hostname, port);
            printf("host: %s port: %s\n", hostname, port);
            printf("host %s %s\n", serveri->hostname, hostname);
            if (serveri->socketfd == -1
                || strcasecmp(serveri->hostname, hostname) != 0) {
                serveri->socketfd = make_socket(hostname, port);
                FREEMEM(serveri->hostname);
                serveri->hostname = strdup(hostname);
            } else {
                if (serveri->hostname != NULL)
                    printf("%ld is reusing: %s %s", (long) getpid(),
                           serveri->hostname, hostname);
            }
        }

        client->bytes_read += byte_count;

        if (byte_count == 2) {
            break;
        }

    }

    check((serveri->socketfd != -1), "Cannot connect to the real server.");
    check(send(serveri->socketfd, client->buffer, client->bytes_read, 0) ==
          client->bytes_read, "Failed to send.");
    client->bytes_read = 0;

    p = conf;
    int             largest = 0;
    while (p != NULL) {
        if (strcasecmp(p->name, "rates") == 0) {
            struct config_token *token = p->tokens;
            while (token != NULL) {
                if (endswith(serveri->hostname, token->token, 1) == 0
                    && strlen(token->token) > largest) {
                    largest = strlen(token->token);
                    rate = atoi(token->value);

                }
                token = token->next;
            }
        }
        p = p->next;
    }

    FD_ZERO(&master);
    FD_SET(serveri->socketfd, &master);
    FD_SET(client->socketfd, &master);
    fdmax =
        serveri->socketfd >
        client->socketfd ? serveri->socketfd : client->socketfd;

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    factor = USECOND_PER_SECOND / rate;
    memset(&ts, 0, sizeof(ts));

    for (;;) {
        gettimeofday(&current_time, NULL);

        read_fds = master;

        check(select(fdmax + 1, &read_fds, NULL, NULL, &tv) != -1,
              "Cannot select.");

        if (FD_ISSET(serveri->socketfd, &read_fds)) {

            tv.tv_sec = 2;

            if (rate == -1) {
                byte_count = recv(serveri->socketfd,
                                  serveri->buffer, KBYTES_TO_BYTES(10), 0);
            } else {
                byte_count = recv(serveri->socketfd,
                                  serveri->buffer,
                                  KBYTES_TO_BYTES(rate), 0);
            }

            check(byte_count != -1,
                  "Error when receiving data from the real server.");
            if (byte_count == 0)
                goto cleanup;

            if (byte_count == HTTP_CONTINUE_MESSAGE_LENGTH &&
                strncasecmp(serveri->buffer, HTTP_CONTINUE_MESSAGE,
                            HTTP_CONTINUE_MESSAGE_LENGTH) != 0) {
                server_flag = 1;
            }

            byte_count =
                send(client->socketfd, serveri->buffer, byte_count, 0);
            check(byte_count != -1,
                  "Error when sending data to the client.");
            if (byte_count == 0)
                goto cleanup;

            prev_second = current_time.tv_sec;
            prev_usecond = current_time.tv_usec;
            gettimeofday(&current_time, NULL);

            if (rate != -1) {
                sleep_time =
                    (useconds_t) (factor * BYTES_TO_KBYTES(byte_count)) -
                    ((useconds_t) (current_time.tv_sec - prev_second)) *
                    USECOND_PER_SECOND - (current_time.tv_usec -
                                          prev_usecond);
                if (sleep_time >= 0) {
                    ts.tv_nsec = sleep_time * 1000;
                    nanosleep(&ts, NULL);
                }
            }

            continue;
        } else if (FD_ISSET(client->socketfd, &read_fds)) {
            /*
             *
             * RFC 2616 Section 8.1.1
             * HTTP implementations SHOULD implement persistent
             * connections.
             *
             */
            if (server_flag == 1)
                goto start;

            byte_count =
                recv(client->socketfd, client->buffer, KBYTES_TO_BYTES(2),
                     0);
            check(byte_count != -1,
                  "Error when receiving data from the client.");
            if (byte_count == 0)
                goto cleanup;

            byte_count =
                send(serveri->socketfd, client->buffer, byte_count, 0);
            check(byte_count != -1,
                  "Error when sending data to the server.");
            if (byte_count == 0)
                goto cleanup;

            continue;
        } else {
            break;
        }
    }


  cleanup:
    printf("%ld finish\n", (long) getpid());
    CLOSEFD(client->socketfd);
    CLOSEFD(serveri->socketfd);
    FREEMEM(serveri->hostname);
    FREEMEM(client->buffer);
    FREEMEM(serveri->buffer);
    FREEMEM(client);
    FREEMEM(serveri);
    FREEMEM(raw_hostname);
    FREEMEM(hostname);
    FREEMEM(port);
    config_destroy(conf);
    _exit(EXIT_SUCCESS);

  error:
    CLOSEFD(client->socketfd);
    CLOSEFD(serveri->socketfd);
    FREEMEM(serveri->hostname);
    FREEMEM(client->buffer);
    FREEMEM(serveri->buffer);
    FREEMEM(client);
    FREEMEM(serveri);
    FREEMEM(raw_hostname);
    FREEMEM(hostname);
    FREEMEM(port);
    config_destroy(conf);
    _exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
    int             sfd = -1,
        newfd = -1;
    struct addrinfo hints,
                   *servinfo = NULL,
        *p = NULL;
    int             optval;
    int             fd = -1;
    char           *config_path = NULL;
    char           *listen_port;

    switch (argc) {
    case 1:
        break;
    case 3:
        if (strcmp(argv[1], "-f") == 0) {
            config_path = argv[2];
            break;
        }
    default:
        return EXIT_FAILURE;
    }

    conf = config_load(config_path);
    check(conf != NULL, "Cannot find configuration file.");
    config_dump(conf);

    cache_size = NUM_RECORD * sizeof(struct record) + sizeof(int);

    fd = shm_open("dnscache", O_CREAT | O_EXCL | O_RDWR,
                  S_IRUSR | S_IWUSR);

    check(fd != -1, "Cannot create shared memory.");

    printf("fd: %ld\n", (long) fd);

    check(ftruncate(fd, cache_size) != -1, "Cannot resize the object");

    addr =
        mmap(NULL, cache_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    check(addr != MAP_FAILED, "Cannot map?!");

    memset(addr, 0, cache_size);

    // memcpy(addr, str, strlen(str));

    setbuf(stdout, NULL);

    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    listen_port = config_get_value(conf, "default", "proxy_port", 1);
    if (listen_port == NULL)
        listen_port = "8080";
    check(getaddrinfo(NULL, listen_port, &hints, &servinfo) == 0,
          "cannot getaddrinfo");

    optval = 1;

    for (p = servinfo; p != NULL; p = p->ai_next) {
        sfd = socket(servinfo->ai_family, servinfo->ai_socktype,
                     servinfo->ai_protocol);
        if (sfd == -1) {
            continue;
        }

        if (setsockopt
            (sfd, SOL_SOCKET, SO_REUSEADDR, &optval,
             sizeof(optval)) == -1) {
            continue;
        }

        if (bind(sfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
            close(sfd);
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        goto error;
    }

    freeaddrinfo(servinfo);

    check(listen(sfd, 10) != -1, "Cannot listen");

    while (1) {
        check((newfd = accept(sfd, NULL, NULL)) != -1, "cannot accept");

        switch (fork()) {
        case 0:
            proxy(newfd);
            break;
        default:
            close(newfd);
            continue;
        }
    }

    return EXIT_SUCCESS;

  error:
    shm_unlink("dnscache");
    CLOSEFD(sfd);
    freeaddrinfo(servinfo);
    return EXIT_FAILURE;
}
