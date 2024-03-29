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
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT OWNER AND CONTRIBUTORS "AS IS"
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

#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>           /* Defines mode constants */
#include <arpa/inet.h>

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>              /* Defines O_* constants */
#include <netdb.h>
#include <semaphore.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "dbg.h"
#include "http.h"
#include "utils.h"

#ifdef __OPENSSL_SUPPORT__
#include "common.h"
#include "server.h"
#endif

/*
 * Feature testing macros
 * I want header files to expose only the definitions (constants, function
 * prototypes, and so on) that follow POSIX.1-2001.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112
#endif

/*
 * Default parameters
 */
#define RECORD_HOSTNAME_LENGTH  50

/*
 * Please do NOT change PEER_BUFFER_SIZE to a small value. If the
 * PEER_BUFFER_SIZE is too small, CPU will be interrupted too frequently and it
 * will do too many context switches, which can be expensive and lead to higher
 * CPU usage.
 */
#define PEER_BUFFER_SIZE INT16_MAX

#define SHM_NAME "dnscache_shm"
#define SEM_NAME "dnscache_sem"

/*
 * Units and units conversion.
 */
#define USECOND_PER_SECOND 1000000
#define BYTES_TO_KBYTES(A) (A / 1024)
#define KBYTES_TO_BYTES(A) (A * 1024)

struct peer {
    int             socketfd;   /* Socket file descriptor */
    char           *hostname;   /* Hostname of the peer, only set for
                                 * "server" */
    char           *buffer;     /* Buffer area for the incoming data */
    int             bytes_read; /* Number of bytes read or the amount of
                                 * data in buffer */
};

/*
 * DNS record
 */
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

int             debug_level = 0;
int             use_abs_url = 1;

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
    } else if (sig == SIGTERM) {
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
                /****************************** WARNING ***********************
                 * The child process uses _exit(2) to terminate. The resources
                 * may not be fully released. Its parent must use exit(2) to
                 * terminate in order to releases resources.
                 *************************************************************/
        config_destroy(conf);
        _exit(EXIT_FAILURE);
    }
}

void
#ifdef __OPENSSL_SUPPORT__
send_error(BIO * io, const int code)
#else
send_error(int sfd, const int code)
#endif
{
    char           *head;
    char           *tail = RESPONSE_HEADER_TAIL;

    switch (code) {
    case 503:
        head = RESPONSE_503_HEAD;
        break;
    case 414:
        head = RESPONSE_414_HEAD;
    case 400:
    default:
        head = RESPONSE_400_HEAD;
        break;
    }

#ifdef __OPENSSL_SUPPORT__
    BIO_puts(io, head);
    BIO_puts(io, tail);
#else
    send(sfd, head, strlen(head), 0);
    send(sfd, tail, strlen(tail), 0);
#endif
}

/*
 * REF: http://www.cse.yorku.ca/~oz/hash.html
 */
unsigned long
hash(const unsigned char *str)
{
    const unsigned char *p;
    unsigned long   hash = 5381;
    int             c;
    p = str;

    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;

    return hash % (cache_size / sizeof(struct record));
}

/*
 * Establishes a connection with the real server.
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

        /*
         * Use fork(2) to speed up. The parent will return the socket file
         * descriptor to the caller. The child will add the record in the
         * shared memory and exit.
         */
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

void
dnscleaner(void)
{
    int             ttl;
    char           *p;
    struct record  *r;
    struct timeval  tv;
    p = config_get_value(conf, "dns", "ttl", 1);

    signal(SIGTERM, childSigHandler);

    if (p == NULL)
        ttl = DEFAULT_TTL;
    else
        ttl = (int) strtol(p, (char **) NULL, 10);

    if (ttl < 60) {
        log_warn("TTL is too small. I will now set it to 60s.");
        ttl = 60;
    }

    for (;;) {
        sleep(ttl / 2);
        r = (struct record *) addr;
        gettimeofday(&tv, NULL);
        sem_wait(sem);
        while ((char *) r - addr < cache_size) {
            if (tv.tv_sec - r->tv.tv_sec > ttl) {
                memset(r, 0, sizeof(*r));
            }
            r++;
        }
        sem_post(sem);
    }
}

/*
 * Gets the rate specified in the configuration file.
 * Returns -1 if not found.
 * Returns the best (longest) matches if there are multiple matches.
 */
