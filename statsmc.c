// ========================================================================
//
//  Project   : statsproxy 
//
//  Version   : 1.0
//
//  Copyright :
//
//      Software License Agreement (BSD License)
//
//      Copyright (c) 2009, Gear Six, Inc.
//      All rights reserved.
//
//      Redistribution and use in source and binary forms, with or without
//      modification, are permitted provided that the following conditions are
//      met:
//
//      * Redistributions of source code must retain the above copyright
//        notice, this list of conditions and the following disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following disclaimer
//        in the documentation and/or other materials provided with the
//        distribution.
//
//      * Neither the name of Gear Six, Inc. nor the names of its
//        contributors may be used to endorse or promote products derived from
//        this software without specific prior written permission.The Gear Six logo, 
//        which is provided in the source code and appears on the user interface 
//        to identify Gear Six, Inc. as the originator of the software program, is 
//        a trademark of Gear Six, Inc. and can be used only in unaltered form and 
//        only for purposes of such identification.
//
//      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//      "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//      LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//      A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
//      OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//      SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
//      LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//      DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//      THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//      (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//      OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// ========================================================================
//

#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <poll.h>
#include <sys/time.h>

#include "queue.h"
#include "statsproxy.h"
#include "proxylog.h"

#define MAXSTATSZ 32768
#define MEMCACHE_STAT_CMD_ERROR_STR "ERROR"

// make a socket stream connection to a memcache server
int
sp_memcache_connect(backend_t *bep)
{
    /* Open a non-blocking socket connection to one of the memcache processes.
     * If the connection fails, return a relevant error code to the caller.
     */
    int err = 0;
    int32_t res = 0;
    struct sockaddr_in serv_addr;
    socklen_t reslen = sizeof(res);
    int timeout_ms = bep->settings.connect_ms;
    
    assert(bep->fd == -1);

    memset(&serv_addr, 0, sizeof serv_addr);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = bep->settings.backaddr;
    serv_addr.sin_port   = htons(bep->settings.backport);

    bep->fd = socket(AF_INET, SOCK_STREAM, 0);
    bail_require_msg(bep->fd >= 0, "Cannot open socket for %s:%d, %s", 
                     bep->settings.backhost, bep->settings.backport,
                     strerror(errno));

    if (fcntl(bep->fd, F_SETFL, O_NONBLOCK) < 0) {
        bail_force_msg("Cannot set socket as non-block for %s:%d, %s",
                       bep->settings.backhost, bep->settings.backport,
                       strerror(errno));
    }

    if (connect(bep->fd, (struct sockaddr *) &serv_addr,
                                             sizeof serv_addr) < 0) {
        err = errno;
        if (errno != EINPROGRESS) {
            bail_force_msg("Cannot connect to %s:%d, %s",
                            bep->settings.backhost, bep->settings.backport,
                             strerror(errno));
        }

        struct pollfd fds;
        memset(&fds, 0, sizeof(fds));
        fds.fd = bep->fd;
        fds.events = (POLLOUT | POLLERR | POLLHUP | POLLNVAL);
        if (poll(&fds, 1, timeout_ms) <= 0) {
            errno = ETIMEDOUT;
            bail_force_msg("connect timed out to %s:%d, %s", 
                           bep->settings.backhost, bep->settings.backport,
                           strerror(errno));
        }
    }

    if (getsockopt(bep->fd, SOL_SOCKET, SO_ERROR, (void *)&res, &reslen) < 0) {
        err = errno;
        bail_force_msg("cannot get socket status for %s:%d, %s",
                       bep->settings.backhost, bep->settings.backport,
                       strerror(errno));
    } else {
        err = 0;
    }

    if (res) {
        errno = err = res;
        bail_force_msg("cannot connect to %s:%d, %s", 
                       bep->settings.backhost, bep->settings.backport,
                       strerror(errno));
    }
    return err;

 bail:
    if (bep->fd >= 0) {
        close(bep->fd);
        bep->fd = -1;
    }
    bep->last_error = err;
    return err;
}

