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

#include <semaphore.h>

#include "dbg.h"
#include "readline.h"
#include "config.h"
#include "utils.h"

/*
 * RFC 2616 3.2.2
 * "If the port is empty or not given, port 80 is assumed."
 */
#define DEFAULT_PORT "80"

#define NUM_RECORD              100

#define RECORD_HOSTNAME_LENGTH  30

#define BUFFER_SIZE INT16_MAX
#define USECOND_PER_SECOND 1000000
#define BYTES_TO_KBYTES(A) (A / 1024)
#define KBYTES_TO_BYTES(A) (A * 1024)

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE
#endif

#define HOST_PREFIX        "Host:"
#define HOST_PREFIX_LENGTH strlen(HOST_PREFIX)

#define SHM_NAME "dnscache_shm"
#define SEM_NAME "dnscache_sem"

/*
 * HTTP/1.1 100 Continue is the ONLY response from the server that allows the
 * client to send the rest of the request.
 */
#define HTTP_CONTINUE_MESSAGE        "HTTP/1.1 100 Continue\r\n\r\n"
#define HTTP_CONTINUE_MESSAGE_LENGTH strlen(HTTP_CONTINUE_MESSAGE)

struct peer {
    int             socketfd;   /* Socket file descriptor */
    char           *hostname;   /* Hostname of the peer, only set for
                                 * "server" */
    char           *buffer;     /* Buffer area for the incoming data */
    int             bytes_read; /* Number of bytes read or the amount of
                                 * data in buffer */
};

struct record {
    char            valid;      /* 1 if valid, 0 if invalid */
    char            hostname[RECORD_HOSTNAME_LENGTH + 1];
    struct addrinfo addr;       /* Result of addrinfo. WARNING: certain
                                 * fields are invalid. Don't rely on this
                                 * too much. */
    struct sockaddr_storage sock;       /* Use sockaddr_storage to support
                                         * both IPv4 and IPv6 */
    struct timeval  tv;         /* When this record was used last time */
};

struct config_sect *conf = NULL;

/*
 * Posix Shared Memory
 */
char           *addr = NULL;
/*
 * Size of shared memory
 */
int             cache_size;
/*
 * POSIX Semaphores
 */
sem_t          *sem;

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
        log_info("Catch SIGINT");
        kill(0, SIGTERM);
    }

    if (sig == SIGTERM) {
        log_info("Catch SIGTERM");
        sleep(2);
        shm_unlink(SHM_NAME);
        sem_unlink(SEM_NAME);
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
        log_warn("Child process %ld is existing.", (long) getpid);
        /****************************** WARNING ****************************
         * The child process uses _exit(2) to terminate. The resources
         * may not be fully released. Its parent should use exit(2) to
         * terminate and releases the resources (e.g., file descriptors).
         ******************************************************************/
        _exit(0);
    }
}

int
send_error(int sfd, const int code)
{
    char           *tail = "Content-length: 0\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Server: '; DROP TABLE servertypes; --\r\n"
        "Connection: close\r\n\r\n";
    char           *head;
    switch (code) {
    case 503:
        head = "HTTP/1.1 503 SERVICE UNAVAILABLE\r\n";
        break;
    case 400:
    default:
        head = "HTTP/1.1 400 BAD REQUEST\r\n";
        break;
    }

    puts(head);
    if (sfd != -1) {
        send(sfd, head, strlen(head), 0);
        send(sfd, tail, strlen(tail), 0);
        return 0;
    } else {
        return -1;
    }
}

