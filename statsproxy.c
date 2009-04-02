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
#include <inttypes.h>
#include <netdb.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>

#include "queue.h"
#include "statsproxy.h"
#include "proxylog.h"
#include "mcr_web.h"
#include "uristrings.h"

static char sysLogo[] =
#include "g6logo.inc"

// return a millisecond timestamp
static uint64_t
timestamp(void)
{
    int rc;
    struct timeval  tv;

    rc = gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// single threaded!
char *
addr2host(const struct sockaddr_in *addr)
{
    static char     hostname[MAXHOSTNAMELEN];
    struct hostent *hp = NULL;
    char            *dot = NULL;

    hp = gethostbyaddr((char *) &addr->sin_addr.s_addr,
                       sizeof (struct in_addr), AF_INET);
    if (hp) {
        strncpy(hostname, hp->h_name, MAXHOSTNAMELEN - 1);
        dot = strchr(hostname, '.');
        if (dot) {
            *dot = '\0';  // trim the fqdn
        }
        return hostname;
    } else {
        return inet_ntoa(addr->sin_addr);
    }
}

int
host2addr(const char *host, struct sockaddr_in  *addr)
{
    struct hostent      *myEnt;

    if (host == NULL || addr == NULL) {
        return EINVAL;
    }

    if ((addr->sin_addr.s_addr = inet_addr(host)) != INADDR_NONE) {
        // dot format IP address given
    } else {
        // look up host name
        if ((myEnt = gethostbyname(host)) != NULL) {
            addr->sin_addr.s_addr = *((uint32_t *) myEnt->h_addr);
        } else {
            // gethostbyname failed
            return ENOENT;
        }
    }
    return 0;
}

// some simple html goop

static void
write_page_refresh(int refresh_ms, FILE *fp)
{
    fprintf(fp,
"<script language=\"JavaScript\"> "
"var sURL = unescape(window.location.pathname); "
"function doLoad() { setTimeout( \"refresh()\", %d ); } "
"function refresh() { window.location.href = sURL; } "
"</script> "
"<script language=\"JavaScript1.1\"> "
"function refresh() { window.location.replace( sURL ); } "
"</script> "
"<script language=\"JavaScript1.2\"> "
"function refresh() { window.location.reload( false ); } "
"</script>\r\n"
"<BODY onload=\"doLoad()\"> \r\n", refresh_ms);
}

static char *
rfc1123date(char *datestr, time_t t)
{
  struct tm *tm;
  tm = localtime(&t);
  strftime(datestr, DATEBUFSZ, "%a %d %m %Y %H:%M:%S", tm);
  return datestr;
}

static void
write_http_header(const char *mimeType, FILE *http) 
{
    char dateBuf[DATEBUFSZ], lastModifiedBuf[DATEBUFSZ];
    time_t now;

    time(&now);
    fprintf(http,
        "HTTP/1.1 200 OK\r\n"
        "X-Date: %s\r\n"
        "Server: Gear6 Memcached\r\n"
        "MIME-version: 1.0\r\n"
        "Last-Modified: %s\r\n"
        "Cache-Control: no-cache\r\n"
        "Content-type: %s\r\n\r\n",
        rfc1123date(dateBuf, now), rfc1123date(lastModifiedBuf, now), mimeType);
}

static void
write_html_image(FILE *http, char *bits, int size)
{
    write_http_header("image/png", http);
    fwrite(bits, size, 1, http);
}
static void
write_html_body(FILE *http)
{
    char hostname[HOSTSZ];

    gethostname(hostname, HOSTSZ);
    fprintf(http,
    "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\r\n"
    "<HTML>\r\n"
    "<TITLE>%s - Gear6 Memcached</TITLE>\r\n",
    hostname);
}

static void
write_html_service_info(proxyclient_t *clnt, int vipLabel)
{
    time_t              now;
    char                timeBuf[DATEBUFSZ];
    backend_t           *bep = clnt->bep;
    struct uri_entry    *entry;
    struct in_addr      addr;

    time(&now);
    ctime_r(&now, timeBuf);
    timeBuf[strlen(timeBuf) - 1] = '\0'; // zap newline
    addr.s_addr = bep->settings.backaddr;

    if (vipLabel) {
        fprintf(clnt->fp, "<a href=\"http://%s\">"
            "<img border=\"0\" src=\"logo.png\" alt=\"Logo\" "
            "align=\"absmiddle\"/></a>"
            "&nbsp;&nbsp;Memcache Information for "
            "<b>%s:%d</b> "
            "<font size=\"-1\">(proxy %s:%d)</font> "
            "%s<br>",
            bep->settings.backhost,
            bep->settings.backhost,
            bep->settings.backport,
            bep->settings.fronthost,
            bep->settings.frontport,
            timeBuf);
    } else {
        fprintf(clnt->fp, "<a href=\"http://%s\">"
                "<img border=\"0\" src=\"logo.png\" alt=\"Logo\" "
                "align=\"absmiddle\"/></a>"
                "&nbsp;&nbsp;<b>Memcache Reporter</b> %s",
                bep->settings.backhost, timeBuf);
    }
    fprintf(clnt->fp, "<hr>Raw stats: \r\n");
    fprintf(clnt->fp, "<b><a href=\"/\">basic</a></b> ");
    TAILQ_FOREACH(entry, &bep->uris, next) {
        fprintf(clnt->fp, "<b><a href=\"%s\">%s</a></b> ",
                entry->uri, entry->uri);
    }

    /* if memcache reporter is turned off; print nothing. */
    if (strcmp(bep->settings.reporter, "off") != 0) {
        fprintf(clnt->fp, "<br>Memcache Reporter stats: "
                "[<i>top clients by: </i><b>"
                "<a href=\"top-clients-ops?addr=%s&port=%hd&key=ops\">ops</a> "
                "<a href=\"top-keys-select?addr=%s&port=%hd\">keys</a>"
                "</b>]&nbsp;&nbsp; "
                "[<i>top keys by: </i><b>"
                "<a href=\"top-keys-gets?addr=%s&port=%hd\">gets</a>  "
                "<a href=\"top-keys-sets?addr=%s&port=%hd\">sets</a>  "
                "<a href=\"top-keys-all?addr=%s&port=%hd\">all</a>"
                "</b>]&nbsp;&nbsp; ",
                inet_ntoa(addr), bep->settings.backport,
                inet_ntoa(addr), bep->settings.backport,
                inet_ntoa(addr), bep->settings.backport,
                inet_ntoa(addr), bep->settings.backport,
                inet_ntoa(addr), bep->settings.backport);

        /* if memcache reporter is configured in the read-only or "view"
         * mode then do not provide a config option. in other words if
         * the memcache reporter section reads "modify" provide the 
         * config option.
         * */
        if (strcmp(bep->settings.reporter, "modify") == 0) {
            fprintf(clnt->fp,
                "[<i>settings: </i>"
                "<a href=\"mcr-config\">config</a> \n"
                "]&nbsp;&nbsp; ");
        }
    }

    fprintf(clnt->fp, "<br><br><i>[polling interval: %dms, ",
            bep->settings.pollfreq_ms);
    fprintf(clnt->fp, "webpage refresh interval: %dms, ",
            bep->settings.refreshfreq_ms);
    fprintf(clnt->fp, "connect/read/write timeout: %d/%d/%dms]</i><br>\n",
            bep->settings.connect_ms,
            bep->settings.read_ms,
            bep->settings.write_ms);
    fprintf(clnt->fp, "<hr>\r\n");
}

static void
end_html_body(FILE *fp) 
{
    /* print statsproxy version number. 
     * */
    fprintf(fp, "<hr>\r\n");
    fprintf(fp, "Generated by statsproxy %s\r\n", VERSION);

    /* end html body. */
    fprintf(fp, "</BODY>\r\n");
    fprintf(fp, "</HTML>\r\n");
}

static void *handleFrontendRequest(void *);

static void *
runFrontend(void *arg)
{
    backend_t *bep = (backend_t *) arg;
    int sockfd;
    int newsockfd;
    int reuse = 1;
    int clilen;
    struct sockaddr_in cli_addr;
    struct sockaddr_in serv_addr;
    pthread_t chld_thr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        proxylog(LOG_ERR, "error - can't open socket - server for %s:%d "
                "unavailable",
                bep->settings.fronthost,
                bep->settings.frontport);
        exit(1);
    }

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    if (host2addr(bep->settings.fronthost, &serv_addr) != 0) {
        proxylog(LOG_ERR, "%s: failed to resolve host %s", __FUNCTION__,
                bep->settings.fronthost);
        exit(1);
    }
    serv_addr.sin_port = htons(bep->settings.frontport);
    
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        proxylog(LOG_ERR, "error - can't bind socket for %s:%d",
                bep->settings.fronthost, bep->settings.frontport);
        exit(1);
    }

    listen(sockfd, LISTEN_BACKLOG);

    for (;;){
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, (socklen_t *)
    							&clilen);
        if (newsockfd < 0) {
            proxylog(LOG_ERR, "server accept error for %s:%d: %s",
                    bep->settings.fronthost, bep->settings.frontport,
                    strerror(errno));
            usleep(ACCEPT_BACKOFF);  // slow down the inbounds
            continue;
        }

        proxyclient_t *clnt = (proxyclient_t *) calloc(1, sizeof *clnt);
        alloc_fail_check(clnt);
        clnt->fd = newsockfd;
        clnt->bep = bep;
        if (pthread_create(&chld_thr, NULL, handleFrontendRequest,
                           (void *) clnt) != 0) {
            proxylog(LOG_ERR, "could not create thread for %s:%d: %s",
                    bep->settings.fronthost, bep->settings.frontport, 
                    strerror(errno));
            exit(1);
        }
        pthread_detach(chld_thr);
    }
    // not reached
    return NULL;
}