int
get_rate(const char *hostname)
{
    struct config_sect *p;
    struct config_token *token;
    size_t          best_match;
    int             rate;

    p = conf;
    best_match = 0;
    rate = -1;

    /*
     * The idea is, if the "hostname" matches "edu.au" and "anu.edu.au", the
     * later one will be used regardless of their order in the configuration
     * file.
     */
    while (p != NULL) {
        if (strcasecmp(p->name, "rates") == 0) {
            token = p->tokens;
            while (token != NULL) {
                if (endswith(hostname, token->token, 1) == TRUE
                    && strlen(token->token) > best_match) {
                    best_match = strlen(token->token);
                    rate = atoi(token->value);
                }
                token = token->next;
            }
        }
        p = p->next;
    }

    return rate;
}

void
#ifdef __OPENSSL_SUPPORT__
proxy(int sfd, SSL * ssl)
#else
proxy(int sfd)
#endif
{
    /*
     * Peers information
     */
    struct peer    *client = NULL;
    struct peer    *server = NULL;

    /*
     * Hostname and port from Host filed
     */
    char           *hostname = NULL;
    char           *port = NULL;

    /*
     * Hostname and port from Request-line.
     */
    char           *request_hostname = NULL;
    char           *request_port = NULL;

    /*
     * Variables for select()
     */
    struct timeval  tv;
    fd_set          master,
                    read_fds;
    int             fdmax;

    int             byte_count,
                    line_count;

    /*
     * Rate-limiting related variables
     */
    int             rate;
    int             factor;

    struct timeval  current_time;
    struct timespec ts;
    time_t          prev_second;
    suseconds_t     prev_usecond;
    int             sleep_time;

    /*
     * If the server sends actual response
     */
    int             content_flag;

    int             chunk_size;

#ifdef __OUT_OF_MIND__
    send_error(sfd, 400);
#endif

#ifdef __OPENSSL_SUPPORT__
    BIO            *io,
                   *ssl_bio;

    io = BIO_new(BIO_f_buffer());
    ssl_bio = BIO_new(BIO_f_ssl());
    BIO_set_ssl(ssl_bio, ssl, BIO_CLOSE);
    BIO_push(io, ssl_bio);
#endif

    signal(SIGTERM, childSigHandler);

    /*
     * Initialise variables
     */

    client = malloc(sizeof(*client));
    check_mem(client);
    memset(client, 0, sizeof(*client));

    client->socketfd = sfd;
    client->buffer = malloc(PEER_BUFFER_SIZE);
    check_mem(client->buffer);
    client->bytes_read = 0;

    server = malloc(sizeof(*server));
    check_mem(server);
    memset(server, 0, sizeof(*server));

    server->socketfd = -1;
    server->buffer = malloc(PEER_BUFFER_SIZE);
    check_mem(server->buffer);
    server->bytes_read = 0;

    server->hostname = malloc(HOSTNAME_LENGTH);
    check_mem(server->hostname);

    hostname = malloc(HOSTNAME_LENGTH);
    check_mem(hostname);

    port = malloc(PORT_LENGTH);
    check_mem(port);

    request_hostname = malloc(HOSTNAME_LENGTH);
    check_mem(request_hostname);

    request_port = malloc(PORT_LENGTH);
    check_mem(request_port);

  start:
    memset(hostname, 0, sizeof(*hostname));
    memset(port, 0, sizeof(*port));
    memset(request_hostname, 0, sizeof(*request_hostname));
    memset(request_port, 0, sizeof(*request_port));

    byte_count = 0;
    line_count = 0;
    content_flag = 0;

    FD_ZERO(&master);
    FD_SET(client->socketfd, &master);
    fdmax = client->socketfd;

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    for (;;) {
        read_fds = master;

        if (select(fdmax + 1, &read_fds, NULL, NULL, &tv) == -1) {
            log_err("select() fails");
#ifdef __OPENSSL_SUPPORT__
            send_error(io, 503);
#else
            send_error(client->socketfd, 503);
#endif
            goto error;
        }

        /*
         * Timeout
         */
        if (!FD_ISSET(client->socketfd, &read_fds)) {
#ifdef __OPENSSL_SUPPORT__
            if (!SSL_pending(ssl)) {
#endif
                log_info("timeout");
                goto error;
#ifdef __OPENSSL_SUPPORT__
            }
#endif
        }
#ifndef __OPENSSL_SUPPORT__
        byte_count = readLine(client->socketfd,
                              client->buffer + client->bytes_read,
                              KBYTES_TO_BYTES(5));
#else
        byte_count = readLine(io, client->buffer + client->bytes_read,
                              KBYTES_TO_BYTES(5));
#endif

        if (byte_count == -1) {
            log_warn("Failed to read from the client.");

#ifdef __OPENSSL_SUPPORT__
            send_error(io, 503);
#else
            send_error(client->socketfd, 503);
#endif
            goto error;
        }

        /*
         * Client closes the connection.
         */
        if (byte_count == 0)
            goto cleanup;

        /*
         * HTTP Request-Line
         */
        if (line_count == 0) {
            byte_count =
                process_request_line(request_hostname, request_port,
                                     client->buffer, byte_count,
                                     use_abs_url);
            log_info("host: %s, port: %s", request_hostname, request_port);

            if (byte_count == -1) {
                log_warn("The HTTP request line is malformed");
#ifdef __OPENSSL_SUPPORT__
                send_error(io, 400);
#else
                send_error(client->socketfd, 400);
#endif
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
            extract(hostname, port, client->buffer + client->bytes_read);
            /*
             * Check if the request line is seen
             */
            if (strlen(request_hostname) == 0
                || strlen(request_hostname) == 0) {
#ifdef __OPENSSL_SUPPORT__
                send_error(io, 400);
#else
                send_error(client->socketfd, 400);
#endif
                goto error;
            }
            /*
             * Consistence check
             */
            if (strcasecmp(request_hostname, hostname) != 0) {
                log_warn("Hostname is consistent");
#ifdef __OPENSSL_SUPPORT__
                send_error(io, 400);
#else
                send_error(client->socketfd, 400);
#endif
                goto error;
            }

            if (strcasecmp(request_port, port) != 0) {
                log_warn("Port is inconsistent");
#ifdef __OPENSSL_SUPPORT__
                send_error(io, 400);
#else
                send_error(client->socketfd, 400);
#endif
                goto error;
            }

            /*
             * Do we need a new socket?
             * We should, if:
             * a) We have not established a connection to any server, or
             * b) We have established a connection to a server whose hostname
             *    is different from this request.
             */
            if (server->socketfd == -1
                || strcasecmp(server->hostname, hostname) != 0) {
                /*
                 * Safely close existing socket file descriptor.
                 */
                CLOSEFD(server->socketfd);
                server->socketfd = make_socket(hostname, port);
                if (server->socketfd == -1) {
                    log_err("Cannot connect to %s", hostname);
#ifdef __OPENSSL_SUPPORT__
                    send_error(io, 503);
#else
                    send_error(client->socketfd, 503);
#endif
                    goto error;
                }
                rate = get_rate(hostname);
                memset(server->hostname, 0, sizeof(*(server->hostname)));
                strcpy(server->hostname, hostname);
            }
        }

        client->bytes_read += byte_count;

        if (client->bytes_read > PEER_BUFFER_SIZE / 2) {
#ifdef __OPENSSL_SUPPORT__
            send_error(io, 414);
#else
            send_error(client->socketfd, 414);
#endif
            goto error;
        }

        /*
         * I don't care if this line is actually "\r\n". I just need a way to
         * `break` this infinite loop.
         */
        if (byte_count == 2)
            break;
    }

    /*
     * Check for error.
     */
    if (server->socketfd == -1) {
        log_err("Cannot connect to the real server.");
#ifdef __OPENSSL_SUPPORT__
        send_error(io, 503);
#else
        send_error(client->socketfd, 503);
#endif
        goto error;
    }

    /*
     * Send the content in the buffer to the server.
     */
    if (send(server->socketfd, client->buffer, client->bytes_read, 0) !=
        client->bytes_read) {
        log_err("Failed to send.");
#ifdef __OPENSSL_SUPPORT__
        send_error(io, 503);
#else
        send_error(client->socketfd, 503);
#endif
        goto error;
    }

    /*
     * Reset the counts.
     */
    client->bytes_read = 0;
    server->bytes_read = 0;

    FD_ZERO(&master);
    FD_SET(server->socketfd, &master);

#ifdef __OPENSSL_SUPPORT__
    FD_SET(client->socketfd, &master);
    fdmax = max(server->socketfd, client->socketfd);
#else
    fdmax = server->socketfd;
#endif

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    if (rate != -1)
        factor = USECOND_PER_SECOND / rate;

    memset(&ts, 0, sizeof(ts));

    if (rate != -1)
        chunk_size = min(PEER_BUFFER_SIZE, KBYTES_TO_BYTES(rate));
    else
        chunk_size = KBYTES_TO_BYTES(10);

    for (;;) {
        /*
         * Start a timer.
         */
        gettimeofday(&current_time, NULL);

        read_fds = master;

        if (select(fdmax + 1, &read_fds, NULL, NULL, &tv) == -1) {
            log_warn("Cannot select.");
#ifdef __OPENSSL_SUPPORT__
            send_error(io, 503);
#else
            send_error(client->socketfd, 503);
#endif
        }

        if (FD_ISSET(server->socketfd, &read_fds)) {

            tv.tv_sec = 2;

            byte_count = recv(server->socketfd,
                              server->buffer, chunk_size, 0);

            if (byte_count == -1) {
                log_err("Error when receiving data from the real server.");
#ifdef __OPENSSL_SUPPORT__
                send_error(io, 503);
#else
                send_error(client->socketfd, 503);
#endif
                goto error;
            }

            if (byte_count == 0)
                goto cleanup;

            /*
             * If reads the "100 Continue" HTTP response message, allows the
             * client to write.
             */
            if (byte_count == HTTP_CONTINUE_MESSAGE_LENGTH &&
                strncasecmp(server->buffer, HTTP_CONTINUE_MESSAGE,
                            HTTP_CONTINUE_MESSAGE_LENGTH) == 0)
                content_flag = 0;
            else
                content_flag = 1;

#ifndef __OPENSSL_SUPPORT__
            byte_count =
                send(client->socketfd, server->buffer, byte_count, 0);
#else
            byte_count = BIO_write(io, server->buffer, byte_count);
            check(BIO_flush(io) >= 0, "Error flushing BIO");
#endif

            if (byte_count == -1) {
                log_err("Error when sending data to the client.");
#ifdef __OPENSSL_SUPPORT__
                send_error(io, 503);
#else
                send_error(client->socketfd, 503);
#endif
            }

            if (byte_count == 0)
                goto cleanup;

            /*
             * Get the elapsed time
             */
            prev_second = current_time.tv_sec;
            prev_usecond = current_time.tv_usec;

            gettimeofday(&current_time, NULL);

            /*
             * Use nanosleep(2) to tune the speed to `rate`.
             *
             * I build a calibration mechanism here.
             * 1. If current rate is under the rate the user wants to limit to,
             * do NOT sleep.
             * 2. Subtract the time spending on receiving from the server and
             * sending to the client from the calculated sleep time.
             */
            if (rate != -1) {
                sleep_time =
                    (useconds_t) (factor *
                                  BYTES_TO_KBYTES(byte_count)) -
                    ((useconds_t)
                     (current_time.tv_sec -
                      prev_second)) * USECOND_PER_SECOND -
                    (current_time.tv_usec - prev_usecond);
                /*
                 * If sleep_time is less than zero, the current speed is way
                 * below the rate we want to limit to. Do not sleep in this
                 * case.
                 */
                if (sleep_time > 0) {
                    ts.tv_nsec = sleep_time * 1000;
                    nanosleep(&ts, NULL);
                }
            }

            continue;

        } else if (FD_ISSET(client->socketfd, &read_fds)) {
#ifdef __OPENSSL_SUPPORT__
          read_client:
#endif
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

#ifdef __OPENSSL_SUPPORT__
            byte_count = BIO_read(io, client->buffer, KBYTES_TO_BYTES(10));
#else
            byte_count =
                recv(client->socketfd, client->buffer,
                     KBYTES_TO_BYTES(10), 0);
#endif

            if (byte_count == -1) {
                log_err("Error when receiving data from the client.");
#ifdef __OPENSSL_SUPPORT__
                send_error(io, 503);
#else
                send_error(client->socketfd, 503);
#endif
                goto error;
            }
            if (byte_count == 0)
                goto cleanup;

            byte_count =
                send(server->socketfd, client->buffer, byte_count, 0);

            if (byte_count == -1) {
                log_err("Error when sending data to the server.");
#ifdef __OPENSSL_SUPPORT__
                send_error(io, 503);
#else
                send_error(client->socketfd, 503);
#endif
                goto error;
            }

            continue;
        } else {                /* Timeout */
#ifdef __OPENSSL_SUPPORT__
            if (SSL_pending(ssl))
                goto read_client;
#endif
            break;
        }
    }

  cleanup:
#ifdef __OPENSSL_SUPPORT__
    SSL_shutdown(ssl);
    SSL_free(ssl);
#endif
    CLOSEFD(client->socketfd);
    CLOSEFD(server->socketfd);
    FREEMEM(server->hostname);
    FREEMEM(client->buffer);
    FREEMEM(server->buffer);
    FREEMEM(client);
    FREEMEM(server);
    FREEMEM(hostname);
    FREEMEM(port);
    FREEMEM(request_hostname);
    FREEMEM(request_port);
    config_destroy(conf);
    _exit(EXIT_SUCCESS);

  error:
#ifdef __OPENSSL_SUPPORT__
    SSL_shutdown(ssl);
    SSL_free(ssl);
#endif
    log_info("Child process %ld exiting.", (long) getpid());
    CLOSEFD(client->socketfd);
    CLOSEFD(server->socketfd);
    FREEMEM(server->hostname);
    FREEMEM(client->buffer);
    FREEMEM(server->buffer);
    FREEMEM(client);
    FREEMEM(server);
    FREEMEM(hostname);
    FREEMEM(port);
    FREEMEM(request_hostname);
    FREEMEM(request_port);
    config_destroy(conf);
    _exit(EXIT_FAILURE);
}

void
usage(int error)
{
    FILE           *stream;
    if (error == 1)
        stream = stderr;
    else
        stream = stdout;
    fprintf(stream,
            "NAME\n"
            "\twebproxy - A Rate-limiting HTTP Proxy and Filter\n"
            "\n"
            "SYNOPSIS\n"
            "\twebproxy -h\n"
            "\twebproxy [-f FILE]\n"
            "\n"
            "OPTIONS\n"
            "\t-h\tshow this message.\n"
            "\t-f FILE\tspecify the configuration file.\n");
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
    char           *ptr;

#ifdef __OPENSSL_SUPPORT__
    BIO            *sbio;
    SSL_CTX        *ctx;
    SSL            *ssl;

    ctx = initialize_ctx(KEYFILE, PASSWORD);
    load_dh_params(ctx, DHFILE);
#endif

    /*
     * Preventsthe transformation of children into zombies.
     */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    switch (argc) {
    case 1:
        log_info
            ("No configuration file specified, defaults values will be used.");
        break;
    case 2:
        if (strcmp(argv[1], "-h") == 0) {
            usage(0);
            return EXIT_SUCCESS;
        } else {
            usage(1);
            return EXIT_FAILURE;
        }
    case 3:
        if (strcmp(argv[1], "-f") == 0) {

            conf = config_load(argv[2]);
            if (conf == NULL) {
                log_warn("Cannot find configuration file.");
                return EXIT_FAILURE;
            }
            break;
        }
    default:
        usage(1);
        return EXIT_FAILURE;
    }

    ptr = config_get_value(conf, "dns", "records", 1);
    if (ptr == NULL)
        cache_size = NUM_RECORD * sizeof(struct record);
    else
        cache_size =
            (int) strtol(ptr, (char **) NULL, 10) * sizeof(struct record);

    fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    check(fd != -1, "Cannot create shared memory.");

    check(ftruncate(fd, cache_size) != -1, "Cannot resize the object");

    addr =
        mmap(NULL, cache_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    check(addr != MAP_FAILED, "Cannot map?!");

    sem =
        sem_open(SEM_NAME, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR,
                 1);
    check(sem != SEM_FAILED, "Cannot create semaphores.");
    memset(addr, 0, cache_size);

    setbuf(stdout, NULL);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;


    ptr = config_get_value(conf, "default", "debug", 1);
    if (ptr == NULL)
        debug_level = 0;
    else
        debug_level = (int) strtol(ptr, (char **) NULL, 10);

    ptr = config_get_value(conf, "default", "no_abs", 0);
    if (ptr != NULL)
        use_abs_url = 0;

    ptr = config_get_value(conf, "default", "proxy_port", 1);
    if (ptr == NULL)
        listen_port = "8080";
    else
        listen_port = ptr;

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

    log_info("The proxy is listening at port: %s", listen_port);

    freeaddrinfo(servinfo);

    check(p != NULL, "Failed to bind\n");

    check(listen(sfd, 10) != -1, "Cannot listen");

    switch (fork()) {
    case 0:
        dnscleaner();
        _exit(EXIT_SUCCESS);
    case -1:
        log_err("Cannot fork()");
        goto error;
    default:
        break;

    }

    while (1) {
        check((newfd = accept(sfd, NULL, NULL)) != -1, "cannot accept");

        switch (fork()) {
        case 0:

#ifdef __OPENSSL_SUPPORT__
            sbio = BIO_new_socket(newfd, BIO_NOCLOSE);
            ssl = SSL_new(ctx);
            SSL_set_bio(ssl, sbio, sbio);
            switch (SSL_accept(ssl)) {
            case 1:
                proxy(newfd, ssl);
                break;
            default:
                log_info("SSL handshake failed, "
                         "fall back to unencrypted connection");
                _exit(EXIT_FAILURE);
            }
#else
            proxy(newfd);
#endif
            break;
        case -1:
            goto error;
        default:
            close(newfd);
        }
    }

    return EXIT_SUCCESS;

  error:
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);
    CLOSEFD(sfd);
    freeaddrinfo(servinfo);
    return EXIT_FAILURE;
}
