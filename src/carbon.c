/*
 * Copyright (C) 2012 Ron Pedde <ron@pedde.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "carbon.h"

/*
 * Globals
 */

static char *c_host = NULL;
static uint16_t c_port = 0;
static int c_fd = -1;

/**
 * Initialize the carbon backend
 *
 * @param carbon_host ip or hostname of carbon-cache host
 * @param carbon_port tcp port of carbon-cache host
 * @returns TRUE on success, FALSE on failure
 */
int carbon_open(const char *carbon_host, uint16_t carbon_port) {
  int must_connect = 0;
  struct sockaddr_in sa;
  struct hostent *server;

  assert(carbon_host);

  if (!carbon_host)
    return FALSE;

  if((c_port != carbon_port) ||
     (strcmp(carbon_host, c_host) != 0) ||
     (c_fd == -1)) {
    /* we are either newly connecting, switching servers, or
       are recovering from a disconnect */
    c_host = strdup(carbon_host);
    c_port = carbon_port;

    if (!c_host)
      return FALSE;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;

    /* try as dotted quad first */
    sa.sin_addr.s_addr = inet_addr(c_host);
    if(sa.sin_addr.s_addr == INADDR_NONE) {
      /* try as hostname */
      server = gethostbyname(c_host);
      if (server == NULL)
        return FALSE;

      memcpy((void*)&sa.sin_addr.s_addr, server->h_addr,
             server->h_length);
    }

    sa.sin_port = htons(c_port);

    c_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c_fd < 0) /* we will just fail and retry next pass */
      return TRUE;

    if (connect(c_fd, (const struct sockaddr *)&sa,
                sizeof(sa)) < 0) {
      close(c_fd);
      c_fd = -1;
      return TRUE; /* again, we'll just retry next pass */
    }
    syslog(LOG_DEBUG,"Connected to carbon line-receiver");
  }

  return TRUE;
}

/**
 * Send a prepared block of metrics
 *
 * @param metric arbitrary metric like "system.foo.loadavg"
 * @param value value for metric
 * @param epoch metric time as unix epoch.  defaults to time(NULL) if NULL
 * @returns TRUE on success, FALSE on failure
 */
int carbon_send(const char *message) {
  int len;

  assert(message);

  if (!message)
    return FALSE;

  if(c_fd == -1) /* we'll just drop these on the floor */
    return TRUE;

  len = strlen(message);

  syslog(LOG_DEBUG, "Sending carbon metrics:\n%s\n", message);

  if(len != write(c_fd, message, len)) {
    /* we will reopen next pass */
    shutdown(c_fd,2);
    close(c_fd);
    c_fd = -1;
    syslog(LOG_WARNING, "Disconnect from carbon host.  Will retry");
    return FALSE;
  }

  return TRUE;
}
