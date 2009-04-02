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
%{
/*
 * settings_parser.y
 *
 * A parser for parsing memcached settings file.
 */

/*
 * TODO
 * -- negative numbers are not parsed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "queue.h"
#include "statsproxy.h"

#define YYERROR_VERBOSE
#define YYPRINT
#define YYLEX_PARAM fp

typedef char *charptr;

static int yylex_lineno = 1;
static int yylex(FILE *fp);
static int yyerror(FILE *fp, struct settings *settings, const char *message);

%}

%token-table

%token INTEGER
%token FLOAT
%token STRING
%token CHAR

%union {
    size_t int_val;
    charptr string_val;
    double double_val;
    char char_val;
}

%type <int_val> INTEGER
%type <double_val> FLOAT
%type <string_val> STRING
%type <char_val> CHAR

%parse-param {FILE *fp}
%parse-param {struct settings *settings}

%%



config : "memcache-stats-proxy-settings" '{' statements '}'
    ;

statements : /* empty */
    | statements1
    ;

statements1 : statement
    | statements1 statement
    ;

statement : "uri" STRING ';'
            {
                addGlobalUri(&settings->global, $2);
            }
    | proxy_mapping_block
    ;

proxy_mapping_block : "proxy-mapping" '{'
            {
                settings->local.connect_ms = DEFAULT_TIMEOUT_MS;
                settings->local.read_ms = DEFAULT_TIMEOUT_MS;
                settings->local.write_ms = DEFAULT_TIMEOUT_MS;
                settings->local.pollfreq_ms = DEFAULT_POLL_FREQ_MS;
                settings->local.refreshfreq_ms = DEFAULT_WEBPAGE_REFRESH_FREQ_MS;
            }
              proxy_mapping_statements
            {
                int failures = 0;

                if (settings->local.frontport == 0) {
                    fprintf(stderr, "Missing front-end at line %u\n",
                            yylex_lineno);
                    failures++;
                }

                if (settings->local.backport == 0) {
                    fprintf(stderr, "Missing back-end at line %u\n",
                            yylex_lineno);
                    failures++;
                }

                if (failures) {
                    YYERROR;
                }

                /*
                 * Add the proxy-mapping to the list.
                 */
                addProxy(settings);
            }
              '}'
;

proxy_mapping_statements : /* empty */
    | proxy_mapping_statements1
    ;

proxy_mapping_statements1 : proxy_mapping_statement
    | proxy_mapping_statements1 proxy_mapping_statement
    ;


proxy_mapping_statement : "front-end" '=' STRING ';'
            {
                char *ch;

                ch = strchr($3, ':');
                if (ch == NULL) {
                    fprintf(stderr, "Bad syntax for front-end\n");
                    YYABORT;
                }
                *ch = '\0';
                settings->local.fronthost = strdup($3);
                settings->local.frontport = strtoul(ch + 1, NULL, 10);
            }
    | "back-end" '=' STRING ';'
            {
                char *ch;

                ch = strchr($3, ':');
                if (ch == NULL) {
                    fprintf(stderr, "Bad syntax for back-end\n");
                    YYABORT;
                }
                *ch = '\0';
                settings->local.backhost = strdup($3);
                settings->local.backport = strtoul(ch + 1, NULL, 10);
            }
    | "timeout" '=' INTEGER ';'
            {
                settings->local.connect_ms = $3 * 1000;
                settings->local.read_ms = $3 * 1000;
                settings->local.write_ms = $3 * 1000;
                if (settings->local.connect_ms <= 0) {
                    fprintf(stderr, "timeout value should be greater "
                            "than 0\n");
                    YYABORT;
                }
            }
    | "poll-interval" '=' INTEGER ';'
            {
                settings->local.pollfreq_ms = $3 * 1000;
                if (settings->local.pollfreq_ms <= 0) {
                    fprintf(stderr, "poll-interval value should be greater "
                            "than 0\n");
                    YYABORT;
                }
            }
    | "webpage-refresh-interval" '=' INTEGER ';'
            {
                settings->local.refreshfreq_ms = $3 * 1000;
                if (settings->local.refreshfreq_ms <= 0) {
                    fprintf(stderr, "webpage-refresh-interval value should "
                            "be greater than 0\n");
                    YYABORT;
                }
            }
    | "memcache-reporter" '=' STRING ';'
            {
                settings->local.reporter = strdup($3);
                if (strcmp(settings->local.reporter, "off") != 0 && 
                    strcmp(settings->local.reporter, "view") != 0 &&
                    strcmp(settings->local.reporter, "modify") != 0) {
                    fprintf(stderr, "Bad syntax for memcache-reporter; expecting "
                            "[off | view | modify] \n");
                    YYABORT;
                }
            }
    ;

%%

#define YYLEX_BUFFER_SIZE       4096

static char *yylex_buffer = NULL;

int
yylex(FILE *fp)
{
    char *yylex_bufptr;
    int yylex_buf_consumed;
    int processing_comment;
    int ch;

    if (yylex_buffer == NULL) {
        yylex_buffer = (char *) malloc(YYLEX_BUFFER_SIZE);
        if (yylex_buffer == NULL) {
            return -1;
        }
    }

    /*
     * Eat any leading white space.
     */
    processing_comment = 0;
    while (1) {
        ch = fgetc(fp);
        if (ch == EOF) {
            return 0;
        }

        if (ch == '\n') {
            yylex_lineno++;
            if (processing_comment) {
                processing_comment = 0;
            }
        }

        if (!processing_comment) {
            if (ch == '#') {
                processing_comment = 1;
            } else if (!isspace(ch)) {
                break;
            }
        }
    }

    /*
     * See if this is a CHAR.
     */
    if (ch == '\'') {
        ch = fgetc(fp);
        if (ch == EOF) {
            return -1;
        }

        if (ch == '\\') {
            /*
             * It was an escape, get the real character.
             */
            ch = fgetc(fp);
            if (ch == EOF) {
                return -1;
            }
        }

        yylval.char_val = ch;

        /*
         * Get the close quote.
         */
        ch = fgetc(fp);
        if (ch == EOF || ch != '\'') {
            return -1;
        }

        return CHAR;
    }

    /*
     * See if this is a STRING.
     */
    if (ch == '"') {
        char *t = yylex_buffer;
        int string_size = 0;
        int done = 0, escape = 0;

        while (!done && (string_size < YYLEX_BUFFER_SIZE - 1)) {
            ch = fgetc(fp);
            if (ch == EOF) {
                done = 1;
            } else if (!escape && ch == '"') {
                done = 1;
            } else if (!escape && ch == '\\') {
                escape = 1;
            } else {
                escape = 0;
                *t++ = ch;
                string_size++;
            }
        }

        if (ch == EOF || (string_size >= YYLEX_BUFFER_SIZE - 1)) {
            return -1;
        }

        *t = '\0';
        yylval.string_val = yylex_buffer;
        return STRING;
    }

    /*
     * Handle numbers.
     */
    if (isdigit(ch) || ch == '.') {
        int number_type;
        int base;
        size_t integer_value = 0;
        double double_value = 0.0;
        int saw_decimal = 0;
        double denom = 10.0;

        /*
         * Assume integer until we see a decimal point.
         */
        number_type = INTEGER;
        if (ch == '0') {
            ch = fgetc(fp);
            if (ch == EOF) {
                yylval.int_val = 0;
                return INTEGER;
            }

            if (ch == 'x' || ch == 'X') {
                /*
                 * We have a 0x (or 0X) prefix, treat as hexadecimal.
                 */ 
                base = 16;
                ch = fgetc(fp);
            } else {
                /*
                 * We don't have 0x (or 0X), treat as octal.
                 */
                ungetc(ch, fp);
                base = 8;
                ch = '0';
            }
        } else {
            /*
             * Treat as decimal.
             */
            base = 10;

        }

        /* XXX '.' all by itself is a punct */
        /* XXX negative numbers */

        /* 00000.0 */
        /* 00177.0 */

        integer_value = 0;
        double_value = 0.0;
        yylex_bufptr = yylex_buffer;
        yylex_buf_consumed = 0;
        while (ch != EOF && (isdigit(ch) || (!saw_decimal && ch == '.'))) {
            if (ch == '.') {
                if (base == 16) {
                    /*
                     * If parsing a hex number, a period is not a
                     * decimal, and the number is over.
                     */
                    ungetc(ch, fp);
                    yylval.int_val = integer_value;
                    return number_type;
                }

                if (base == 8) {
                    char *s;

                    /*
                     * If parsing an octal number, a period means
                     * that this really was a decimal floating point
                     * number and we have to start over.
                     */
                    base = 10;
                    integer_value = 0;
                    for (s = yylex_buffer; s != yylex_bufptr; s++) {
                        integer_value = integer_value * base + (*s - '0');
                    }
                }
                number_type = FLOAT;
                double_value = integer_value;
                saw_decimal = 1;
                ch = fgetc(fp);
                continue;
            }

            /*
             * Save the character.
             */
            if (yylex_buf_consumed >= YYLEX_BUFFER_SIZE) {
                /*
                 * Must have been a ridiculously long number.
                 */
                return -1;
            }
            *yylex_bufptr++ = ch;
            yylex_buf_consumed++;

            switch (base) {
            case 8:
                if (ch >= '8') {
                    /*
                     * Not a valid octal number.
                     */
                    return -1;
                }
                integer_value *= base;
                integer_value += ch - '0';
                break;
            case 10:
                if (ch > '9') {
                    /*
                     * Not a valid decimal number.
                     */
                    return -1;
                }
                if (number_type == INTEGER) {
                    integer_value *= base;
                    integer_value += ch - '0';
                } else {
                    if (saw_decimal) {
                        /* fractional part */
                        double_value += (ch - '0') / denom;
                        denom *= 10.0;
                    } else {
                        double_value *= base;
                        double_value += ch - '0';
                    }
                }
                break;
            case 16:
                integer_value *= base;
                if (ch < '9') {
                    integer_value += ch - '0';
                } else {
                    ch = tolower(ch);
                    if (ch >= 'a' && ch <= 'f') {
                        integer_value += 10 + ch - 'a';
                    } else {
                        /*
                         * Not a valid hexadecimal number.
                         */
                        return -1;
                    }
                }
                break;
            default:
                return -1;
            }

            ch = fgetc(fp);
        }
        if (ch != EOF) {
            ungetc(ch, fp);
        }

        if (number_type == INTEGER) {
            yylval.int_val = integer_value;
        } else {
            yylval.double_val = double_value;
        }
        return number_type;
    }
    
    /*
     * Handle keywords.
     */
    if (isalpha(ch)) {
        int yylex_buffer_index;
        int i;

        yylex_buffer_index = 0;
        while (ch != EOF && (isalnum(ch) || ch == '-' || ch == '_')) {
            yylex_buffer[yylex_buffer_index++] = ch;
            if (yylex_buffer_index == YYLEX_BUFFER_SIZE - 1) {
                yyerror(fp, NULL, "Keyword too large");
            }
            ch = fgetc(fp);
        }
        if (ch != EOF) {
            ungetc(ch, fp);
        }

        /*
         * Find the token.
         */
        yylex_buffer[yylex_buffer_index] = '\0';
        for (i = 0; i < YYNTOKENS; i++) {
            if ((yytname[i] != 0) &&
                (yytname[i][0] == '"') &&
                (strncmp(yytname[i] + 1, yylex_buffer,
                         yylex_buffer_index) == 0) &&
                (yytname[i][yylex_buffer_index + 1] == '"') &&
                (yytname[i][yylex_buffer_index + 2] == '\0')) {
                return yytoknum[i];
            }
        }

        /*
         * Unknown token.
         */
        return -1;
    }

    /*
     * 
     * Return any punctuation character as is.
     */
    if (ispunct(ch)) {
        return ch;
    }

    return -1;
}

int
yyerror(FILE *fp, struct settings *settings, const char *message)
{
    fprintf(stderr, "Error: %s on line %u\n", message, yylex_lineno);
    exit(1);
}
