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
#include <unistd.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "queue.h"
#include "statsproxy.h"
#include "proxylog.h"
#include "mcr_web.h"
#include "uristrings.h"

#define PROC_NET_TCP "/proc/net/tcp"
#define PROC_NET_UDP "/proc/net/udp"
#define LINESZ 1024
#define TOKSZ 255

static void
mcr_emptyList(mcr_entries_t *el)
{
    struct mcr_entry    *entry;
    struct mcr_entry    *tmp;

    entry = TAILQ_FIRST(&el->entries);
    while (entry != NULL) {
        tmp = TAILQ_NEXT(entry, next);
        free((void *) entry->host);
        free((void *) entry->dotquad);
        entry = tmp;
    }
    TAILQ_INIT(&el->entries);
}

static void
mcr_emptyTopKeys(tk_entries_t *tkl)
{
    struct tk_entry    *entry;
    struct tk_entry    *tmp;

    entry = TAILQ_FIRST(&tkl->entries);
    while (entry != NULL) {
        tmp = TAILQ_NEXT(entry, next);
        free((void *) entry->key);
        free(entry);
        entry = tmp;
    }
    TAILQ_INIT(&tkl->entries);
}

static void
mcr_emptyTopClients(tc_entries_t *tcl)
{
    struct tc_entry    *entry;
    struct tc_entry    *tmp;

    entry = TAILQ_FIRST(&tcl->entries);
    while (entry != NULL) {
        tmp = TAILQ_NEXT(entry, next);
        free((void *) entry->host);
        free((void *) entry->dotquad);
        free(entry);
        entry = tmp;
    }
    TAILQ_INIT(&tcl->entries);
}

static int
isInList(uint32_t addr, mcr_entries_t *el)
{
    struct mcr_entry     *entry;

    TAILQ_FOREACH(entry, &el->entries, next) {
        if (entry->addr == addr) {
            return TRUE;
        }
    }
    return FALSE;
}

static int
mcr_connectRemote(const struct sockaddr_in *mcr, int *fd, FILE **fp)
{
    int err = -1;

    *fd = socket(AF_INET, SOCK_STREAM, 0);
    bail_require_msg(*fd >= 0, "%s: socket() call failed: %s",
                     __FUNCTION__, strerror(errno));

    err = connect(*fd, (struct sockaddr *) mcr, sizeof *mcr);
    bail_error_msg(err, "%s: connect() call failed: %s",
                                 __FUNCTION__, strerror(errno));
    *fp = fdopen(*fd, "rw+");
    bail_require_msg(*fp != NULL, "%s: fdopen() call failed: %s",
                     __FUNCTION__, strerror(errno));

    setlinebuf(*fp);
bail:
    return err;
}

// update the list of running memcache instances
static int 
mcr_updateInstances(mcr_entries_t *el, const struct sockaddr_in *mcr)
{
    int                    err = -1;
    int                    fd = -1;
    struct mcr_entry       *entry;
    FILE                   *fp = NULL;
    char                   line[LINESZ];
    char                   ip[TOKSZ];
    struct sockaddr_in     ip_sa;
    uint32_t               port;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    bail_require_msg(fd >= 0, "%s: socket() call failed: %s",
                              __FUNCTION__, strerror(errno));

    err = mcr_connectRemote(mcr, &fd, &fp);
    bail_error_msg(err, "got could not get remote instances");

    fputs("show instances\r\n", fp);

    while (!feof(fp)) {
        if (fgets(line, LINESZ, fp) == NULL) {
            break;
        }
        if (strcmp(line, "END\r\n") == 0 || strcmp(line, "ERROR\r\n") == 0) {
            break;
        }
        memset(ip, 0, TOKSZ);
        if (sscanf(line, "%*20s %s %d", ip, &port) != 2) {
            continue;
        }

        entry = (struct mcr_entry *) calloc(1, sizeof *entry);
        alloc_fail_check(entry);
        entry->addr = inet_addr(ip);
        ip_sa.sin_addr.s_addr = entry->addr;
        entry->dotquad = strdup(ip);
        alloc_fail_check(entry->dotquad);
        entry->host = strdup(addr2host(&ip_sa));
        alloc_fail_check(entry->host);
        entry->port = port;
        entry->enabled = TRUE;
        TAILQ_INSERT_TAIL(&el->entries, entry, next);
    }

    err = 0;
bail:
    if (fp != NULL) {
        fclose(fp);
    }
    if (fd >= 0) {
        close(fd);
    }
    return err;
}

