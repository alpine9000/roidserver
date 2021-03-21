
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define MSG_DONTWAIT 0
#else
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#endif

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef AMIGA
#include <proto/exec.h>
#include <proto/socket.h>
#define localtime_r(a, b) localtime(a)
#endif

#define ROIDSERVER_NUM_PING_PACKETS 8
#define ROIDSERVER_MAX_CLIENTS 16
#define ROIDSERVER_READY_STATE (ROIDSERVER_NUM_PING_PACKETS+1)

#ifndef max
#define max(a, b) (a > b ? a : b)
#endif

#if __STDC_VERSION__ < 199901L || defined(AMIGA) || defined(_WIN32) || defined(__linux__)
#define strlcat(a, b, c) _strlcat(a, b, c)
#define strlcpy(a, b, c) _strlcpy(a, b, c)
#define ROID_NEED_SAFE_STRING_FILLS
#endif

#ifndef AMIGA
#ifdef _WIN32
#define debug_errno(x) printf(x"%s\n", WSAGetLastError());
#else
#define debug_errno(x) printf(x"%s\n", strerror(errno));
#endif
#else
#define debug_errno(x) printf(x"%s\n", Errno());
#endif


#ifdef _WIN32
#define log_printNow() do {						\
    time_t t;								\
    time(&t);								\
    struct tm now;							\
    localtime_s(&now, &t);						\
    printf("%02d:%02d:%02d : ", now.tm_hour, now.tm_min, now.tm_sec);	\
  } while (0);
#else
#define log_printNow() do {						\
    time_t t;								\
    time(&t);								\
    struct tm *l;							\
    struct tm now;							\
    l = localtime_r(&t, &now);						\
    printf("%02d:%02d:%02d : ", l->tm_hour, l->tm_min, l->tm_sec);	\
  } while(0);
#endif

#define log_printf(...) do {						\
    log_printNow();							\
    printf(__VA_ARGS__);						\
  } while(0);

#define countof(x) (sizeof(x)/sizeof(x[0]))

typedef struct {
  int socketFD;
  uint32_t id;
  uint32_t state;
  char buffer[255];
  int bufferIndex;
  struct sockaddr_in addr;
  char ip[20];
  time_t connected;
  uint32_t lag;
  int sent;
  int recv;
} client_connection_t;

typedef struct {
  int socketFD;
  char buffer[4096];
  int bufferIndex;
} status_connection_t;

typedef struct {
  int serverFD;
  int statusFD;
  client_connection_t clients[ROIDSERVER_MAX_CLIENTS];
  status_connection_t status[ROIDSERVER_MAX_CLIENTS];
} global_t;


static global_t global;
static const int ONE = 1;
#ifdef AMIGA
struct Library *SocketBase = 0;
#endif


#ifdef ROID_NEED_SAFE_STRING_FILLS

static int
_strlcpy(char * dest, char * src, int max) {
  int len = strlen(src);
  if (len + 1 < max) {
    memcpy(dest, src, len + 1);
  } else if (max != 0) {
    memcpy(dest, src, max - 1);
    dest[len-1] = 0;
  }

  return len;
}


static int
_strnlen(char *s, size_t max)
{
  unsigned int i;

  for (i = 0; i < max; i++, s++) {
    if (*s == 0) {
      break;
    }
  }

  return (i);
}


static int
_strlcat(char * dest, char * src, int maxlen)
{
  int srcLen = strlen(src);
  int destLen = _strnlen(dest, maxlen);
  if (destLen == maxlen) {
    return destLen+srcLen;
  }
  if (srcLen < maxlen-destLen) {
    memcpy(dest+destLen, src, srcLen+1);
  } else {
    memcpy(dest+destLen, src, maxlen-1);
    dest[destLen+maxlen-1] = 0;
  }
  return destLen + srcLen;
}
#endif


static void
network_exit(int error)
{
#ifdef AMIGA
  if (SocketBase) {
    CloseLibrary(SocketBase);
  }
#endif
  exit(error);
}


static void
network_assertValidClient(int index)
{
  if (index < 0 || index >= (int)countof(global.clients)) {
    log_printf("network_assertValidClient: invalid client: %d\n", index);
    network_exit(5);
  }
}


static void
network_assertValidStatus(int index)
{
  if (index < 0 || index >= (int)countof(global.clients)) {
    log_printf("network_assertValidStatus: invalid status: %d\n", index);
    network_exit(5);
  }
}


static void
network_closeSocket(int fd)
{
#ifdef AMIGA
  CloseSocket(fd);
#elif defined(_WIN32)
  closesocket(fd);
#else
  close(fd);
#endif
}