static callback_t
findCallback(proxyclient_t *clnt, char *uri)
{
    struct settings   *config = clnt->bep->config;
    struct confed_uri *system_uri;
    struct uri_entry  *bep_entry;

    // check system uris first
    TAILQ_FOREACH(system_uri, &config->sys.uris, next) {
        if (strcmp(system_uri->uri, uri) == 0) {
            return system_uri->cb;
        }
    }

    // check backend specific uris (stats)
    TAILQ_FOREACH(bep_entry, &clnt->bep->uris, next) {
        if (strcmp(bep_entry->uri, uri) == 0) {
            return bep_entry->cb;
        }
    }
    return NULL;
}

static void
setClientType(proxyclient_t *clnt, char *method)
{
    if (strcmp(method, "stats") == 0) {
        clnt->type = MEMCACHE_CLIENT;
    } else {
        clnt->type = HTTP_CLIENT;
    }
}

static bool_t
badMethod(char *method)
{
    if (method == NULL) {
        return TRUE;
    }

    if (strcmp(method, "GET") == 0) {
        return FALSE;
    }
    if (strcmp(method, "POST") == 0) {
        return FALSE;
    }
    if (strcmp(method, "stats") == 0) {
        return FALSE;
    }
    return TRUE;
}

static void
clntRedirect(proxyclient_t *clnt, int httpCode, const char *uri)
{
    switch (httpCode) {
    case HTTP_MOVEPERM:
    case HTTP_MOVETEMP:
        fprintf(clnt->fp, "HTTP/%d.%d %d %s\r\nLocation: %s\r\n",
            HTTP_MAJOR, HTTP_MINOR, httpCode, "Found", uri);
        break;
    default:
        fprintf(clnt->fp, "HTTP/%d.%d %d %s\r\n",
            HTTP_MAJOR, HTTP_MINOR, httpCode, "ERROR");
    }
}