// update the list of top keys
static int 
mcr_updateTopKeys(tk_entries_t *tkl, char **versionStr,
        const struct sockaddr_in *mcr, const char *op,
        char *dotquad, uint16_t port, int mcrtime, int nkeys)
{
    int                    err = -1;
    int                    fd = -1;
    struct tk_entry        *entry;
    FILE                   *fp = NULL;
    char                   line[LINESZ];
    char                   key[TOKSZ];
    int                    keylen;
    uint64_t               mgets;
    uint64_t               sets;
    uint64_t               all;
    uint64_t               bytes;
    uint64_t               bytes_read;
    uint64_t               bytes_written = 0;

    err = mcr_connectRemote(mcr, &fd, &fp);
    bail_error_msg(err, "could not get top-keys");

    fprintf(fp, "top-keys %s %s %d %d %d\r\n",
            op, dotquad, port, mcrtime, nkeys);

    while (!feof(fp)) {
        if (fgets(line, LINESZ, fp) == NULL) {
            break;
        }
        if (strcmp(line, "END\r\n") == 0) {
            break;
        }

        if (strcmp(line, "ERROR\r\n") == 0) {
            err = -1;
            goto bail;
        }

        // STAT version 180 time Mon Feb 23 03:33:57 2009
        if (strncmp(line, "STAT version", 12) == 0) {
            *versionStr = strdup(line);
            continue;
        }

        // STAT version 180 time Mon Feb 23 03:33:57 2009
        if (strncmp(line, "STAT key key-length", 19) == 0) {
            continue;
        }

        memset(key, 0, TOKSZ);
        if (sscanf(line, "%*s %250s %d %*s "
                    "%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64"mb"
                    "(%"PRIu64"mb/%"PRIu64"mb)",
                   key, &keylen, &mgets, &sets, &all,
                   &bytes, &bytes_read, &bytes_written) == 0) {
            err = -1;
            goto bail;
        }

        entry = (struct tk_entry *) calloc(1, sizeof *entry);
        alloc_fail_check(entry);
        entry->key = strdup(key);
        alloc_fail_check(entry->key);
        entry->keylen = keylen;
        entry->gets = mgets;
        entry->sets = sets;
        entry->all = all;
        entry->bytes = bytes;
        entry->bytes_read = bytes_read;
        entry->bytes_written = bytes_written;
        TAILQ_INSERT_TAIL(&tkl->entries, entry, next);
    }

    err = 0;
bail:
    if (fp != NULL) {
        fclose(fp);
    }
    if (fd >= 0) {
        close(fd);
    }
    return err;
}