static int
network_serverTCP(int port)
{
  int socket_fd;
  struct sockaddr_in sa;

  socket_fd = socket(AF_INET, SOCK_STREAM, 0);

  if (socket_fd < 0) {
    debug_errno("network_serverTCP: socket() failed:");
    return -1;
  }

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = inet_addr("0.0.0.0");
  sa.sin_port = htons(port);

  setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (void*) &ONE, sizeof(ONE));

  if (bind(socket_fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
    debug_errno("network_serverTCP: bind() failed: ");
    network_closeSocket(socket_fd);
    return -1;
  }

  if (listen(socket_fd,5)) {
    debug_errno("network_serverTCP: listen() failed: ");
  }

#ifdef AMIGA
  int noblock = 1;
  IoctlSocket(socket_fd, FIONBIO, (char*)&noblock);
#else
#ifdef _WIN32
  unsigned long noblock = 1;
  ioctlsocket(socket_fd, FIONBIO, &noblock);
#else
  int noblock = 1;
  ioctl(socket_fd, FIONBIO, (char*)&noblock);
#endif
#endif

  return socket_fd;
}


static void
network_removeConnection(int index)
{
  network_assertValidClient(index);
  uint32_t id = global.clients[index].id;
  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    if (id == global.clients[i].id) {
      log_printf("network_processClientData: removing connection slot: %d\n", i);
      network_closeSocket(global.clients[i].socketFD);
      global.clients[i].id = 0;
    }
  }
}


static void
network_removeStatusConnection(int index)
{
  network_assertValidStatus(index);
  network_closeSocket(global.status[index].socketFD);
  global.status[index].socketFD = -1;
}


static int
network_accept(int serverFD)
{
  int accept_fd;
  struct sockaddr_in isa;

#if  defined(_WIN32)
  int addr_size;
#else
  socklen_t addr_size;
#endif

  addr_size = sizeof(isa);
  do {
     accept_fd = accept(serverFD, (struct sockaddr*)&isa, &addr_size);
  } while (accept_fd == -1);
  if (accept_fd < 0 ) {
    if (accept_fd < 0) {
      debug_errno("network_accept: accept() failed: ");
    } else {
      network_closeSocket(accept_fd);
    }
    network_closeSocket(serverFD);
    return -1;
  }

  setsockopt(accept_fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&ONE, sizeof ONE);
  setsockopt(accept_fd, IPPROTO_TCP, TCP_NODELAY, (void*)&ONE, sizeof ONE);

#ifdef AMIGA
   int noblock = 1;
  IoctlSocket(accept_fd, FIONBIO, (char*)&noblock);
#else
#ifdef _WIN32
  unsigned long noblock = 1;
  ioctlsocket(accept_fd, FIONBIO, &noblock);
#else
  int noblock = 1;
  ioctl(accept_fd, FIONBIO, (char*)&noblock);
#endif
#endif

  return accept_fd;
}


static int
network_setId(unsigned int index, uint32_t id)
{
  network_assertValidClient(index);
  unsigned int i;
  int count = 0;
  for (i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id == id) {
      count++;
    }
  }

  if (count < 2) {
    global.clients[index].id = id;
    log_printf("network_setId: assigning %d to %x\n", index, id);
  }

  return count < 2;
}


static uint32_t
network_getPacket(int index)
{
  network_assertValidClient(index);
  uint32_t packet = htonl(*(uint32_t*)global.clients[index].buffer);
  int d = 0;
  int s = 4;
  while (s < global.clients[index].bufferIndex) {
    global.clients[index].buffer[d++] = global.clients[index].buffer[s++];
  }
  global.clients[index].bufferIndex -= 4;
  return packet;
}


static void
network_processId(int index)
{
  network_assertValidClient(index);
  uint32_t packet = network_getPacket(index);
  log_printf("network_processId: %d: %x\n", index, packet);
  global.clients[index].state++;
  if (!network_setId(index, packet)) {
    log_printf("network_processId: failed\n");
    network_removeConnection(index);
  }
}


static int
network_send(int clientIndex, void* data, int len)
{
  if (send(global.clients[clientIndex].socketFD, data, len, MSG_DONTWAIT) != len) {
    log_printf("network_send: failed\n");
    network_removeConnection(clientIndex);
  } else {
    global.clients[clientIndex].sent += len;
    return len;
  }

  return 0;
}