static void
clntError(proxyclient_t *clnt, int httpCode, char *uri)
{
    if (clnt->type == MEMCACHE_CLIENT) {
        if (clnt->bep->last_error == 0) {
            fprintf(clnt->fp, "ERROR\r\n");
        } else {
            fprintf(clnt->fp, "ERROR (%s:%d - %s)\r\n",
                clnt->bep->settings.backhost,
                clnt->bep->settings.backport,
                strerror(clnt->bep->last_error));
        }
        return;
    }
    switch (httpCode) {
    case HTTP_NOTFOUND:
        fprintf(clnt->fp, ERR_404, uri);
        break;
    case HTTP_SERVUNAVAIL:
        write_http_header("text/html", clnt->fp);
        write_html_body(clnt->fp);
        write_page_refresh(clnt->bep->settings.refreshfreq_ms, clnt->fp);
        write_html_service_info(clnt, TRUE);
        fprintf(clnt->fp,
                "Error getting stats from remote memcached: <b>%s</b>",
                strerror(clnt->bep->last_error));
        end_html_body(clnt->fp);
        break;
    default:
        fprintf(clnt->fp, "HTTP/%d.%d %d %s\r\n",
        HTTP_MAJOR, HTTP_MINOR, httpCode, "ERROR");
    }
}

static int
countStat(struct uri_entry *uri_entry, const char *name)
{
    struct stats_entry *entry;
    int count = 0;

    TAILQ_FOREACH(entry, &uri_entry->stats, next) {
        if (strcmp(entry->name, name) == 0) {
            count++;
        }
    }
    return count;
}

static void
rawPrintStats(char *uri, struct uri_entry *uri_entry, FILE *fp)
{
    struct stats_entry *entry;

    TAILQ_FOREACH(entry, &uri_entry->stats, next) {
        if (countStat(uri_entry, entry->name) > 1) {
            proxylog(LOG_ERR, "dupe stat %s detected", entry->name);
        }
        if (entry->type == ALPHA) {
            fprintf(fp, "STAT %s %s\r\n", entry->name, entry->v.valueStr);
        } else {
            fprintf(fp, "STAT %s %"PRIu64"\r\n", entry->name, entry->v.value);
        }
    }
    fprintf(fp, "END\r\n");
}

static void
htmlPrintStats(char *uri, struct uri_entry *uri_entry, FILE *fp)
{
    struct stats_entry *entry;

    TAILQ_FOREACH(entry, &uri_entry->stats, next) {
        if (entry->type == ALPHA) {
            fprintf(fp,
                    "<font size=\"-2\">STAT</font> <i>%s</i> <b>%s</b><br>",
                    entry->name, entry->v.valueStr);
        } else {
            fprintf(fp,
                    "<font size=\"-2\">STAT</font> <i>%s</i> <b>%"PRIu64
                    "</b><br>",
                    entry->name, entry->v.value);
        }
    }
}

// deliver cached stats
static int
statsCallback(void *arg, char *uri)
{
    int              closeConnection = TRUE;
    proxyclient_t    *clnt = (proxyclient_t *) arg;
    backend_t        *bep = clnt->bep;
    struct           uri_entry *entry = NULL;

    // find the uri
    TAILQ_FOREACH(entry, &bep->uris, next) {
        if (strcmp(entry->uri, uri) == 0) {
            break;
        }
    }
    if (entry == NULL) {
        clntError(clnt, HTTP_NOTFOUND, uri);
        goto bail;
    }
    rdlock(clnt->bep);

    if (clnt->bep->state != POLLING) {
        clntError(clnt, HTTP_SERVUNAVAIL, uri);
        unlock(clnt->bep);
        goto bail;
    }

    // deliver the stays chain hanging off of it
    if (clnt->type == MEMCACHE_CLIENT) {
        rawPrintStats(uri, entry, clnt->fp);
        closeConnection = FALSE;
    } else {
        write_http_header("text/html", clnt->fp);
        write_html_body(clnt->fp);
        write_page_refresh(clnt->bep->settings.refreshfreq_ms, clnt->fp);
        write_html_service_info(clnt, TRUE);
        htmlPrintStats(uri, entry, clnt->fp);
        end_html_body(clnt->fp);
    }
    unlock(clnt->bep);
bail:
    return closeConnection;
}