// update the list of top clients
static int 
mcr_updateTopClients(tc_entries_t *tcl, char **versionStr,
        const struct sockaddr_in *mcr, char *dotquad, uint16_t port,
        char *key, int mcrtime, int nclnts)
{
    int                    err = -1;
    int                    fd = -1;
    struct tc_entry        *entry;
    FILE                   *fp = NULL;
    char                   line[LINESZ];
    struct sockaddr_in     ip_sa;
    char                   tcip[TOKSZ];
    uint16_t               tcport;
    uint64_t               count = 0;

    err = mcr_connectRemote(mcr, &fd, &fp);
    bail_error_msg(err, "could not get top-clnts");

    if (strcmp(key, "ops") == 0) {
        fprintf(fp, "top-clnts %s %s %d %d %d\r\n",
            key, dotquad, port, mcrtime, nclnts);
    } else {
        fprintf(fp, "top-clnts %s %s %d %d\r\n",
            key, dotquad, port, mcrtime);
    }

    while (!feof(fp)) {
        if (fgets(line, LINESZ, fp) == NULL) {
            break;
        }
        if (strcmp(line, "END\r\n") == 0) {
            break;
        }

        if (strcmp(line, "ERROR\r\n") == 0) {
            err = -1;
            goto bail;
        }

        // STAT version 180 time Mon Feb 23 03:33:57 2009
        if (strncmp(line, "STAT version", 12) == 0) {
            *versionStr = strdup(line);
            continue;
        }

        // STAT version 180 time Mon Feb 23 03:33:57 2009
        if (strncmp(line, "STAT client", 11) == 0) {
            continue;
        }

        memset(tcip, 0, TOKSZ);
        if (sscanf(line, "%*s %250[^ :]:%hu %"PRIu64"",
                   tcip, &tcport, &count) == 0) {
            err = -1;
            goto bail;
        }

        entry = (struct tc_entry *) calloc(1, sizeof *entry);
        alloc_fail_check(entry);
        entry->addr = inet_addr(tcip);
        ip_sa.sin_addr.s_addr = entry->addr;
        entry->dotquad = strdup(inet_ntoa(ip_sa.sin_addr));
        alloc_fail_check(entry->dotquad);
        entry->host = strdup(addr2host(&ip_sa));
        alloc_fail_check(entry->host);
        entry->port = tcport;
        entry->count = count;
        TAILQ_INSERT_TAIL(&tcl->entries, entry, next);
    }

    err = 0;
bail:
    if (fp != NULL) {
        fclose(fp);
    }
    if (fd >= 0) {
        close(fd);
    }
    return err;
}

static int
mcr_updateConfigedInstances(mcr_entries_t *el, struct backend_entries *proxies)
{
    int                      err = 0;
    struct mcr_entry         *entry;
    backend_t                *bep;
    struct in_addr           in;
    struct sockaddr_in       ip_sa;

    TAILQ_FOREACH(bep, proxies, next) {

        if (isInList(bep->settings.backaddr, el)) {
            // don't add dupes
            continue;
        }
        entry = (struct mcr_entry *) calloc(1, sizeof *entry);
        alloc_fail_check(entry);

        entry->addr = bep->settings.backaddr;
        in.s_addr = entry->addr;
        entry->dotquad = strdup(inet_ntoa(in));
        alloc_fail_check(entry->dotquad);

        ip_sa.sin_addr.s_addr = entry->addr;
        entry->host = strdup(addr2host(&ip_sa));
        alloc_fail_check(entry->host);

        entry->port = bep->settings.backport;
        entry->enabled = FALSE;
        TAILQ_INSERT_TAIL(&el->entries, entry, next);
    }
    return err;
}

// add or delete an instance 
static int 
mcr_instanceOp(const char *op, char *addr, uint16_t port,
               const struct sockaddr_in *mcr)
{
    int                      err = -1;
    int                      fd = -1;
    FILE                     *fp = NULL;
    char                     line[LINESZ];

    err = mcr_connectRemote(mcr, &fd, &fp);
    bail_error_msg(err, "could not run remote op");

    if (strcmp(op, "del-all") == 0) {
        fprintf(fp, "dell-all\n");
    } else {
        fprintf(fp, "%s %s %d\r\n", op, addr, port);
    }

    if (fgets(line, LINESZ, fp) == NULL) {
        err = -1;
        bail_force_msg("%s: fgets failed", __FUNCTION__);
    }

    if (strcmp(line, "END\r\n") == 0) {
        err = 0;
    } else {
        err = -1;
    }

bail:
    if (fp != NULL) {
        fclose(fp);
    }
    if (fd >= 0) {
        close(fd);
    }
    return err;
}

static char *
actions(struct mcr_entry *entry)
{
    char                           *actionUri;
    const char                     *action;

    action = entry->enabled ? "disable" : "enable";
    actionUri = (char *) calloc(1, LINESZ);
    alloc_fail_check(actionUri);
    snprintf(actionUri, LINESZ,
             "<a href=\"mcr-%s?addr=%s&port=%d\">%s&nbsp;reporting</a>",
             action, entry->dotquad, entry->port, action);
    return actionUri;
}