unsigned long
hash(const unsigned char *str)
{
    const unsigned char *p;
    unsigned long   hash = 5381;
    int             c;
    p = str;

    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;

    return hash % NUM_RECORD;
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
    pid_t           pid;


    log_info("Child process %ld is attempting to connect to "
             "host:%s, port: %s", (long) getpid(), name, port);

    ptr = (struct record *) addr + hash((unsigned char *) name);
    sem_wait(sem);
    if (ptr->valid != 0 && strcasecmp(ptr->hostname, name) == 0) {
        sfd =
            socket(ptr->addr.ai_family, ptr->addr.ai_socktype,
                   ptr->addr.ai_protocol);

        if (sfd == -1) {
            memset(ptr, 0, sizeof(*ptr));
            goto new_record;
        }

        if (connect(sfd, ptr->addr.ai_addr, ptr->addr.ai_addrlen) != 0) {
            memset(ptr, 0, sizeof(*ptr));
            goto new_record;
        }

        log_info("Reusing DNS record of host:%s", name);
        sem_post(sem);
        return sfd;
    }
    sem_post(sem);
    log_info("Did not find the cached record for %s", name);

  new_record:

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags |= AI_CANONNAME;

    check(getaddrinfo(name, port, &hints, &ai) == 0, "Cannot getaddrinfo");

    for (p = ai; p != NULL; p = p->ai_next) {
        sfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sfd == -1)
            continue;
        if (connect(sfd, p->ai_addr, p->ai_addrlen) != 0)
            continue;

        log_info("Connected to %s", p->ai_canonname);

        pid = fork();
        check(pid != -1, "Cannot fork");

        switch (pid) {
        case 0:
            sem_wait(sem);

            ptr = (struct record *) addr + hash((unsigned char *) name);

            memset(ptr, 0, sizeof(*ptr));
            ptr->valid = 1;
            strcpy(ptr->hostname, p->ai_canonname);
            memcpy(&(ptr->sock), p->ai_addr, sizeof(*(p->ai_addr)));
            memcpy(&(ptr->addr), p, sizeof(*p));
            ptr->addr.ai_addr = (struct sockaddr *) &(ptr->sock);
            ptr->addr.ai_canonname = ptr->hostname;

            gettimeofday(&(ptr->tv), NULL);

            sem_post(sem);
            _exit(EXIT_SUCCESS);
        default:
            freeaddrinfo(ai);
            return sfd;
        }
    }

  error:
    freeaddrinfo(ai);
    sem_post(sem);
    return -1;
}

/*
 * Extracts the value of HTTP header.
 * e.g., from "Host: example.com\r\n" to "example.com"
 */
int
extract(char *hostname, char *port, const char *line)
{

    char           *h = hostname,
        *p = port;

    const char     *ch = line;

    /*
     * RFC 2616 Section 4.2
     *
     * Each header field consists of a name followed by a colon (":") and the
     * field value. Field names are case-insensitive. The field value MAY be
     * preceded by any amount of LWS, though a single SP is preferred.
     */

    while (*ch != ' ' && *ch != '\t')
        ch++;

    while (*ch == ' ' || *ch == '\t')
        ch++;

    while (*ch != ':' && *ch != '\r')
        *h++ = *ch++;
    *h = '\0';

    if (*ch == ':') {
        ch++;
        while (isdigit(*ch))
            *p++ = *ch++;
        *p = '\0';
    } else {
        strcpy(p, DEFAULT_PORT);
    }

    return 0;
}