// system uri for reporter interface
static int
reporterCallback(void *arg, char *uri)
{
    int           err = 0;
    proxyclient_t *clnt = (proxyclient_t *) arg;
    char          *c;
    char          *parse;
    char          *parseEnd;
    char          *addr = NULL;
    char          *key = NULL;
    char          *portStr = NULL;
    uint16_t      port = 0;

    parse = uri;
    parseEnd = uri + strlen(uri);

    // parse uri params
    for (c = uri; *c != '\0'; c++) {
        if (*c == '?' || *c == '&') {
            *c = '\0';
        }
    }
    for (c = parse; c < parseEnd; c++) {
        if (strncmp(c, "addr=", 5) == 0) {
            addr = c + 5;
        } else if (strncmp(c, "port=", 5) == 0) {
            portStr = c + 5;
            sscanf(portStr, "%hd", &port);
        } else if (strncmp(c, "key=", 4) == 0) {
            key = c + 4; // XXX fix keys with spaces?
        }
    }

    // configuration uri
    if (strcmp(uri, "mcr-config") == 0) {
        write_http_header("text/html", clnt->fp);
        write_html_body(clnt->fp);
        write_html_service_info(clnt, FALSE);
        err = write_html_mcr_config(clnt, uri);
        if (err) {
            fprintf(clnt->fp, "Error: could not get memcache reporter "
                              "configuration - please check logs for "
                              "more information");
        }
        end_html_body(clnt->fp);

    // top keys uri
    } else if (strcmp(uri, "top-keys-gets") == 0 ||
               strcmp(uri, "top-keys-sets") == 0 ||
               strcmp(uri, "top-keys-all")  == 0 ||
               strcmp(uri, "top-keys-select") == 0) {

        char *op = &uri[9];
        write_http_header("text/html", clnt->fp);
        write_html_body(clnt->fp);
        write_page_refresh(MCRREFRESH, clnt->fp);
        write_html_service_info(clnt, FALSE);
        if (addr == NULL || port == 0) {
            fprintf(clnt->fp, "Error: addr or port parameter not specified");
        } else {
            err = write_html_mcr_top_keys(clnt, addr, port, op,
                                          TKTIME, TKNUM);
            if (err) {
                fprintf(clnt->fp, "Error: could not get top keys for %s:%hd"
                                  " - please check reporting is enabled or "
                                  " try later", addr, port);
            }
        }
        end_html_body(clnt->fp);

    // top clients uri
    } else if (strcmp(uri, "top-clients-ops") == 0 ||
               strcmp(uri, "top-clients-keys") == 0) {
        write_http_header("text/html", clnt->fp);
        write_html_body(clnt->fp);
        write_page_refresh(MCRREFRESH, clnt->fp);
        write_html_service_info(clnt, FALSE);
        if (addr == NULL || port == 0 || key == NULL) {
            fprintf(clnt->fp, "Error: addr+port+key parameters not specified");
        } else {
            err = write_html_mcr_top_clients(clnt, addr, port, key,
                                          TCTIME, TCNUM);
            if (err) {
                fprintf(clnt->fp, "Error: could not get top clients for %s:%hd"
                                  " - please check reporting is enabled or "
                                  " try later", addr, port);
            }
        }
        end_html_body(clnt->fp);

    // configuration uri
    } else if (strcmp(uri, "mcr-enable") == 0 ||
               strcmp(uri, "mcr-disable") == 0) {

        if (addr == NULL || port == 0) {
            write_http_header("text/html", clnt->fp);
            fprintf(clnt->fp, "Error: addr+port+key parameters not specified");
            end_html_body(clnt->fp);
        } else {
            err = mcr_op(clnt, strcmp(uri, "mcr-enable") == 0 ? "add" : "del",
                         addr, port);
            if (err) {
                write_http_header("text/html", clnt->fp);
                fprintf(clnt->fp, "System error setting reporter params -"
                              "please check logs for more information");
                end_html_body(clnt->fp);
            } else {
                clntRedirect(clnt, HTTP_MOVETEMP, "/mcr-config");
            }
        }

    } else {
        clntError(clnt, HTTP_NOTFOUND, uri);
    }
    return TRUE;
}

// system uri for embedded image delivery
static int
imageCallback(void *arg, char *uri)
{
    proxyclient_t *clnt = (proxyclient_t *) arg;
    if (strcmp(uri, "logo.png") == 0) {
        write_html_image(clnt->fp, sysLogo, sizeof sysLogo);
    } else {
        clntError(clnt, HTTP_NOTFOUND, uri);
    }
    return TRUE;
}