static void
write_mcr_css(FILE *http)
{
    fprintf(http, 
"<style type=\"text/css\">"
"table.config {"
"    border-width: 1px;"
"    border-spacing: 1px;"
"    border-style: inset;"
"    border-color: gray;"
"    border-collapse: separate;"
"    background-color: white;"
"}"
"table.config th {"
"    border-width: 1px;"
"    text-align: left;"
"    padding: 1px;"
"    border-style: inset;"
"    border-color: gray;"
"    -moz-border-radius: ;"
"}"
"table.config tr.d0 td {"
"    border-width: 1px;"
"    text-align: left;"
"    padding: 1px;"
"    border-style: inset;"
"    background-color: white;"
"    -moz-border-radius: ;"
"}"
"table.config tr.d1 td {"
"    border-width: 1px;"
"    text-align: left;"
"    padding: 1px;"
"    border-style: inset;"
"    background-color: rgb(240, 240, 240);"
"    -moz-border-radius: ;"
"}"
"</style>");
}

#define NS_CACHE_TIME 5*60 // seconds to cache name service info
static int
mcr_getAddr(struct settings *config, struct sockaddr_in *mcrAddr)
{
    int                            err = 0;
    static time_t                  nsValidTime = 0;
    static struct sockaddr_in      mcr;
    time_t                         now;

    memset(&mcr, 0, sizeof mcr);
    now = time(NULL);
    if (now - nsValidTime > NS_CACHE_TIME || mcr.sin_port == 0) {
        // get a dns freshen-up for the reporter host
        nsValidTime = now;
        memset(&mcr, 0, sizeof mcr);
        err = host2addr(config->sys.reporterAddr, &mcr);
        if (err) {
            goto bail;
        }
        mcr.sin_family = AF_INET;
        mcr.sin_port = htons(config->sys.reporterPort);
    }
    *mcrAddr = mcr;
bail:
    return err;
}


int
mcr_op(proxyclient_t *clnt, const char *op, char *addr, uint16_t port)
{
    int                            err = 0;
    struct sockaddr_in             mcrAddr;
    FILE                           *http = clnt->fp;
    struct settings                *config = clnt->bep->config;

    err = mcr_getAddr(config, &mcrAddr);
    if (err) {
        fprintf(http, "<b>Error: did not find reporter host %s</b>",
                   config->sys.reporterAddr);
        bail_error_msg(err, "Did not find reporter host %s",
                   config->sys.reporterAddr);
    }
    err = mcr_instanceOp(op, addr, port, &mcrAddr);
bail:
    return err;
}

int
write_html_mcr_top_clients(proxyclient_t *clnt, char *dotquad, uint16_t port,
                           char *key, int mcrtime, int nclnts)

{
    int                            err = 0;
    struct sockaddr_in             mcrAddr;
    struct sockaddr_in             ip_sa;
    struct settings                *config = clnt->bep->config;
    int                            ranking = 0;
    tc_entries_t                   tcl;
    struct tc_entry                *entry;
    FILE                           *http = clnt->fp;
    char                           *versionStr = NULL;
    char                           *host;

    err = mcr_getAddr(config, &mcrAddr);
    if (err) {
        fprintf(http, "<b>Error: did not find reporter host %s</b>",
                   config->sys.reporterAddr);
        bail_error_msg(err, "Did not find reporter host %s",
                   config->sys.reporterAddr);
    }
    memset(&ip_sa, 0, sizeof ip_sa);
    ip_sa.sin_addr.s_addr = inet_addr(dotquad);
    host = strdup(addr2host(&ip_sa));

    memset(&tcl, 0, sizeof tcl);
    TAILQ_INIT(&tcl.entries);

    err = mcr_updateTopClients(&tcl, &versionStr, &mcrAddr, dotquad, port, 
                            key, mcrtime, nclnts);

    bail_error_msg(err, "could not get key information from reporter host %s",
                         config->sys.reporterAddr);

    write_mcr_css(http);
    char opLabel[LINESZ];
    if (strcmp(key, "ops") == 0) {
        sprintf(opLabel, " by ops");
    } else {
        sprintf(opLabel, "for key <i><font size =\"-2\">%s</font></i>", key);
    }
    fprintf(http, "<b>Top Clients <b>%s</b> on memcache %s:%hu - "
                  "<font size=\"-1\">%s</font></b><br><br>\r\n",
                  opLabel, host, port, versionStr == NULL ? "" : versionStr);
    free(host);
    if (versionStr != NULL) {
        free(versionStr);
    }
    fprintf(http, "<table class=\"config\" width=\"50%%\">");
    fprintf(http, "<tr>");
    fprintf(http, "<th>Ranking</th>"
                  "<th>Client&nbsp;IP&nbsp;Address</th>"
                  "<th>Client&nbsp;DNS&nbsp;Name</th>"
                  "<th>Src&nbsp;Port</th>"
                  "<th>Access&nbsp;Count</th>");
    fprintf(http, "</tr>");
    TAILQ_FOREACH(entry, &tcl.entries, next) {
        ranking++;

        fprintf(http, "<tr class=\"d%d\">", ranking % 2);
        fprintf(http, "<td>%d</td>"
                      "<td>%s</td>"
                      "<td>%s</td>"
                      "<td>%hu</td>"
                      "<td>%"PRIu64"</td>",
                      ranking,
                      entry->dotquad,
                      entry->host,
                      entry->port,
                      entry->count);
        fprintf(http, "</tr>\r\n");
    }
    fprintf(http, "</table>\r\n");
    mcr_emptyTopClients(&tcl);
bail:
    return err;
}