static void
network_processPing(int index)
{
  network_assertValidClient(index);
  uint32_t packet = network_getPacket(index);
  log_printf("network_processPing: %d: %x\n", index, packet);
  if (packet == 0xdeadbeef) {
    global.clients[index].state++;
    network_send(index, (void*)&packet, sizeof(packet));
  } else {
    log_printf("network_processPing: failed\n");
    network_removeConnection(index);
  }
}


static void
network_processLag(int index)
{
  network_assertValidClient(index);
  global.clients[index].state++;
  global.clients[index].lag = htonl(*(uint32_t*)global.clients[index].buffer);
  log_printf("network_processLag: %d: %x\n", index, global.clients[index].lag);
}


static void
network_sendClientData(void)
{
  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    int done = 0;
    while (global.clients[i].id != 0 && global.clients[i].bufferIndex >= 4 && !done) {
      if (global.clients[i].state == 0) {
	network_processId(i);
      } else if (global.clients[i].state < ROIDSERVER_READY_STATE) {
	network_processPing(i);
      } else if (global.clients[i].state >= ROIDSERVER_READY_STATE) {
	if (global.clients[i].state == ROIDSERVER_READY_STATE) {
	  network_processLag(i);
	}
	done = 1;
	unsigned int d;
	for (d = 0; d < countof(global.clients); d++) {
	  if (global.clients[i].id == global.clients[d].id &&  global.clients[d].state >= ROIDSERVER_READY_STATE && i != d) {
	    network_send(d, (void*)global.clients[i].buffer, global.clients[i].bufferIndex);
	    global.clients[i].bufferIndex = 0;
	    break;
	  }
	}
      }
    }
  }
}


static int
network_numClientConnections(void)
{
  int total = 0;
  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id != 0) {
      total++;
    }
  }
  return total;
}


static int
network_numStatusConnections(void)
{
  int total = 0;
  unsigned int i;
  for (i = 0; i < countof(global.status); i++) {
    if (global.status[i].socketFD >= 0) {
      total++;
    }
  }
  return total;
}


static const char*
html_renderClientTable(void)
{
  static char buffer[32768];

  if (network_numClientConnections()) {
    snprintf(buffer, sizeof(buffer), "<div class=\"container\"><div class=\"titlebar\">Clients</div><table><thead><tr><th>Slot</th><th>Remote IP</th><th>State</th><th>Game ID</th><th>Sent</th><th>Recv'd</th><th>Lag</th><th>Connected</th></tr></thead></div>");

    unsigned i;
    for (i = 0; i < countof(global.clients); i++) {
      if (global.clients[i].id) {
	static char line[255];
	snprintf(line, sizeof(line), "<tr><td>%d</td><td>%s</td><td>%d</td><td>%x</td><td>%d</td><td>%d</td><td>%d</td><td class=\"time\">%ld</td></tr>\n", i, global.clients[i].ip, global.clients[i].state, global.clients[i].id, global.clients[i].sent, global.clients[i].recv, global.clients[i].lag, global.clients[i].connected);
	strlcat(buffer, line, sizeof(buffer));
      }
    }

    strlcat(buffer, "</table>", sizeof(buffer));
  } else {
    buffer[0] = 0;
  }

  return buffer;
}


static const char*
html_renderStatusSummary(void)
{
  static char buffer[1024];

  snprintf(buffer, sizeof(buffer), "<div class=\"container\"><div class=\"titlebar\">Status</div><table><thead><tr><th>Client Connections</th><th>Status Connections</th></tr></thead><tr><td>%d</td><td>%d</td></tr></table></div>", network_numClientConnections(), network_numStatusConnections());

  return buffer;
}


static const char*
html_renderStatusHTML(void)
{
  static char buffer[32768];
  snprintf(buffer, sizeof(buffer), "%s%s", html_renderStatusSummary(), html_renderClientTable());
  return buffer;
}


static int
network_sendStatus(int statusIndex, void* data, int len)
{
  network_assertValidStatus(statusIndex);

  int done = 0;
  int sent = 0;
  int totalSent = 0;

  while (!done) {
    if ((sent = send(global.status[statusIndex].socketFD, (void*)(((char*)data)+totalSent), len-totalSent, MSG_DONTWAIT)) != len) {
      if (errno != EAGAIN) {
	network_removeStatusConnection(statusIndex);
	done = 1;
	return 0;
      } else {
	totalSent += sent;
      }
    } else {
      done = 1;
    }
  }
  return len;
}