void
proxy(int sfd)
{
    struct peer    *client = NULL;
    struct peer    *serveri = NULL;

    fd_set          master,
                    read_fds;
    int             fdmax;

    int             byte_count,
                    line_count;

    char           *raw_hostname = NULL,
        *hostname = NULL,
        *port = NULL;

    char           *request_hostname = NULL,
        *request_port = NULL;

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
    check_mem(client);
    memset(client, 0, sizeof(*client));

    client->socketfd = sfd;
    client->buffer = malloc(BUFFER_SIZE);
    check_mem(client->buffer);
    client->bytes_read = 0;

    serveri = malloc(sizeof(*serveri));
    check_mem(serveri);
    memset(serveri, 0, sizeof(*serveri));

    serveri->socketfd = -1;
    serveri->buffer = malloc(BUFFER_SIZE);
    check_mem(serveri->buffer);
    serveri->bytes_read = 0;

    hostname = malloc(200);
    check_mem(hostname);

    port = malloc(20);
    check_mem(port);

    request_hostname = malloc(200);
    check_mem(request_hostname);

    request_port = malloc(200);
    check_mem(request_port);

  start:
    memset(hostname, 0, sizeof(*hostname));
    memset(port, 0, sizeof(*port));
    memset(request_hostname, 0, sizeof(*request_hostname));
    memset(request_port, 0, sizeof(*request_port));

    byte_count = 0;
    line_count = 0;
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
            readLine(client->socketfd,
                     client->buffer + client->bytes_read,
                     KBYTES_TO_BYTES(2));

        check(byte_count != -1, "Cannot read.");

        if (byte_count == 0)
            goto cleanup;

        if (line_count == 0) {
            byte_count =
                process_request_line(request_hostname, request_port,
                                     client->buffer +
                                     client->bytes_read, byte_count);
            log_info("host: %s, port: %s", request_hostname, request_port);
            if (byte_count == -1) {
                send_error(client->socketfd, 400);
                goto error;
            }
            line_count = 1;
        }

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
            memset(hostname, 0, sizeof(*hostname));
            memset(port, 0, sizeof(*port));
            extract(hostname, port, client->buffer + client->bytes_read);
            if (line_count == 1) {
                check(strcasecmp(request_hostname, hostname) == 0,
                      "The URL specified %s != %s", request_hostname,
                      hostname);
                check(strcasecmp(request_port, port) == 0,
                      "The URL specified %s != %s", request_port, port);
            }
            if (serveri->socketfd == -1
                || strcasecmp(serveri->hostname, hostname) != 0) {
                /*
                 * Safely close existing socket file descriptor.
                 */
                CLOSEFD(serveri->socketfd);
                serveri->socketfd = make_socket(hostname, port);
                if (serveri->socketfd == -1) {
                    log_err("Cannot connect to %s", hostname);
                    send_error(client->socketfd, 503);
                    goto error;
                }
                FREEMEM(serveri->hostname);
                serveri->hostname = strdup(hostname);
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
    size_t          largest = 0;
    while (p != NULL) {
        if (strcasecmp(p->name, "rates") == 0) {
            struct config_token *token = p->tokens;
            while (token != NULL) {
                if (endswith(serveri->hostname, token->token, 1)
                    == 0 && strlen(token->token) > largest) {
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
    fdmax = max(serveri->socketfd, client->socketfd);

    content_flag = 1;

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

            /*
             * If reads the "100 Continue" HTTP response message, allows the
             * client to write.
             */
            if (byte_count == HTTP_CONTINUE_MESSAGE_LENGTH &&
                strncasecmp(serveri->buffer, HTTP_CONTINUE_MESSAGE,
                            HTTP_CONTINUE_MESSAGE_LENGTH) == 0) {
                content_flag = 0;
            } else {
                content_flag = 1;
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
                    (useconds_t) (factor *
                                  BYTES_TO_KBYTES(byte_count)) -
                    ((useconds_t)
                     (current_time.tv_sec -
                      prev_second)) * USECOND_PER_SECOND -
                    (current_time.tv_usec - prev_usecond);
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
             * If the server has responded any HTTP response message other than
             * 100 Continue and the client has written data, data written
             * appears to belong to the next request/response exchange.
             *
             */
            if (content_flag == 1)
                goto start;

            byte_count =
                recv(client->socketfd, client->buffer,
                     KBYTES_TO_BYTES(2), 0);
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
    log_info("%ld finish", (long) getpid());
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
    char           *listen_port;


    switch (argc) {
    case 1:
        break;
    case 3:
        if (strcmp(argv[1], "-f") == 0) {

            conf = config_load(argv[2]);
            check(conf != NULL, "Cannot find configuration file.");
            config_dump(conf);

            break;
        }
    default:
        return EXIT_FAILURE;
    }

    cache_size = NUM_RECORD * sizeof(struct record);

    fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    check(fd != -1, "Cannot create shared memory.");

    check(ftruncate(fd, cache_size) != -1, "Cannot resize the object");

    addr =
        mmap(NULL, cache_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    check(addr != MAP_FAILED, "Cannot map?!");

    *((struct record **) addr) = NULL;

    sem =
        sem_open(SEM_NAME, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR,
                 1);
    check(sem != SEM_FAILED, "Cannot create semaphores.");
    memset(addr, 0, cache_size);

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

    check(p != NULL, "Failed to bind\n");

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
        }
    }

    return EXIT_SUCCESS;

  error:
    shm_unlink("dnscache");
    CLOSEFD(sfd);
    freeaddrinfo(servinfo);
    return EXIT_FAILURE;
}