static char *
key_href(const char *base, const char *dotquad, uint16_t port, const char *key)
{
    char *encodedUri;
    char *keyHREF = (char *) calloc(1, LINESZ);
    alloc_fail_check(keyHREF);

    encodedUri = uri_encode(key);
    snprintf(keyHREF, LINESZ, "<a href=\"%s?addr=%s&port=%hu&key=%s\">",
                              base, dotquad, port, key);
    free(encodedUri);
    return keyHREF;
}

int
write_html_mcr_top_keys(proxyclient_t *clnt, char *dotquad, uint16_t port,
                        char *op, int mcrtime, int nkeys)
{
    int                            err = 0;
    struct sockaddr_in             mcrAddr;
    struct sockaddr_in             ip_sa;
    struct settings                *config = clnt->bep->config;
    int                            ranking = 0;
    tk_entries_t                   tkl;
    struct tk_entry                *entry;
    char                           *keyHREF;
    FILE                           *http = clnt->fp;
    char                           *versionStr = NULL;
    char                           *host;
    const char                     *keyop;
    int                            doSelect = FALSE;

    err = mcr_getAddr(config, &mcrAddr);
    if (err) {
        fprintf(http, "<b>Error: did not find reporter host %s</b>",
                   config->sys.reporterAddr);
        bail_error_msg(err, "Did not find reporter host %s",
                   config->sys.reporterAddr);
    }
    memset(&ip_sa, 0, sizeof ip_sa);
    ip_sa.sin_addr.s_addr = inet_addr(dotquad);
    host = strdup(addr2host(&ip_sa));

    memset(&tkl, 0, sizeof tkl);
    TAILQ_INIT(&tkl.entries);

    // this allow us to use the all-keys dialog as a selection pad for
    // top-clients-by-key type operations
    if (strcmp(op, "select") == 0) {
        doSelect = TRUE;
        keyop = "all";
    } else {
        keyop = op;
    }
    err = mcr_updateTopKeys(&tkl, &versionStr, &mcrAddr, keyop, dotquad, port, 
                            mcrtime, nkeys);
    bail_error_msg(err, "could not get key information from reporter host %s",
                         config->sys.reporterAddr);

    write_mcr_css(http);
    if (doSelect) {
        fprintf(http, "<b>Select key below for client use information...</b>"
                      "<br><br>");
    } else {
        fprintf(http, "<b>Top Keys by <b>%s</b> for %s:%hu - "
                  "<font size=\"-1\">%s</font></b><br><br>\r\n",
                  op, host, port, versionStr == NULL ? "" : versionStr);
    }
    free(host);
    if (versionStr != NULL) {
        free(versionStr);
    }
    fprintf(http, "<table class=\"config\" width=\"50%%\">");
    fprintf(http, "<tr>");
    fprintf(http, "<th>Ranking</th>"
                  "<th>Key</th>"
                  "<th>Key&nbsp;Length</th>"
                  "<th>#&nbsp;Gets</th>"
                  "<th>#&nbsp;Sets</th>"
                  "<th>#&nbsp;All</th>"
                  "<th>Bytes&nbsp;(mb)</th>"
                  "<th>Read&nbsp;(mb)</th>"
                  "<th>Written&nbsp;(mb)</th>");
    fprintf(http, "</tr>\r\n");
    TAILQ_FOREACH(entry, &tkl.entries, next) {
        ranking++;

        keyHREF = key_href("/top-clients-ops", dotquad, port, entry->key);
        fprintf(http, "<tr class=\"d%d\">", ranking % 2);
        fprintf(http, "<td>%d</td>"
                      "<td><font size=\"-2\">%s%s</a><font></td>"
                      "<td>%d</td>"
                      "<td>%"PRIu64"</td>"
                      "<td>%"PRIu64"</td>"
                      "<td>%"PRIu64"</td>"
                      "<td>%"PRIu64"</td>"
                      "<td>%"PRIu64"</td>"
                      "<td>%"PRIu64"</td>",
                      ranking,
                      keyHREF,
                      entry->key,
                      entry->keylen,
                      entry->gets,
                      entry->sets,
                      entry->all,
                      entry->bytes,
                      entry->bytes_read,
                      entry->bytes_written);
        fprintf(http, "</tr>\r\n");
        free(keyHREF);
    }
    fprintf(http, "</table>\r\n");
    mcr_emptyTopKeys(&tkl);
bail:
    return err;
}

