# ========================================================================
#
#  Project   : Makefile for statsproxy 
#
#  Version   : 1.0
#
#  Copyright :
#
#      Software License Agreement (BSD License)
#
#      Copyright (c) 2009, Gear Six, Inc.
#      All rights reserved.
#
#      Redistribution and use in source and binary forms, with or without
#      modification, are permitted provided that the following conditions are
#      met:
#
#      * Redistributions of source code must retain the above copyright
#        notice, this list of conditions and the following disclaimer.
#
#      * Redistributions in binary form must reproduce the above
#        copyright notice, this list of conditions and the following disclaimer
#        in the documentation and/or other materials provided with the
#        distribution.
#
#      * Neither the name of Gear Six, Inc. nor the names of its
#        contributors may be used to endorse or promote products derived from
#        this software without specific prior written permission.The Gear Six logo, 
#        which is provided in the source code and appears on the user interface 
#        to identify Gear Six, Inc. as the originator of the software program, is 
#        a trademark of Gear Six, Inc. and can be used only in unaltered form and 
#        only for purposes of such identification.
#
#      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#      "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#      LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#      A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#      OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#      SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#      LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#      DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#      THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#      (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#      OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# ========================================================================
# 		Notes:
# 		needs bison
# ========================================================================
CC	    = g++
CXXFLAGS= $(CFLAGS)
INC 	=
CFLAGS	= -Wall -g -D__STDC_FORMAT_MACROS -DVERSION=\"v1.0\"
HDRS    = statsproxy.h uristrings.h proxylog.h mcr_web.h
OBJS	= statsproxy.o statsmc.o uristrings.o proxylog.o settings_parser.tab.o mcr_web.o


all: statsproxy

$(OBJS): $(HDRS)

statsproxy: $(OBJS)
	$(CC) -o $@ $(OBJS) -lpthread

settings_parser.tab.c: settings_parser.y
	bison settings_parser.y

clean:
	@\rm -f *.o statsproxy settings_parser.tab.c