// fixed-time polling for memcache server sockets
static void
sp_memcache_stats_update_time_remaining(struct sp_memcache_socket_state *sk)
{
    int64_t cur_time_ms = 0;
    int64_t start_time_ms = 0;
    struct timeval current_time;

    gettimeofday(&current_time, NULL);

    cur_time_ms = current_time.tv_sec * NUM_MSECS_PER_SEC +
        current_time.tv_usec / NUM_MSECS_PER_SEC;
    start_time_ms = sk->start_time.tv_sec * NUM_MSECS_PER_SEC +
        sk->start_time.tv_usec / NUM_MSECS_PER_SEC;

    sk->time_remaining = sk->timeout - labs(cur_time_ms - start_time_ms);

    proxylog(LOG_DEBUG,
                 "current_time_ms=%"PRIi64", start_time_ms=%"PRIi64", "
                 "time_remaining=%"PRIi64,
                 cur_time_ms, start_time_ms, sk->time_remaining);
}

// send a command to a memcache server socket
int
sp_memcache_write(backend_t *bep, const char *cmd)
{
    int i = strlen(cmd);
    int err = 0;
    int num_chars = 0;
    struct pollfd pfd;
    struct sp_memcache_socket_state session_info;
    struct sp_memcache_socket_state *sk_state = &session_info;

    memset(&session_info, 0, sizeof session_info);
    gettimeofday(&session_info.start_time, NULL);
    session_info.timeout = session_info.time_remaining = bep->settings.write_ms;

    for (; i > 0; i -= num_chars) {
        cmd += num_chars;
        num_chars = write(bep->fd, cmd, strlen(cmd));
        if (num_chars < 0) {
            err = errno;
            if (errno == EWOULDBLOCK && sk_state->time_remaining > 0) {
                num_chars = 0;
                pfd.fd = bep->fd;
                pfd.events = POLLOUT | POLLERR;
                pfd.revents = 0;
                
                /* if poll times out, the socket is still not ready to write.
                 * This is unusual, but is OK.  The next write will return
                 * EWOULDBLOCK, and we will poll some more.
                 */
                if (poll(&pfd, 1, sk_state->timeout) < 0) {
                    err = errno;
                    bail_force_msg("poll failed for %s:%d, %s",
                                   bep->settings.backhost,
                                   bep->settings.backport,
                                   strerror(errno));
                }
            } else {
                if (errno == EWOULDBLOCK) {
                    /* we have reached the timelimit
                     */
                    bail_force_msg("write failed for %s:%d",
                                   bep->settings.backhost,
                                   bep->settings.backport);
                } else {
                    /* socket connection is broken
                     */
                    bail_force_msg("write failed for %s:%d, %s",
                                   bep->settings.backhost,
                                   bep->settings.backport,
                                   strerror(errno));
                }
            }
        }

        sp_memcache_stats_update_time_remaining(sk_state);

        if ((i - num_chars) > 0 && sk_state->time_remaining < 0) {
            /* we have reached the retry timeout
             */
            bail_force_msg("write timed out after %dms", sk_state->timeout);
        }
    }
    return err;

 bail:
    if (bep->fd >= 0) {
        close(bep->fd);
        bep->fd = -1;
    }
    bep->last_error = err;
    return err;
}

// parse data received from the server
int
sp_memcache_read(backend_t *bep, char *data, int len, 
                        struct sp_memcache_socket_state *sk_state, bool_t *done)
{
    int i = 0;
    int err = 0;
    int num_chars = 0;
    struct pollfd pfd;

    while (i < len && !*done) {
        /* Read up to bufsize on the first try, and decrease the read size to 
         * get the remaining bytes
         */
        num_chars = read(bep->fd, &data[i], (len - i));
        if (num_chars < 0) {
            err = errno;
            if (errno == EINTR || errno == EAGAIN) {
                if (sk_state->time_remaining > 0) {
                    pfd.fd = bep->fd;
                    pfd.events = POLLIN;
                    pfd.revents = 0;               
                    
                    /* if poll times out, the socket is still not ready to read.
                     * This is unusual, but is OK.  The next read() will return
                     * EAGAIN, and we will poll some more.
                     */
                    if (poll(&pfd, 1, sk_state->time_remaining) < 0) {
                        err = errno;
                        bail_force_msg("poll failed, %s", 
                                       strerror(errno));
                    }
                } else {
                    /* we have reached the time limit
                     */
                    bail_force_msg("read for %s:%d failed after %dms",
                                   bep->settings.backhost,
                                   bep->settings.backport,
                                   sk_state->timeout);
                }
            } else {
                /* socket connection is broken
                 */
                bail_force_msg("read for %s:%d failed, %s",
                               bep->settings.backhost,
                               bep->settings.backport,
                               strerror(errno));
            }
        } else {
            /* increment the data array index
             */
            err = 0;
            i += num_chars;
        }

        /* compute how much time is left to work with
         */
        sp_memcache_stats_update_time_remaining(sk_state);

        /* we have reached the end of the stats
         */
        if (num_chars == 0 || strstr(data, "END\r\n")) {
            *done = TRUE;
        } 

        /* we are done if we got an error message back
         */
        if (strstr(data, MEMCACHE_STAT_CMD_ERROR_STR)) {
            err = ECANCELED;
            goto bail;
        }
    }
    sk_state->bytes_rcvd = i;
    return err;