static void
http_sendResponse(int statusIndex, int status, const char* desc, const char* contentType, int cacheSeconds, const char* data, int length)
{
  network_assertValidStatus(statusIndex);

  static char header[512];
  snprintf(header, sizeof(header),  "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nConnection: keep-alive\r\nContent-Length: %d", status, desc, contentType, length);
  if (cacheSeconds) {
    strlcat(header, "\r\nCache-Control: max-age=", sizeof(header));
    static char seconds[20];
    snprintf(seconds, sizeof(seconds), "%d", cacheSeconds);
    strlcat(header, seconds, sizeof(header));
  }
  strlcat(header, "\r\n\r\n", sizeof(header));
  const int len = strlen(header);
  if (network_sendStatus(statusIndex, header, len) != len) {
    return;
  }
  network_sendStatus(statusIndex, (void*)data, length);
}


static int
http_sendFile(int i, const char* filename, const char* contentType, int cacheSeconds)
{
  network_assertValidStatus(i);
  int found = 0;

  struct stat st;
  if (stat(filename, &st) == 0) {
    char* buffer = malloc(st.st_size+1);
    if (buffer) {
      int fd = open(filename, O_RDONLY);
      if (fd >= 0) {
	int len = read(fd, buffer, st.st_size);
	if (len) {
	  buffer[len] = 0;
	  http_sendResponse(i, 200, "OK", contentType, cacheSeconds, buffer, len);
	  found = 1;
	}
	close(fd);
      }
      free(buffer);
    }
  }

  return found;
}


static void
http_processRequest(int i)
{
  network_assertValidStatus(i);

  int found = 0;
  if (strstr(global.status[i].buffer, "GET /9000 ") != NULL) {
    found = http_sendFile(i, "roid.html", "text/html", 10000);
  } else if (strstr(global.status[i].buffer, "GET /status") != NULL) {
    const char* buffer = html_renderStatusHTML();
    http_sendResponse(i, 200, "OK", "text/html", 0, buffer, strlen(buffer));
    found = 1;
  } else if (strstr(global.status[i].buffer, "GET /roid.css") != NULL) {
    found = http_sendFile(i, "roid.css", "text/css", 10000);
  } else if (strstr(global.status[i].buffer, "GET /favicon.ico") != NULL) {
    found = http_sendFile(i, "roid.ico", "image/vnd.microsoft.icon", 10000);
  } else if (strstr(global.status[i].buffer, "GET /Sans.ttf") != NULL) {
    found = http_sendFile(i, "Sans.ttf", "font/ttf", 10000);
  }

  if (!found) {
    http_sendFile(i, "404.html", "text/html", 10000);
  }
}


static void
http_processRequests(int i)
{
  network_assertValidStatus(i);

  char* ptr = strstr(global.status[i].buffer, "\n\n");

  if (ptr != NULL) { /* end of request */
    http_processRequest(i);
    ptr += strlen("\n\n");
    char* end = &global.status[i].buffer[global.status[i].bufferIndex];
    char* dest = global.status[i].buffer;
    global.status[i].bufferIndex -= (ptr - global.status[i].buffer);
    if (ptr < end) {
      *dest = *ptr;
      dest++, ptr++;
    }
    global.status[i].buffer[global.status[i].bufferIndex] = 0;
  }
}


static void
network_sendStatusData(void)
{
  unsigned int i;
  for (i = 0; i < countof(global.status); i++) {
    if (global.status[i].socketFD >= 0) {
      while (strstr(global.status[i].buffer, "\n\n") != NULL) { /* end of request */
	http_processRequests(i);
      }
    }
  }
}


static void
network_processClientData(fd_set *read_fds)
{
  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id > 0 &&( FD_ISSET(global.clients[i].socketFD, read_fds))) {
      int done = 0;
      do {
	int len = recv(global.clients[i].socketFD, &global.clients[i].buffer[global.clients[i].bufferIndex], 1, MSG_DONTWAIT);
	if (len > 0) {
	  global.clients[i].recv++;
	  if (global.clients[i].bufferIndex < (int)(countof(global.clients[i].buffer)-1)) {
	    global.clients[i].bufferIndex++;
	  }
	} else {
	  if (len == 0) {
	    log_printf("network_processClientData: failed %s\n", strerror(errno));
	    network_removeConnection(i);
	  }
	  done = 1;
	}
      } while (!done);
    }
  }
}