// process inbound telnet or web requests
static void *
handleFrontendRequest(void *arg)
{
    proxyclient_t     *clnt = (proxyclient_t *) arg;
    int               sock = clnt->fd;
    callback_t        cb;
    char              servRequest[MAXREQSZ];
    char              uri[MAXREQSZ];
    char              method[MAXREQSZ];
    FILE              *serv;

    if ((serv = fdopen(sock, "a+")) == NULL) {
        proxylog(LOG_ERR, "could not open file pointer to socket (%d)", sock);
        close (sock);
        return NULL;
    }

    clnt->fp = serv;

    int done = FALSE;
    while (!done) {
        memset(servRequest, 0, MAXREQSZ);
        memset(uri, 0, MAXREQSZ);
        memset(method, 0, MAXREQSZ);
        // pull the server request
        if (fgets(servRequest, MAXREQSZ, serv) == NULL) {
            goto end;
        }
        sscanf(servRequest, "%15s %1000s", method, uri);

        if (badMethod(method) || uri == NULL) {
            clntError(clnt, HTTP_BADREQUEST, uri);
            goto end;
        } else {
            setClientType(clnt, method);
            if (clnt->type == HTTP_CLIENT) {
                // line buffer to make more interactive
                setlinebuf(clnt->fp);
            } 
        }

        char *uriStr = uri;
        if (uriStr[0] == '/') {
            // strip leading /
            uriStr++;
        }

        char *decodedUri = uri_decode(uriStr);

        if (strchr(uriStr, '?') != NULL) {
            // get base uri string without params
            *strchr(uriStr, '?') = '\0';
        }

        cb = findCallback(clnt, uriStr);
        if (!cb) {
            free(decodedUri);
            clntError(clnt, HTTP_NOTFOUND, uriStr);
            goto end;
        }
        done = (*cb)(clnt, decodedUri);
        free(decodedUri);
        fflush(clnt->fp);
    }
        
    // close the socket and exit this thread
end:
    fflush(serv);
    fclose(serv);
    free(clnt);
    return NULL;
}

static void
removeOldStats(struct uri_entry *uri_entry)
{
    struct stats_entry *entry;
    struct stats_entry *tmp;

    entry = TAILQ_FIRST(&uri_entry->stats);
    while (entry != NULL) {
        tmp = TAILQ_NEXT(entry, next);
        free((void *) entry->name);
        if (entry->type == ALPHA) {
            free(entry->v.valueStr);
        }
        free(entry);
        entry = tmp;
    }
    TAILQ_INIT(&uri_entry->stats);
}

// allocate a stats entry
struct stats_entry *
newStatEntry(const char *name, enum stats_type type, char *strVal, uint64_t val)
{
    struct stats_entry *entry;

    entry = (struct stats_entry *) calloc(1, sizeof *entry);
    alloc_fail_check(entry);

    entry->name = name;
    entry->type = type;
    switch (type) {
    case ALPHA:
        entry->v.valueStr = strVal; // must be allocated externally
        break;
    case UINT64:
        entry->v.value = val;
        break;
    default:
        assert(0);
    }
    return entry;
}

// add new stats to the uri
static void
addNewStats(struct uri_entry *uri_entry, struct stats_entries *new_stats)
{
    struct stats_entry *entry;

    while (!TAILQ_EMPTY(new_stats)) {
        entry = TAILQ_FIRST(new_stats);
        TAILQ_REMOVE(new_stats, entry, next);
        TAILQ_INSERT_TAIL(&uri_entry->stats, entry, next);
    }
}

static int
checkAndConnect(backend_t *bep)
{
    int i;
    int err = 0;
    int retries = 5;

    // try to connect
    wrlock(bep);
    bep->state = CONNECTING;
    unlock(bep);
    for (i = 0; i <= retries; i++) {
        if (!sp_memcache_is_connected(bep)) {
            err = sp_memcache_connect(bep);
            if (err == 0) {
                break;
            }
            sleep(CONN_RETRY_WAIT);
        }
    }
    return err;
}

// get one stats command result, update cached values
static void
getStat(struct uri_entry *uri_entry, backend_t *bep)
{
    int err = 0;
    char statsCmd[CMDSZ];

    snprintf(statsCmd, sizeof statsCmd, "stats %s\r\n", uri_entry->uri);
    err = sp_memcache_write(bep, statsCmd);

    struct stats_entries new_stats;
    TAILQ_INIT(&new_stats);

    if (err == 0) {
        // parse command results
        err = sp_memcache_read_replies(bep, &new_stats);

        // update stats with new ones - (or nuke old ones on error)
        wrlock(bep);
        removeOldStats(uri_entry);
        addNewStats(uri_entry, &new_stats);
        unlock(bep);
    }
}

// get health information - used for liveness checks etc
#define LIVENESS_CMD "set __LIVENESS__ 0 0 12\r\nliveness chk\r\n"
#define LIVENESS_OK  "STORED\r\n"

