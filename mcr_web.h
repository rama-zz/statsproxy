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

#ifndef _MCR_WEB_H
#define _MCR_WEB_H

#ifdef __cplusplus
extern "C" {
#endif

// one reporter entry
struct mcr_entry {
    TAILQ_ENTRY(mcr_entry)       next;
    char                         *host;     // hostname
    char                         *dotquad;  // hostname
    uint32_t                     addr;      // address
    uint16_t                     port;      // port
    uint8_t                      enabled;   // configured for reporting
};

// list of reporter entries
typedef struct {
    TAILQ_HEAD(mcr_entries, mcr_entry) entries; // listeners
} mcr_entries_t;

// top key entry
struct tk_entry {
    TAILQ_ENTRY(tk_entry)        next;
    char                         *key;
    int16_t                      keylen;
    uint64_t                     gets;
    uint64_t                     sets;
    uint64_t                     all;
    uint64_t                     bytes;
    uint64_t                     bytes_read;
    uint64_t                     bytes_written;
};

// list of top keys
typedef struct {
    TAILQ_HEAD(tk_entries, tk_entry) entries; // top keys
} tk_entries_t;

// top client entry
struct tc_entry {
    TAILQ_ENTRY(tc_entry)        next;
    char                         *host;     // hostname
    char                         *dotquad;  // hostname
    uint32_t                     addr;      // address
    uint16_t                     port;      // port
    uint64_t                     count;     // access count
};

// list of top clients
typedef struct {
    TAILQ_HEAD(tc_entries, tc_entry) entries; // top clients
} tc_entries_t;

int write_html_mcr_config(proxyclient_t *clnt, char *uri);
int mcr_op(proxyclient_t *clnt, const char *op, char *addr, uint16_t port);
int write_html_mcr_top_keys(proxyclient_t *clnt, char *dotquad, uint16_t port,
                            char *op, int mcrtime, int nkeys);
int write_html_mcr_top_clients(proxyclient_t *clnt, char *dotquad,
                               uint16_t port,
                               char *key, int mcrtime, int nclnts);

#define TKTIME 60      // top key reporting time (mins)
#define TKNUM  100     // top key number
#define TCNUM  20      // top clients number
#define TCTIME 60      // top clients reporting time (mins)
#define MCRREFRESH     60*1000

#ifdef __cplusplus
}
#endif

#endif // _MCR_WEB_H */
