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
#ifndef HTTP_H_
#define HTTP_H_

#define HTTP_SCHEME_PREFIX "http://"

/*
 * RFC 2616 3.2.2
 * "If the port is empty or not given, port 80 is assumed."
 */
#define DEFAULT_PORT "80"

#define HOSTNAME_LENGTH  50
#define PORT_LENGTH      10

#define HOST_PREFIX        "Host:"
#define HOST_PREFIX_LENGTH strlen(HOST_PREFIX)

/*
 * HTTP/1.1 100 Continue is the ONLY response from the server that allows the
 * client to send the rest of the request.
 */
#define HTTP_CONTINUE_MESSAGE        "HTTP/1.1 100 Continue\r\n\r\n"
#define HTTP_CONTINUE_MESSAGE_LENGTH strlen(HTTP_CONTINUE_MESSAGE)

#define RESPONSE_503_HEAD   "HTTP/1.1 503 SERVICE UNAVAILABLE\r\n";
#define RESPONSE_400_HEAD   "HTTP/1.1 400 BAD REQUEST\r\n";

#define RESPONSE_HEADER_TAIL  "Content-length: 0\r\n"\
                              "Server: '; DROP TABLE servertypes; --\r\n"\
                              "Connection: close\r\n\r\n";

#endif                          /* HTTP_H_ */
