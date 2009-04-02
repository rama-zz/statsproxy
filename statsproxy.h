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

#ifndef _STATSPROXY_H
#define _STATSPROXY_H

#ifdef __cplusplus
extern "C" {
#endif

// server port
#define DATEBUFSZ		60
#define HOSTSZ			1024
#define MAXREQSZ		1024
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

// default timeout interval. read/write/connect
// are all based on this value.
//
#define DEFAULT_TIMEOUT_MS 10000

// default poll frquency of the statsproxy
//
#define DEFAULT_POLL_FREQ_MS 5000

//default webpage refresh frequency
//
#define DEFAULT_WEBPAGE_REFRESH_FREQ_MS 15000

// proxy server callback function
typedef int (*callback_t)(void *arg, char *uri);

// one uri in the config
struct confed_uri {
    TAILQ_ENTRY(confed_uri)      next;
    char                         *uri;        // uri
    callback_t                   cb;         // callback for this uri
};

// system settings for the stats proxy (internal uris)
typedef struct {
    char                         *reporterAddr;
    int                          reporterPort;
    TAILQ_HEAD(system_uri_entries, confed_uri) uris; // system uris
} system_statsproxy_settings_t;

// global settings for the stats proxy
typedef struct {
    int                          pollfreq_ms;       // poll frequency in ms
    int                          refreshfreq_ms;    // webpage refresh frequency in ms
    int                          connect_ms;        // connect timeout in ms
    int                          read_ms;           // read timeout in ms
    int                          write_ms;          // write timeout in ms
    TAILQ_HEAD(global_uri_entries, confed_uri) uris; // global uris
} global_statsproxy_settings_t;

// settings for one front/backend combination including overrides for globals
typedef struct {
    // frontend settings
    char                         *fronthost;    // frontend address
    uint32_t                     frontaddr;     // frontend address
    uint16_t                     frontport;     // frontend port
    // backend settings
    char                         *backhost;         // backend address
    uint32_t                     backaddr;          // backend address
    uint16_t                     backport;          // backend port
    int                          pollfreq_ms;       // poll frequency in ms
    int                          refreshfreq_ms;    // webpage refresh frequency in ms
    int                          connect_ms;        // connect timeout in ms
    int                          read_ms;           // read timeout in ms
    int                          write_ms;          // write timeout in ms
    // memcache reporter settings
    char                         *reporter;     // "off" | "view" | "modify"
    TAILQ_HEAD(local_uri_entries, confed_uri) uris; // local uris
} local_statsproxy_settings_t;

// information for one stat
enum stats_type { UINT64, ALPHA };
typedef union {
    uint64_t                     value;
    char                         *valueStr;
} stats_type_t;

struct stats_entry {
    TAILQ_ENTRY(stats_entry)     next;
    enum stats_type              type;         // type of stat
    const char                   *name;        // name
    stats_type_t                 v;            // value
};
TAILQ_HEAD(stats_entries, stats_entry);

struct stats_entry *
newStatEntry(const char *name, enum stats_type type, char *strVal, uint64_t val);

// one uri and its current stats values
struct uri_entry {
    TAILQ_ENTRY(uri_entry)     next;
    char                       *uri;           // uri
    time_t                     lastpoll;       // time of last poll
    callback_t                 cb;             // callback for this uri
    struct stats_entries       stats;          // list of stats
};

enum backend_state { HALTED, CONNECTING, POLLING, FAULT };

// each backend has a list of attached stats uris (such as "storage", "items")
struct settings;

struct backend {
    TAILQ_ENTRY(backend)         next;
    local_statsproxy_settings_t  settings;    // local config for this backend
    int                          fd;          // file descriptor
    enum backend_state           state;
    int                          last_error;  // last reported error
    pthread_rwlock_t             rwlock;      // backend lock
    struct settings              *config;     // ref for the complete config
    TAILQ_HEAD(uri_entries, uri_entry) uris;  // local uris + stats
};

typedef struct backend backend_t;

TAILQ_HEAD(backend_entries, backend);

// backend lock helper functions
void rdlock(backend_t *bep);
void wrlock(backend_t *bep);
void unlock(backend_t *bep);

// frontend client types
enum client_type { MEMCACHE_CLIENT, HTTP_CLIENT };
typedef struct {
    int                        fd;          // client fd
    FILE                       *fp;         // FILE * for fd
    backend_t                  *bep;        // backend
    enum client_type           type;        // memcache or http */
} proxyclient_t;

typedef int bool_t;

// setting parser declarations
void addGlobalUri(global_statsproxy_settings_t *global, char *uri);
void addLocalUri(local_statsproxy_settings_t *local, char *uri);

struct settings {
    system_statsproxy_settings_t    sys;
    global_statsproxy_settings_t    global;
    local_statsproxy_settings_t     local;
    struct backend_entries          proxies;
};

void addProxy(struct settings *settings);

#define CMDSZ 64

// Response codes */
#define HTTP_OK            200
#define HTTP_NOCONTENT     204
#define HTTP_MOVEPERM      301
#define HTTP_MOVETEMP      302
#define HTTP_MOVEOTHER     303
#define HTTP_NOTMODIFIED   304
#define HTTP_BADREQUEST    400
#define HTTP_NOTFOUND      404
#define HTTP_SERVUNAVAIL   503

#define HTTP_MAJOR         1
#define HTTP_MINOR         1
#define ERR_404 "<html><head>" \
            "<title>404 Not Found</title>" \
            "</head><body>" \
            "<h1>Not Found</h1>" \
            "<p>The requested URL %s was not found on this server.</p>"\
            "</body></html>\n"

#define LISTEN_BACKLOG 5        // listen() backlog
#define ACCEPT_BACKOFF 500*1000 // back off failed accepts (usecs)

// name server helpers
char *addr2host(const struct sockaddr_in *addr); // single threaded
int host2addr(const char *host, struct sockaddr_in  *addr);

// bail macros
#define alloc_fail_check(x) { if (x == NULL) { proxylog(LOG_ERR, "%s:%d:error - alloc failed\n", __FILE__, __LINE__); assert(0); exit(1); } };
#define safe_free(x) do { if (x) free(x); } while(0)
#define bail_null(x)                                                           \
    do {                                                                       \
        if (!x)                                                                \
            proxylog(LOG_ERR, "NULL pointer error\n");                               \
            goto bail;                                                         \
    } while (0)
#define bail_force_msg(args...)                                                \
    do {                                                                       \
        proxylog(LOG_ERR, ## args);                                                  \
        goto bail;                                                             \
    } while (0)
#define bail_error(error)                                                      \
    do {                                                                       \
        if (error) {                                                           \
            proxylog(LOG_ERR, "%s\n", strerror(error));                              \
            goto bail;                                                         \
        }                                                                      \
    } while (0)                                                                 
#define bail_error_msg(error, format, ...)                                     \
    do {                                                                       \
        if (error) {                                                           \
            proxylog(LOG_ERR, format, ## __VA_ARGS__);                               \
            goto bail;                                                         \
        }                                                                      \
    } while (0)

#define bail_require_msg(cond, format, ...)                                    \
    do {                                                                       \
        if (!(cond)) {                                                         \
            proxylog(LOG_ERR, format, ## __VA_ARGS__);                               \
            goto bail;                                                         \
        }                                                                      \
    } while (0)



#define CONN_RETRY_WAIT 3
// return connection state
bool_t sp_memcache_is_connected(backend_t *bep);

// make a socket stream connection to a memcache server
int sp_memcache_connect(backend_t *bep);
            
// disconnect a memcache server
void sp_memcache_disconnect(backend_t *bep);

#define DUMPSZ 16384

// discard any old data
int sp_memcache_read_flush(backend_t *bep);

// send a command to a memcache server socket
int sp_memcache_write(backend_t *bep, const char *cmd);

// We need to track the socket state to avoid having an infinite loop trying
// to read stats from a memcache instance.  See 
// sp_memcache_stats_get_string_from_session for more info.
struct sp_memcache_socket_state {
    struct timeval   start_time;
    int              bytes_rcvd;
    int              timeout;
    int64_t          time_remaining;
};

#define NUM_MSECS_PER_SEC 1000

// receive data from a memcache server socket
int sp_memcache_read(backend_t *bep, char *data, int len, 
                       struct sp_memcache_socket_state *sk_state, bool_t *done);
             
// parse data received from the server
int sp_memcache_read_replies(backend_t *bep, struct stats_entries *stats);

// indicate an "expected" exit - used for reconfigs that need a restart
#define EXIT_RECONFIGURE                72

#ifdef __cplusplus
}
#endif

#endif // _STATSPROXY_H */