static void
network_processStatusData(fd_set *read_fds)
{
  unsigned int i;
  for (i = 0; i < countof(global.status); i++) {
    if (global.status[i].socketFD >= 0 &&( FD_ISSET(global.status[i].socketFD, read_fds))) {
      int done = 0;
      do {
	int len = recv(global.status[i].socketFD, &global.status[i].buffer[global.status[i].bufferIndex], 1, MSG_DONTWAIT);
	if (len > 0) {
	  if (global.status[i].buffer[global.status[i].bufferIndex] != '\r') {
	    if (global.status[i].bufferIndex < (int)(countof(global.status[i].buffer)-1)) {
	      global.status[i].bufferIndex++;
	    }
	  }
	} else {
	  if (len == 0) {
	    network_removeStatusConnection(i);
	  }
	  done = 1;
	}
      } while (!done);
    }
  }
}


static int
network_setupFDS(fd_set *read_fds)
{
  FD_ZERO(read_fds);

  FD_SET(global.serverFD, read_fds);
  FD_SET(global.statusFD, read_fds);

  int maxFD = max(global.serverFD, global.statusFD);

  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id) {
      if (global.clients[i].socketFD > maxFD) {
	maxFD = global.clients[i].socketFD;
      }
      FD_SET(global.clients[i].socketFD, read_fds);
    }
  }

  for (i = 0; i < countof(global.status); i++) {
    if (global.status[i].socketFD >= 0) {
      if (global.status[i].socketFD > maxFD) {
	maxFD = global.status[i].socketFD;
      }
      FD_SET(global.status[i].socketFD, read_fds);
    }
  }

  return maxFD;
}


static void
network_addConnection(int socketFD)
{
  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id == 0) {
      memset(&global.clients[i], 0, sizeof(global.clients[i]));
      global.clients[i].id = 1;
      global.clients[i].socketFD = socketFD;

      time(&global.clients[i].connected);
      socklen_t addr_size = sizeof(struct sockaddr_in);
      getpeername(socketFD, (struct sockaddr *)&global.clients[i].addr, &addr_size);
#ifdef AMIGA
      strlcpy(global.clients[i].ip, Inet_NtoA(global.clients[i].addr.sin_addr.s_addr), sizeof(global.clients[i].ip));
#else
      strlcpy(global.clients[i].ip, inet_ntoa(global.clients[i].addr.sin_addr), sizeof(global.clients[i].ip));
#endif
      log_printf("network_addConnection: new client slot: %d fd: %d\n", i, socketFD);
      return;
    }
  }

  network_closeSocket(socketFD);
  log_printf("network_addConnection: no free slots\n");
}


static void
network_addStatus(int socketFD)
{
  unsigned int i;
  for (i = 0; i < countof(global.status); i++) {
    if (global.status[i].socketFD < 0) {
      global.status[i].socketFD = socketFD;
      global.status[i].bufferIndex = 0;
      log_printf("network_addStatus: new status slot: %d fd: %d\n", i, socketFD);
      return;
    }
  }

  network_closeSocket(socketFD);
  log_printf("network_addStatus: no free slots\n");
}


int
main(int argc, char** argv)
{
  (void)argc;
  (void)argv;

#ifdef AMIGA
  SocketBase = OpenLibrary((APTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    network_exit(1);
  }
#endif

  unsigned int i;
  for (i = 0; i < countof(global.status); i++) {
    global.status[i].socketFD = -1;
  }

  global.serverFD = network_serverTCP(9000);
  global.statusFD = network_serverTCP(9001);

  if (global.serverFD < 0 || global.statusFD < 0) {
    network_exit(2);
  }

  fd_set read_fds;
  int maxFD = 0;

  log_printf("roidserver: ready\n");

  do {
    maxFD = network_setupFDS(&read_fds);

#ifdef AMIGA
    int task = WaitSelect(maxFD + 1, &read_fds, NULL, NULL, NULL, NULL);
#else
    int task = select(maxFD + 1, &read_fds, NULL, NULL, NULL);
#endif

    switch (task) {
    case -1:
      debug_errno("main: select failed\n");
      network_exit(3);
      break;
    default:
      if (FD_ISSET(global.serverFD, &read_fds)) {
	int clientFD = network_accept(global.serverFD);
	if (clientFD < 0) {
	  debug_errno("main: network_accept failed\n");
	} else {
	  network_addConnection(clientFD);
	}
      }

      if (FD_ISSET(global.statusFD, &read_fds)) {
	int clientFD = network_accept(global.statusFD);
	if (clientFD < 0) {
	  debug_errno("main: network_accept failed\n");
	} else {
	  network_addStatus(clientFD);
	}
      }


      network_processClientData(&read_fds);
      network_processStatusData(&read_fds);
      network_sendClientData();
      network_sendStatusData();
    }
  } while (1);


  network_exit(0);
}