static void
getHealth(struct uri_entry *uri_entry, backend_t *bep)
{
    int                done = 0;
    int                err = 0;
    time_t             now;
    int64_t            livenessDelta = 0;
    uint64_t           start;
    uint64_t           pollDelta = 0;
    uint64_t           livenessVal = 0;
    char               *polltimeBuf;
    char               healthCmd[CMDSZ];
    struct stats_entry *liveness;
    struct stats_entry *lastpoll;
    struct stats_entry *lastpoll_int;
    struct stats_entry *liveness_resp;
    struct sp_memcache_socket_state session_info;

    memset(&session_info, 0, sizeof session_info);
    gettimeofday(&session_info.start_time, NULL);
    session_info.timeout =  session_info.time_remaining = bep->settings.read_ms;

    // pull the last polltime
    time(&now);
    polltimeBuf = (char *) calloc(1, DATEBUFSZ);
    alloc_fail_check(polltimeBuf);
    ctime_r(&now, polltimeBuf);
    polltimeBuf[strlen(polltimeBuf) - 1] = '\0'; // zap newline

    if (uri_entry->lastpoll > 0) {
        pollDelta = now - uri_entry->lastpoll;
    } else {
        pollDelta = 0;
    }

    // do a liveness check
    snprintf(healthCmd, sizeof healthCmd, LIVENESS_CMD);
    start = timestamp();
    err = sp_memcache_write(bep, healthCmd);
    if (err != 0) {
        goto fail;
    }

    // expect "STORED\r\n" or "ERROR\r\n" or timeout/fail
    memset(healthCmd, 0, CMDSZ);
    err = sp_memcache_read(bep, healthCmd, strlen(LIVENESS_OK),
                           &session_info, &done);

    livenessDelta = timestamp() - start;

    // livenessDelta cannot be negative.
    //
    livenessDelta = (livenessDelta < 0) ? 0 : livenessDelta;

    if (err != 0) {
        goto fail;
    }
    if (strcmp(healthCmd, LIVENESS_OK) != 0) {
        // didn't get expected answer
        goto fail;
    }

    // things are ok - mark as such in the stats
    livenessVal = 1;
        
fail:
    liveness = newStatEntry(strdup("liveness"), UINT64, NULL, livenessVal);
    liveness_resp = newStatEntry(strdup("respTimeMs"), UINT64, NULL,
                                 livenessDelta);
    lastpoll_int = newStatEntry(strdup("statsAge"), UINT64, NULL, pollDelta);
    lastpoll = newStatEntry(strdup("lastpoll"), ALPHA, polltimeBuf, 0);
    wrlock(bep);
    removeOldStats(uri_entry);
    TAILQ_INSERT_TAIL(&uri_entry->stats, lastpoll_int, next);
    TAILQ_INSERT_TAIL(&uri_entry->stats, lastpoll, next);
    TAILQ_INSERT_TAIL(&uri_entry->stats, liveness, next);
    TAILQ_INSERT_TAIL(&uri_entry->stats, liveness_resp, next);
    unlock(bep);
}

// memcache server poller
static void *
runBackend(void *arg)
{
    backend_t *bep = (backend_t *) arg;
    bool_t done = FALSE;

    uint64_t now;
    uint64_t then;
    int64_t delta;
    int64_t sleep_time;

    wrlock(bep);
    bep->state = HALTED;
    unlock(bep);

restart:
    while (!done) {

        // poll for each of the configured uri
        struct uri_entry *uri_entry;

        then = timestamp();
        TAILQ_FOREACH(uri_entry, &bep->uris, next) {

            if (checkAndConnect(bep) != 0) {
                goto restart;
            }

            wrlock(bep);
            bep->state = POLLING;
            unlock(bep);

            if (strcmp(uri_entry->uri, "health") == 0) {
                // synthetic "health" stats entry
                getHealth(uri_entry, bep);
            } else {
                // request this stats uri
                getStat(uri_entry, bep);
            }

            uri_entry->lastpoll = time(0);
        }
        sp_memcache_disconnect(bep);
        now = timestamp();

        delta = now - then;

        // take care of time going backwards
        //
        delta = (delta < 0) ? 0 : delta;

        sleep_time =  bep->settings.pollfreq_ms - delta;

        // if sleep_time is negative then sleep for 500ms
        // else sleep for the whole sleep_time duration.
        //
        sleep_time = (sleep_time < 0) ? 500 : sleep_time;

        // wait for next poll
        //
        usleep(sleep_time * 1000);
    }
    wrlock(bep);
    bep->state = HALTED;
    unlock(bep);
    return NULL;
}

// always pick a local setting over a global one
#define LOCAL_OR_GLOBAL(x) \
    (local_settings->x != 0 ? local_settings->x : global_settings->x)

