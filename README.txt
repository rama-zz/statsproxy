README.txt, v 1.0 2009/3/20

CONTENTS OF THIS FILE
-------------------------------------------------------------------------------
 * Introduction
 * Supported Platforms
 * Required Libraries
 * Installation
 * Configuration
 * Logging
 * Usage

INTRODUCTION
-------------------------------------------------------------------------------
The statsproxy for memcached is a simple, but useful tool that allows you to
use your web browser to get real-time stats for memcached instead of or in
addition to the standard memcached telnet interface.

The statsproxy for memcached provides:

1. A lightweight web view of memcached statistics without any changes or 
additional resource usage in memcached, i.e. stats can be easily published
and viewed for your memcached servers.

2. A fast, buffered telnet view of memcached stats that can be leveraged by
management and reporting systems, like Cacti.  Stats are reported in constant
time irrespective of memcached's load level.

3. A synthetic 'health check' stat that is currently not available in
memcached, where the statsproxy does a periodic set command and checks that the
memcached server handled it correctly.

SUPPORTED PLATFORMS
-------------------------------------------------------------------------------
The statsproxy for memcached was developed for Linux 2.6 and has been tested
on the following distributions:

CentOS 5.2 2.6.18 and later x86_64 bit gcc 4.1.2

Linux 2.6.18-1.2798.fc6 x86_64 gcc 3.4.6 (Red Hat 3.4.6-4)

Linux 2.6.9-1.667smp i686 athlon gcc 3.4.2 (Red Hat 3.4.2-6.fc3)

Linux 2.6.18-53.el5 x86_64 gcc 4.1.2 (Red Hat 4.1.2-42)

Linux 2.6.12-1.1381_FC3smp x86_64 gcc 3.4.4 (Red Hat 3.4.4-2)

REQUIRED LIBRARIES
-------------------------------------------------------------------------------
Requires 'bison'.

INSTALLATION
-------------------------------------------------------------------------------

Installing the statsproxy for memcached:
 
At your command prompt:
> tar -xzvf statsproxy-1.0.tgz
> cd statsproxy-1.0
> make

Starting the statsproxy for memcached:

At your command prompt:
usage: ./statsproxy -F <config file>

For example:
>./statsproxy -F sample.cfg

CONFIGURATION
-------------------------------------------------------------------------------

Every memcached server that you'd like the statsproxy to monitor and report on
requires an entry in the config file. 

A sample config file 'sample.cfg' is attached with the distribution.  You can
modify this file to declare the memcached servers that you wish to use
statsproxy on.

The following is the format of each memcached server entry in the config file:

For example: 

To monitor a memcached server from the web running on ip mc-1 and port 11211
your proxy-mapping entry could look like following:

    proxy-mapping {
        front-end = "mc-1:8080";
        back-end = "mc-1:11211";
        timeout = 10;
        poll-interval = 5;
        webpage-refresh-interval = 10;
        memcache-reporter = "off";
    }

After you start statsproxy, point your browser to mc-1:8080 to monitor your
memcache server stats through the statsproxy web interface. You can also
use telnet to the same address to get a buffered view of stats, including
'stats health'.

'front-end'
IP & port information for proxied view of this memcached server

'back-end'
IP address and port for this memcached server

'poll-interval'
Defines the polling interval in seconds for the memcached server. The
statsproxy will poll the memcache service for stats on this interval.

'timeout'
statsproxy will wait this many seconds to get a stats or set response from
the memcached server.

'webpage-refresh-interval'
Webpage refresh interval in seconds.

'memcache-reporter'
Takes three different values: "modify" or "view" or "off". Reserved for
future use.

LOGGING
-------------------------------------------------------------------------------
All the logging is done to syslog.

USAGE
-------------------------------------------------------------------------------

Once you have installed and configured the statsproxy for memcached, point
your browser to the server where you memcache service is running, e.g.:

http://frontend-ip-address:8080

It's that easy!
