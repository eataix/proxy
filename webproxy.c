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

#include <sys/stat.h>
#include <fcntl.h>              /* Defines O_* constants */
#include <sys/stat.h>           /* Defines mode constants */
#include <sys/mman.h>

#include "dbg.h"
#include "readline.h"
#include "config.h"
#include "utils.h"

#define BUFFER_SIZE (INT16_MAX)
#define USECOND_PER_SECOND (1000000)
#define BYTES_TO_KBYTES(A) (A / 1024)
#define KBYTES_TO_BYTES(A) (A * 1024)

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE
#endif

#define HOST_PREFIX        ("Host: ")
#define HOST_PREFIX_LENGTH (strlen(HOST_PREFIX))

#define CHUNKED_PREFIX     ("Transfer-Encoding: chunked")
#define CHUNKED_PREFIX_LENGTH (strlen(CHUNKED_PREFIX))

#define CONTENT_LENGTH_PREFIX ("Content-Length: ")
#define CONTENT_LENGTH_PREFIX_LENGTH (strlen(CONTENT_LENGTH_PREFIX))

struct peer {
    int             socketfd;
    char           *buffer;
    int             bytes_read;
    char           *hostname;
};

struct record {
    int             valid;
    char            hostname[20];
    struct addrinfo addr;
    struct sockaddr_storage sock;
};

struct config_sect *conf;

char           *addr;


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
         (char *) ptr - addr < 8804; ptr += (sizeof(*ptr))) {
        printf("Have not found matched\n");
        printf("the digit is %d\n", ptr->valid);
        if (ptr->valid == 0) {
            printf("Have not found matched\n");
            break;
        }
        if (strcasecmp(ptr->hostname, name) == 0) {
            sfd =
                socket(ptr->addr.ai_family, ptr->addr.ai_socktype,
                       ptr->addr.ai_protocol);
            if (sfd == -1)
                goto error;
            if (connect(sfd, ptr->addr.ai_addr, ptr->addr.ai_addrlen) != 0)
                goto error;
            printf("Reusing dns cache%s\n", name);
            return sfd;
        } else {
            printf("name is: %s length: %ld\n", ptr->addr.ai_canonname,
                   strlen(ptr->addr.ai_canonname));
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

        for (ptr = (struct record *) (addr + sizeof(int));
             (char *) ptr - addr < 8804; ptr += sizeof(*ptr)) {
            if (ptr->valid == 1) {
                continue;
            }
            ptr->valid = 1;
            strcpy(ptr->hostname, p->ai_canonname);
            memcpy(&(ptr->sock), p->ai_addr, sizeof(*(p->ai_addr)));
            memcpy(&(ptr->addr), p, sizeof(*(p)));
            ptr->addr.ai_addr = (struct sockaddr *) &(ptr->sock);
            ptr->addr.ai_canonname = ptr->hostname;
            break;
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
parsehostname(const char *raw_hostname, char *hostname, char *port)
{
    char           *newstr;
    char           *p;
    newstr = strdup(raw_hostname);

    if ((p = strtok(newstr, ":")) == NULL) {
        strcpy(hostname, newstr);
        strcpy(port, "http");
    } else {
        strcpy(hostname, p);
        if ((p = strtok(NULL, ":")) == NULL) {
            strcpy(port, "http");
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

    int             line_count;
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
    int             content_flag;

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
    line_count = 0;
    byte_count = 0;
    rate = -1;
    content_flag = 0;

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

        line_count++;

        if (line_count == 1) {
            int             i;
            printf("%ld is: ", (long) getpid());
            for (i = 0; i < byte_count; i++) {
                putchar(*(client->buffer + client->bytes_read + i));
            }

            byte_count =
                process(client->buffer + client->bytes_read, byte_count);
        }

        if (strncmp
            (client->buffer + client->bytes_read, HOST_PREFIX,
             HOST_PREFIX_LENGTH) == 0) {
            printf("host %s %s\n", serveri->hostname, hostname);
            FREEMEM(raw_hostname);
            raw_hostname =
                extract_header(client->buffer + client->bytes_read,
                               HOST_PREFIX);
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
        } else
            if (strncmp
                (client->buffer + client->bytes_read, CHUNKED_PREFIX,
                 CHUNKED_PREFIX_LENGTH) == 0) {
            content_flag = 1;
        } else
            if (strncmp
                (client->buffer + client->bytes_read,
                 CONTENT_LENGTH_PREFIX,
                 CONTENT_LENGTH_PREFIX_LENGTH) == 0) {
            content_flag = 1;
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
    while (p != NULL) {
        if (strcasecmp(p->name, "rates") == 0) {
            struct config_token *token = p->tokens;
            while (token != NULL) {
                if (endswith(serveri->hostname, token->token, 1) == 0) {
                    /*
                     * printf("a hit : %d\n", atoi(token->value)); 
                     */
                    rate = atoi(token->value);

                } else {
                    /*
                     * printf("%s != %s\n", serveri->hostname,
                     * token->token);
                     */
                }
                token = token->next;
            }
        }
        p = p->next;
    }

    FD_ZERO(&master);
    FD_SET(client->socketfd, &master);
    fdmax = client->socketfd;

    if (content_flag == 0) {
        printf("No content skip to response.\n");
        goto read_response;
    }

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    for (;;) {
        read_fds = master;

        check(select(fdmax + 1, &read_fds, NULL, NULL, &tv) != -1,
              "Cannot select.");

        if (FD_ISSET(client->socketfd, &read_fds)) {
            byte_count = recv(client->socketfd, client->buffer, 1024, 0);
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

  read_response:
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
            line_count = 0;
            goto start;
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
main()
{
    int             sfd = -1,
        newfd = -1;
    struct addrinfo hints,
                   *servinfo = NULL,
        *p = NULL;
    int             optval;
    int             fd;

    int             size =
        50 * (sizeof(struct addrinfo) + sizeof(struct sockaddr_storage)) +
        4;

    fd = shm_open("dnscache", O_CREAT | O_EXCL | O_RDWR,
                  S_IRUSR | S_IWUSR);

    check(fd != -1, "Cannot create shared memory.");

    printf("fd: %ld\n", (long) fd);

    check(ftruncate(fd, size) != -1, "Cannot resize the object");

    addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    check(addr != MAP_FAILED, "Cannot map?!");

    memset(addr, 0, size);

    // memcpy(addr, str, strlen(str));

    setbuf(stdout, NULL);

    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    conf = config_load("example.conf");
    config_dump(conf);


    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    check(getaddrinfo(NULL, "8080", &hints, &servinfo) == 0,
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