int
write_html_mcr_config(proxyclient_t *clnt, char *uri)
{
    int                            err = 0;
    struct sockaddr_in             mcrAddr;
    struct in_addr                 in;
    struct settings                *config = clnt->bep->config;
    local_statsproxy_settings_t    settings = clnt->bep->settings;
    int                            instance = 0;
    mcr_entries_t                  el;
    struct mcr_entry               e;
    struct mcr_entry               *entry = &e;
    FILE                           *http = clnt->fp;
    char                           *action;

    err = mcr_getAddr(config, &mcrAddr);
    if (err) {
        fprintf(http, "<b>Error: did not find reporter host %s</b>",
                   config->sys.reporterAddr);
        bail_error_msg(err, "Did not find reporter host %s",
                   config->sys.reporterAddr);
    }
    memset(&el, 0, sizeof el);
    TAILQ_INIT(&el.entries);

    err = mcr_updateInstances(&el, &mcrAddr);
    bail_error_msg(err, "could not get information from reporter host %s",
             config->sys.reporterAddr);
    err = mcr_updateConfigedInstances(&el, &config->proxies);
    bail_error_msg(err, "could not get configed information");

    write_mcr_css(http);
    fprintf(http, "<b>Memcache Reporter Configuration</b><br><br>");
    fprintf(http, "<table class=\"config\" width=\"50%%\">");
    fprintf(http, "<tr>");
    fprintf(http, "<th>Instance&nbsp;#</th>"
                  "<th>IP&nbsp;Address</th>"
                  "<th>DNS&nbsp;Name</th>"
                  "<th>Port</th>"
                  "<th>Reporting</th>"
                  "<th>Actions</th>\n");
    fprintf(http, "</tr>\r\n");
    TAILQ_FOREACH(entry, &el.entries, next) {

        in.s_addr = settings.backaddr;
        const char *ip_dotquad = inet_ntoa(in);

        /* print only the reporter address that we intend to monitor.
         * */
        if (strcmp(ip_dotquad, entry->dotquad) == 0) {

            instance++;
            fprintf(http, "<tr class=\"d%d\">", instance % 2);

            action = actions(entry);
            fprintf(http, "<td>%d</td>"
                          "<td>%s</td>"
                          "<td>%s</td>"
                          "<td>%d</td>"
                          "<td>%s</td>"
                          "<td>%s</td>\n",
                          instance,
                          entry->dotquad,
                          entry->host,
                          entry->port,
                          entry->enabled ? "on" : "off",
                          action);
            free(action);
            fprintf(http, "</tr>");
        }
    }
    fprintf(http, "</table>");
    mcr_emptyList(&el);
bail:
    return err;
}
