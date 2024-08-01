Network Game Server for Metro Siege
===================================

About
-----
_roidserver_ is the server component for [Metro Siege](https://metrosiege.com) online co-op. Clients connect to the server, are matched by a supplied GAME ID, then will be able to play cooperatively. The server facilitates communication between each pair of clients.

Functional Configuration
------------------------
Different _roidserver_ functionality can be enabled/disabled at compile time by editing [roidserver.c](roidserver.c) and modifying the configuration _hash defines_. Comment the item to change configuration from default.

### ROIDSERVER_DASHBOARD
Enable the web based dashboard (default: enabled, amiga: disabled)

### ROIDSERVER_LOGGING
Enable logging of operational events (default: enabled, amiga: disabled)

### ROIDSERVER_MAX_CLIENTS
Set the maximum number of clients allowed to connect (default: 256, amiga: 8)

### ROIDSERVER_NO_ALLOW_DENY_LISTS 
Disable allow/deny list functionality (default: lists allowed, amiga: lists disabled)

Supported Targets
-----------------
_roidserver_ has been built and tested on AmigaOS, OS/X, Linux and Windows.

Building
--------
A very simple Makefile is included, however this may require editing or replacement depending on your compiler and environment. _roidserver_ is a single C file, so compilation in other environments should be quite simple. On most unix like systems roidserver can be built using:
```
cc roidserver.c -o roidserver
```
To build roidserver for an Amiga use [Bebbo's Amiga GCC](https://github.com/bebbo/amiga-gcc) and the supplied Makefile (editing for installation paths will be required).

Running
-------
_roidserver_ allows connections from untrusted clients over the internet. For this reason every precaution must be taken when deploying and running this server. The web based dashboard is not secure and must be run behind a proxy server that can provide the required level of security. _roidserver_ will by default bind to port 0.0.0.0:9000 for the game connections and 127.0.0.1:9001 for the web based dashboard. _roidserver_ will only allocate memory for the dashboard functionality. If the dashboard is disabled, no additional memory will be allocated during operation.

Dashboard
---------
The web based dashboard shows a summary of the client connection statistics as well as connection details for each connected client. The dashboard provides some server based controls (Reset - disconnect all clients, Reload - reload configuration, Shutdown - terminate the server) and client based controls (Disconnect - disconnect the client, Ban - add the client to the deny.txt file). By default the dashboard will bind the 127.0.0.1. We do not recommend running the dashboard in a way that allows unretricted access to the broarder internet).

Configuration Files
-------------------
A group of optional configuration files can be used to configure _roidserver_.

### allow.txt
List of ip addresses/netmasks that are allowed to connect to the dashboard html server.

Format: <IP/Netmask>
Example: 124.233.121.112/255.255.255.255

Note: netmask is optional

### deny.txt
List of ip addresses/netmasks that are not allowed to connect to the _roidserver_.

Format: <IP/Netmask>
Example: 124.233.121.112/255.255.255.255

Note: netmask is optional

### ignore.txt
List of ip addresses/netmasks in which connection information will not be logged. (Useful for health checks)

Format: <IP/Netmask>
Example: 124.233.121.112/255.255.255.255

Note: netmask is optional

### root.txt
Name of the root url path used for the dashboard html server (Useful if the dashboard is behind a proxy server)

Example: dashboard/url

https://example.com/dashboard/url

### ports.txt
Set the listen ports for both the game server port (default: 9000) and dashboard port (default: 9001)

Format: [game server port] [dashboard port]

Example: 9002 9003

Code Security
-------------
This program is written in C, therefore it is highly recommended that additional measures be put in place to ensure that defects in the code do not lead to compromise of the hosting environment. _roidserver_ is quite simple software and does not require high levels of compiler optimisations to be performant, therefore we recommend enabling any available sanitizer or stack protection options that your C compiler provides to minimise the risk that memory defects translate to code execution vulnerabilities. For example: -fsanitize=address -fsanitize=undefined -fstack-protector-all.

Amiga
-----
By default the Amiga version of _roidserver_ will be configured with minimal options. This configuration is designed to be run on home networks with low power Amiga computers. The ram overhead of running _roidserver_ on an Amiga is around 10kb. If you have a higher powered Amiga and wish to enable additional features, edit [roidserver.c](roidserver.c).

Logging
-------
If configured, _roidserver_ will log operational details and connections to stdout. If configured as daemon we recommend redirecting this output to a log file

Warranty
--------
      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
      "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
      TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
      PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
      CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
      EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
      PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
      PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
      LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
      NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
      SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