 bail:
    if (bep->fd >= 0) {
        close(bep->fd);
        bep->fd = -1;
    }
    bep->last_error = err;
    return err;
}

static int
sp_memcache_stats_parse_basic(char *str, char **name, char **value)
{
    /* "STAT <stat_name> <stat_value>"
     */
    int err = 0;

    *name = NULL;
    *value = NULL;

    if (str) {
        /* "<stat_name> <value>"
         */
        char *tok;
        char *save;

        tok = strtok_r(str, " ", &save);
        if (tok) {
            *name = strdup(tok);
            tok = strtok_r(NULL, " ", &save);
            if (tok) {
                *value = strdup(tok);
            } else {
                free(*name);
                *name = NULL; // don't return half a result
            }
        }
    }
    return err;
}

/* ------------------------------------------------------------------------ */
static int
sp_memcache_stats_add_stats(struct stats_entries *stats, 
                            char *stats_input, int *num_stats_added) 
{
    /* input string should be formatted as a series of 
     * "<stat name> <stat value>" pairs separated by the separator string
     */
    int err = 0;
    char *stat_name = NULL;
    char *stat_val = NULL;
    char *res = NULL;
    char *raw_stat = NULL;
    const char *session_prefix = "STAT ";
    char *save;

    res = strtok_r(stats_input, "\r\n", &save);
    while (res) {
        
        /* Most stats are prefixed, and we need to move past it before we
         * can parse the actual statistic.
         */
        if (strncmp(res, session_prefix, strlen(session_prefix)) == 0) {
            raw_stat = res + strlen(session_prefix);
        } else {
            raw_stat = res;
        }

        err = sp_memcache_stats_parse_basic(raw_stat, &stat_name, &stat_val);
        bail_error(err);
        if (stat_name && stat_val) {
            /* add the stat data to the uri
             */
            struct stats_entry *entry;
            entry = newStatEntry(stat_name, ALPHA, stat_val, 0);
            TAILQ_INSERT_TAIL(stats, entry, next);
            (*num_stats_added)++;
        }
        res = strtok_r(NULL, "\r\n", &save);
    }
 bail:
    return err;
}

// parse data received from the server
int
sp_memcache_read_replies(backend_t *bep, struct stats_entries *stats)
{
    int err = 0;
    char buffer[MAXSTATSZ];
    int done = FALSE;
    int valid_stats_cnt = 0;
    struct sp_memcache_socket_state session_info;

    memset(&session_info, 0, sizeof session_info);
    gettimeofday(&session_info.start_time, NULL);
    session_info.timeout = session_info.time_remaining = bep->settings.read_ms;

    while(!done) {
        memset(buffer, 0, MAXSTATSZ);
        err = sp_memcache_read(bep, buffer, MAXSTATSZ,
                                     &session_info, &done);
        
        /* handle ECANCELED here! 
         */
        if (err == ECANCELED) {
            err = 0;
            goto bail;
        } else {
            bail_error(err);
        }

        err = sp_memcache_stats_add_stats(stats, buffer, &valid_stats_cnt);
        bail_error(err);

        /* if we complete a socket read and get no valid stats data out of
         * the buffer then we are obviously being fed crap data, so give up
         */
        if (valid_stats_cnt == 0) {
            done = TRUE;
        }
    }
 bail:
    bep->last_error = err;
    return err;
}

// connection management routines for the backend

// return connection state
bool_t
sp_memcache_is_connected(backend_t *bep)
{
    return bep->fd >= 0;
}

// disconnect a memcache server
void
sp_memcache_disconnect(backend_t *bep)
{
    if (bep->fd >= 0) {
        close(bep->fd);
        bep->fd = -1;
    }
}