// create a new backend server
static backend_t *
backend_new(global_statsproxy_settings_t *global_settings,
            local_statsproxy_settings_t  *local_settings)
{
    int                  err = 0;
    struct sockaddr_in   front;
    struct sockaddr_in   back;
    pthread_rwlockattr_t attr;
    backend_t            *bep = NULL;

    memset(&front, 0, sizeof front);
    memset(&back, 0, sizeof back);

    err = host2addr(local_settings->backhost, &back);
    bail_error_msg(err, "lookup fail for %s", local_settings->backhost);

    err = host2addr(local_settings->fronthost, &front);
    bail_error_msg(err, "lookup fail for %s", local_settings->fronthost);

    bep = (backend_t *) calloc(1, sizeof(backend_t));
    alloc_fail_check(bep);

    bep->settings.fronthost   = strdup(local_settings->fronthost);
    alloc_fail_check(bep->settings.fronthost);
    bep->settings.frontaddr   = front.sin_addr.s_addr;
    bep->settings.frontport   = local_settings->frontport;
    bep->settings.backhost    = strdup(local_settings->backhost);
    alloc_fail_check(bep->settings.backhost);
    bep->settings.backaddr    = back.sin_addr.s_addr;
    bep->settings.backport    = local_settings->backport;
    bep->settings.pollfreq_ms = LOCAL_OR_GLOBAL(pollfreq_ms);

    bep->settings.refreshfreq_ms = LOCAL_OR_GLOBAL(refreshfreq_ms);

    bep->settings.connect_ms  = LOCAL_OR_GLOBAL(connect_ms);
    bep->settings.read_ms     = LOCAL_OR_GLOBAL(read_ms);
    bep->settings.write_ms    = LOCAL_OR_GLOBAL(write_ms);
    bep->fd          = -1;
    bep->state       = HALTED;

    /* memcache reporter settings. */
    if (local_settings->reporter != NULL) {
        bep->settings.reporter = strdup(local_settings->reporter); 
        alloc_fail_check(bep->settings.reporter);
    } else {
        bep->settings.reporter = strdup("off");
        alloc_fail_check(bep->settings.reporter);
    }

    pthread_rwlockattr_init(&attr);
    pthread_rwlock_init(&bep->rwlock, &attr);
    TAILQ_INIT(&bep->uris);
bail:
    return bep;
}

void
rdlock(backend_t *bep)
{
    pthread_rwlock_rdlock(&bep->rwlock);
}

void
wrlock(backend_t *bep)
{
    pthread_rwlock_wrlock(&bep->rwlock);
}

void
unlock(backend_t *bep)
{
    pthread_rwlock_unlock(&bep->rwlock);
}

// server start routines
static void
startFrontendServer(backend_t *bep)
{
    pthread_t chld_thr;
    pthread_create(&chld_thr, NULL, runFrontend, (void *) bep);
    pthread_detach(chld_thr);
}

static void
startBackendServer(backend_t *bep)
{
    pthread_t chld_thr;
    pthread_create(&chld_thr, NULL, runBackend, (void *) bep);
    pthread_detach(chld_thr);
}

// add a uri to the system config
static void
addSystemUri(system_statsproxy_settings_t *sys, const char *uri, callback_t cb)
{
    struct confed_uri *u;
    u = (struct confed_uri *) calloc(1, sizeof *u);
    alloc_fail_check(u);
    u->uri = strdup(uri);
    alloc_fail_check(u->uri);
    u->cb = cb;
    TAILQ_INSERT_TAIL(&sys->uris, u, next);
}

// add a uri to the global config
void
addGlobalUri(global_statsproxy_settings_t *global, char *uri)
{
    struct confed_uri *u;
    u = (struct confed_uri *) calloc(1, sizeof *u);
    alloc_fail_check(u);
    u->uri = strdup(uri);
    alloc_fail_check(u->uri);
    u->cb = statsCallback;
    TAILQ_INSERT_TAIL(&global->uris, u, next);
}

// add a uri to the local config
void
addLocalUri(local_statsproxy_settings_t *local, char *uri)
{
    struct confed_uri *u;
    u = (struct confed_uri *) calloc(1, sizeof *u);
    alloc_fail_check(u);
    u->uri = strdup(uri);
    alloc_fail_check(u->uri);
    u->cb = statsCallback;
    TAILQ_INSERT_TAIL(&local->uris, u, next);
}

static void
addSystemUris(system_statsproxy_settings_t *sys)
{
    addSystemUri(sys, "reporter", reporterCallback);
    addSystemUri(sys, "top-keys", reporterCallback);
    addSystemUri(sys, "top-keys-gets", reporterCallback);
    addSystemUri(sys, "top-keys-sets", reporterCallback);
    addSystemUri(sys, "top-keys-all", reporterCallback);
    addSystemUri(sys, "top-keys-select", reporterCallback);
    addSystemUri(sys, "top-clients-key", reporterCallback);
    addSystemUri(sys, "top-clients-ops", reporterCallback);
    addSystemUri(sys, "mcr-config", reporterCallback);
    addSystemUri(sys, "mcr-enable", reporterCallback);
    addSystemUri(sys, "mcr-disable", reporterCallback);
    addSystemUri(sys, "logo.png", imageCallback);
}

void
addProxy(struct settings *settings)
{
    backend_t *bep;
    struct confed_uri *confed_uri_entry;
    struct uri_entry *entry;

    bep = backend_new(&settings->global, &settings->local);

    if (bep == NULL) {
        exit(1);
    }

    // add locals first - they'll be found in priority
    TAILQ_FOREACH(confed_uri_entry, &settings->local.uris, next) {
        entry = (struct uri_entry *) calloc(1, sizeof *entry);
        alloc_fail_check(entry);
        TAILQ_INIT(&entry->stats);
        entry->uri = strdup(confed_uri_entry->uri);
        alloc_fail_check(entry->uri);
        entry->cb = confed_uri_entry->cb;
        TAILQ_INSERT_TAIL(&bep->uris, entry, next);
    }
    TAILQ_FOREACH(confed_uri_entry, &settings->global.uris, next) {
        entry = (struct uri_entry *) calloc(1, sizeof *entry);
        alloc_fail_check(entry);
        TAILQ_INIT(&entry->stats);
        entry->uri = strdup(confed_uri_entry->uri);
        alloc_fail_check(entry->uri);
        entry->cb = confed_uri_entry->cb;
        TAILQ_INSERT_TAIL(&bep->uris, entry, next);
    }
    bep->config = settings;
    TAILQ_INSERT_TAIL(&settings->proxies, bep, next);

}

// start frontend and backend servers
static void
startProxies(struct settings *settings)
{
    struct backend_entries   *proxies = &settings->proxies;
    backend_t                *bep;
    struct uri_entry         *entry;

    TAILQ_FOREACH(bep, proxies, next) {
        proxylog(LOG_INFO, "%s:%d -> %s:%d",
                bep->settings.fronthost,
                bep->settings.frontport,
                bep->settings.backhost,
                bep->settings.backport);
        proxylog(LOG_INFO, "polling interval: %dms",
                bep->settings.pollfreq_ms);
        proxylog(LOG_INFO, "webpage refresh interval: %dms",
                bep->settings.refreshfreq_ms);
        proxylog(LOG_INFO, "connect/read/write: %d/%d/%dms",
                bep->settings.connect_ms,
                bep->settings.read_ms,
                bep->settings.write_ms);
        TAILQ_FOREACH(entry, &bep->uris, next) {
            proxylog(LOG_INFO, "    uri: %s", entry->uri);
        }
        startBackendServer(bep);
        startFrontendServer(bep);
    }
}

// ext for reconfigure
static void hup_handler(int sig)
{
    _exit(EXIT_RECONFIGURE);
}

// initialize the service
static void
statsproxy_init(void)
{
    // will have a fancier reconfig handler later
    signal(SIGHUP, hup_handler);
    // don't let disconnects ruin the party
    signal(SIGPIPE, SIG_IGN);
}

static void
usage(char *who)
{
    fprintf(stderr, "usage: %s -F <config file>\n", who);
    fprintf(stderr, "A sample config file looks like this: ");
    fprintf(stderr, "(time intervals are in seconds)\n\n");
    
    fprintf(stderr,
"memcache-stats-proxy-settings {\n"
"    uri \"\";\n"
"    uri \"health\";\n"
"    uri \"memory\";\n"
"    uri \"items\";\n"
"    uri \"storage\";\n"
"    uri \"slabs\";\n"
"    uri \"sizes\";\n"
"    uri \"replication\";\n"
"    proxy-mapping {\n"
"        front-end = \"mc-vip-1:8080\";\n"
"        back-end = \"mc-vip-1:11211\";\n"
"        timeout = 5;\n"
"        poll-interval = 10;\n"
"        webpage-refresh-interval = 10;\n"
"        memcache-reporter = \"off\";\n"
"    }\n"
"    proxy-mapping {\n"
"        front-end = \"mc-vip-2:8080\";\n"
"        back-end = \"mc-vip-2:11211\";\n"
"        timeout = 5;\n"
"        poll-interval = 10;\n"
"        webpage-refresh-interval = 10;\n"
"        memcache-reporter = \"off\";\n"
"    }\n"
"}\n");
    fprintf(stderr, "A SIGHUP will cause the process to exit for "
                    "reconfiguration.\n");
    exit(1);
}

extern int yyparse(FILE *fp, struct settings *);

// statsproxy main
int
main(int argc, char **argv) 
{
    int                rc;
    struct settings    settings;

    if (argc != 3 || strcmp(argv[1], "-F") != 0) {
        usage(argv[0]);
    }

    statsproxy_init();

    // Open syslog.
    openlog("statsproxy", LOG_PID, LOG_USER);

    memset(&settings, 0, sizeof settings);

    TAILQ_INIT(&settings.sys.uris);
    TAILQ_INIT(&settings.global.uris);
    TAILQ_INIT(&settings.local.uris);
    TAILQ_INIT(&settings.proxies);

    addSystemUris(&settings.sys);
    settings.sys.reporterAddr = strdup("127.0.0.1");
    settings.sys.reporterPort = 23357;

    char *filename = argv[2];
    FILE *fp;

    fp = fopen(filename, "r");
    if (fp == NULL) {
        perror(filename);
        return -1;
    }

    rc = yyparse(fp, &settings);
    if (rc == 1) {
        fprintf(stderr, "Failed to process file %s\n", filename);
    }

    fclose(fp);

    startProxies(&settings);

    pause();

    // Close syslog.
    //
    closelog();

    exit (0);
}
