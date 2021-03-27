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

#define ROIDSERVER_DASHBOARD
#define ROIDSERVER_LOGGING
#define ROIDSERVER_GAME_PORT              9000
#define ROIDSERVER_DASHBOARD_PORT         9001
#define ROIDSERVER_NUM_PING_PACKETS       8
#define ROIDSERVER_MAX_CLIENTS            16


#define ROIDSERVER_CACHE_TIMEOUT_SECONDS  (60*60*24*365)
#define ROIDSERVER_READY_STATE            (ROIDSERVER_NUM_PING_PACKETS+1)
#define ROIDSERVER_HTTP_REQUEST_SEPARATOR "\r\n\r\n"
#define ROIDSERVER_XFF_HEADER             "X-Forwarded-For: "
#define ROIDSERVER_ENABLE_PROXY           1
//#define ROIDSERVER_ASSERTS
//#define ROIDSERVER_MEASURE_TIME

#ifdef ROIDSERVER_ASSERTS
#define network_assertValidClient(x) _network_assertValidClient(x)
#define network_assertValidDashboard(x) _network_assertValidDashboard(x)
#else
#define network_assertValidClient(x) (void)x
#define	network_assertValidDashboard(x) (void)x
#endif

#if __STDC_VERSION__ < 199901L || defined(AMIGA) || defined(_WIN32) || defined(__linux__)
#define strlcat(a, b, c) _strlcat(a, b, c)
#define strlcpy(a, b, c) _strlcpy(a, b, c)
#define ROID_NEED_SAFE_STRING_FILLS
#endif

#ifdef _WIN32
#define log_getError() _w32_getError()
#else
#define log_getError() strerror(errno)
#endif


int
_inet_aton(const char *cp, struct in_addr *addr)
{
  addr->s_addr = inet_addr((char*)cp);
  return (addr->s_addr == INADDR_NONE) ? 0 : 1;
}


#ifdef _WIN32
const char*
_w32_getError(void)
{
  if (errno) {
    return strerror(errno);
  }

  static char buffer[256];
  buffer[0] = 0;

  FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
		WSAGetLastError(),
		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
		buffer,	sizeof(buffer),	NULL);
  return buffer;
}
#endif


#ifdef ROIDSERVER_LOGGING
#ifdef _WIN32
#define log_printNow() if (global.loggingEnabled) {			\
    time_t t;								\
    time(&t);								\
    struct tm now;							\
    localtime_s(&now, &t);						\
    printf("%02d/%02d/%d %02d:%02d:%02d : ", now.tm_mday, now.tm_mon+1, 1900+now.tm_year, now.tm_hour, now.tm_min, now.tm_sec); \
  }
#else
#define log_printNow() if (global.loggingEnabled) {			\
    time_t t;								\
    time(&t);								\
    struct tm *l;							\
    struct tm now;							\
    l = localtime_r(&t, &now);						\
    printf("%02d/%02d/%d %02d:%02d:%02d : ", l->tm_mday, l->tm_mon+1, 1900+l->tm_year, l->tm_hour, l->tm_min, l->tm_sec); \
  }
#endif

#define log_printf(...) if (global.loggingEnabled) {			\
    log_printNow();							\
    printf(__VA_ARGS__);						\
    fflush(stdout);							\
  }
#else
#define log_printf(...)
#endif

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
  uint32_t networkPlayer;
} client_connection_t;

#ifdef ROIDSERVER_DASHBOARD
typedef struct {
  int socketFD;
  char buffer[4096];
  int bufferIndex;
} dashboard_connection_t;
#endif

typedef struct {
  uint32_t addr;
  uint32_t mask;
} allowdeny_addr_t;

typedef struct {
  allowdeny_addr_t* entries;
  unsigned int num;
} allowdeny_list_t;

typedef struct {
  int serverFD;
  client_connection_t clients[ROIDSERVER_MAX_CLIENTS];
  allowdeny_list_t denyList;

#ifdef ROIDSERVER_DASHBOARD
  allowdeny_list_t dashboardAllowList;
  dashboard_connection_t dashboard[ROIDSERVER_MAX_CLIENTS];
  int dashboardFD;
  char rootPath[256];
#endif

  int loggingEnabled;
} global_t;


static global_t global;
static const int ONE = 1;
#ifdef AMIGA
struct Library *SocketBase = 0;
#endif

#ifdef ROID_NEED_SAFE_STRING_FILLS

#if defined(AMIGA) || (!defined(_WIN32) && (defined(__linux__) && _POSIX_C_SOURCE < 200809L))
static int
strnlen(const char *s, size_t max)
{
  int len;

  for (len = 0; len < max; len++, s++) {
    if (!*s) {
      break;
    }
  }

  return len;
}
#endif

static int
_strlcpy(char* dest, char* src, int max)
{
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


int
_strlcat(char *dest, char *src, int maxlen)
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


#ifdef ROIDSERVER_ASSERTS
static void
_network_assertValidClient(int index)
{
  if (index < 0 || index >= (int)countof(global.clients)) {
    log_printf("network_assertValidClient: invalid client: %d\n", index);
    network_exit(5);
  }
}


#ifdef ROIDSERVER_DASHBOARD
static void
_network_assertValidDashboard(int index)
{
  if (index < 0 || index >= (int)countof(global.dashboard)) {
    log_printf("network_assertValidDashboard: invalid dashboard: %d\n", index);
    network_exit(5);
  }
}
#endif
#endif

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
    log_printf("network_serverTCP: socket() failed: %s\n", log_getError());
    return -1;
  }

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = inet_addr("0.0.0.0");
  sa.sin_port = htons(port);

  setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (void*) &ONE, sizeof(ONE));

  if (bind(socket_fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
    log_printf("network_serverTCP: bind() failed: %s\n", log_getError());
    network_closeSocket(socket_fd);
    return -1;
  }

  if (listen(socket_fd,5)) {
    log_printf("network_serverTCP: listen() failed: %s\n", log_getError());
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
  if (id != 0) {
    for (i = 0; i < countof(global.clients); i++) {
      if (id == global.clients[i].id) {
	log_printf("network_processClientData: removing connection slot: %d\n", i);
	network_closeSocket(global.clients[i].socketFD);
	global.clients[i].id = 0;
      }
    }
  } else {
    log_printf("network_processClientData: removing connection slot: %d\n", index);
    network_closeSocket(global.clients[index].socketFD);
  }
}

static int
main_loadAllowDenyList(allowdeny_list_t* list, const char* filename)
{
  FILE* fp = fopen(filename, "r");
  char line[256];
  char* ptr;
  unsigned int listSize = 0;

  list->num = 0;
  list->entries = 0;

  if (fp) {
    while ((ptr = fgets(line, sizeof(line), fp))) {
      line[strcspn(line, "\n")] = 0;
      char* mask = strstr(line, "/");
      if (listSize <= list->num) {
	listSize = listSize ? listSize*2 : 16;
	list->entries = realloc(list->entries, sizeof(list->entries[0])*listSize);
      }
      if (mask != NULL) {
	mask++;
	struct in_addr addr;
	if (_inet_aton(mask, &addr) == 1) {
	  list->entries[list->num].mask = htonl(addr.s_addr);
	} else {
	  log_printf("main_loadAllowDenyList(%s): failed to parse mask: %s\n", filename, mask);
	  list->entries[list->num].mask = 0;
	}
	line[strcspn(line, "/")] = 0;
	list->entries[list->num].addr = inet_addr(line);
      } else {
	list->entries[list->num].mask = 0xFFFFFFFF;
	list->entries[list->num].addr = inet_addr(line);
      }
      log_printf("main_loadAllowDenyList(%s): adding %s -> %x mask(%x)\n", filename, line, (uint32_t)list->entries[list->num].addr, list->entries[list->num].mask);
      list->num++;
    }

    fclose(fp);
  } else {
    log_printf("main_loadAllowDenyList(%s): failed to open %s\n", filename, log_getError());
  }

  return list->num > 0;
}


static int
network_matchAddr(uint32_t addr1, uint32_t addr2, uint32_t mask) {
  addr1 = htonl(addr1);
  addr2 = htonl(addr2);
  return (addr1 & mask) == (addr2 & mask);
}


static void
main_loadDenyList(void)
{
  if (!main_loadAllowDenyList(&global.denyList, "deny.txt")) {
    log_printf("main: WARNING: failed to load deny list\n");
  }
}


#ifdef ROIDSERVER_DASHBOARD
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


static void
dashboard_removeConnection(int index)
{
  network_assertValidDashboard(index);
  network_closeSocket(global.dashboard[index].socketFD);
  global.dashboard[index].socketFD = -1;
}


static int
dashboard_numConnections(void)
{
  int total = 0;
  unsigned int i;
  for (i = 0; i < countof(global.dashboard); i++) {
    if (global.dashboard[i].socketFD >= 0) {
      total++;
    }
  }
  return total;
}


static char *
http_find(unsigned int dashboardIndex, const char *needle,  int needleLen)
{
  char n, h;
  char* haystack = global.dashboard[dashboardIndex].buffer;
  int haystackLen = sizeof(global.dashboard[dashboardIndex].buffer);
  if ((n = *needle++) != '\0') {
    int len = strnlen(needle, needleLen);
    do {
      do {
	if ((h = *haystack++) == '\0' || haystackLen-- < 1)
	  return (NULL);
      } while (h != n);
      if (len > haystackLen)
	return (NULL);
    } while (strncmp(haystack, needle, len) != 0);
    haystack--;
  }
  return ((char *)haystack);
}


static char *
http_matchRequest(unsigned int dashboardIndex, const char *request, int requestLen)
{
  char n, h;
  char* haystack = global.dashboard[dashboardIndex].buffer;
  int haystackLen = sizeof(global.dashboard[dashboardIndex].buffer);
  const char terminator = '\n';

  if ((n = *request++) != terminator && n != '\0') {
    int len = strnlen(request, requestLen);
    do {
      do {
	if ((h = *haystack++) == terminator || n == '\0' || haystackLen-- < 1)
	  return (NULL);
      } while (h != n);
      if (len > haystackLen)
	return (NULL);
    } while (strncmp(haystack, request, len) != 0);
    haystack--;
  }
  return ((char *)haystack);
}


static const char*
dashboard_renderDisconnectHTML(unsigned int dashboardIndex)
{
  network_assertValidDashboard(dashboardIndex);

  static char* result = "OK";
  char* ptr = http_matchRequest(dashboardIndex, "disconnect/", sizeof("disconnect/"));

  if (ptr) {
    ptr += strlen("disconnect/");
    int i;
    if (sscanf(ptr, "%d", &i) == 1) {
      network_removeConnection(i);
    }
  }

  return result;
}


static const char*
dashboard_renderReloadHTML(unsigned int dashboardIndex)
{
  network_assertValidDashboard(dashboardIndex);

  static char* buffer = "OK";

  if (!main_loadAllowDenyList(&global.dashboardAllowList, "allow.txt")) {
    log_printf("main: WARNING: failed to load allow list\n");
  }

  main_loadDenyList();

  unsigned int i;
  for (i = 0; i < countof(global.dashboard); i++) {
    dashboard_removeConnection(i);
  }

  return buffer;
}


static const char*
dashboard_renderResetHTML(unsigned int dashboardIndex)
{
  network_assertValidDashboard(dashboardIndex);
  static char* buffer = "OK";

  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id > 0) {
      network_removeConnection(i);
    }
  }

  return buffer;
}


static const char*
dashboard_renderStatusHTML(unsigned int dashboardIndex)
{
  network_assertValidDashboard(dashboardIndex);
  static char line[255];
  static char buffer[32768];

  snprintf(buffer, sizeof(buffer), "<div class=\"container\"><div class=\"titlebar\">Server Overview</div><table><thead><tr><th>Connected Fighters</th><th>Dashboard Connections</th><th>Current Time</th><th></th></tr></thead><tr><td>%d</td><td>%d</td><td id=\"time\"></td><td id=\"server-controls\"></td></tr></table></div>", network_numClientConnections(), dashboard_numConnections());

  if (network_numClientConnections()) {
    snprintf(line, sizeof(line), "<div class=\"container\"><div class=\"titlebar\">Fighters</div><table><thead><tr><th>Slot</th><th>Remote IP</th><th>State</th><th>Game ID</th><th>Sent</th><th>Recv'd</th><th>Lag</th><th>Connected Since</th><th></th></tr></thead></div>");
    strlcat(buffer, line, sizeof(buffer));

    unsigned i;
    for (i = 0; i < countof(global.clients); i++) {
      if (global.clients[i].id) {

	snprintf(line, sizeof(line), "<tr clientid=\"%d\"><td>%d</td><td>%s</td><td>%d</td><td>%x</td><td>%d</td><td>%d</td><td>%d</td><td class=\"time\">%ld</td><td class=\"client-controls\"></td></tr>\n", i, i, global.clients[i].ip, global.clients[i].state, global.clients[i].id, global.clients[i].sent, global.clients[i].recv, global.clients[i].lag, global.clients[i].connected);
	strlcat(buffer, line, sizeof(buffer));
      }
    }

    strlcat(buffer, "</table>", sizeof(buffer));
  }

  return buffer;
}


static int
dashboard_sendDashData(int dashboardIndex, void* data, int len)
{
  network_assertValidDashboard(dashboardIndex);

  int done = 0;
  int sent = 0;
  int totalSent = 0;

  while (!done) {
    if ((sent = send(global.dashboard[dashboardIndex].socketFD, (void*)(((char*)data)+totalSent), len-totalSent, MSG_DONTWAIT)) != len) {
      if (errno != EAGAIN) {
	dashboard_removeConnection(dashboardIndex);
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
http_sendResponse(int dashboardIndex, int statusCode, const char* statusMessage, const char* contentType, int cacheSeconds, const char* data, int length)
{
  network_assertValidDashboard(dashboardIndex);

  static char header[512];
  snprintf(header, sizeof(header),  "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nConnection: keep-alive\r\nContent-Length: %d", statusCode, statusMessage, contentType, length);
  if (cacheSeconds) {
    strlcat(header, "\r\nCache-Control: max-age=", sizeof(header));
    static char seconds[20];
    snprintf(seconds, sizeof(seconds), "%d", cacheSeconds);
    strlcat(header, seconds, sizeof(header));
  }
  strlcat(header, ROIDSERVER_HTTP_REQUEST_SEPARATOR, sizeof(header));
  const int len = strnlen(header, sizeof(header));
  if (dashboard_sendDashData(dashboardIndex, header, len) != len) {
    return;
  }
  dashboard_sendDashData(dashboardIndex, (void*)data, length);
}


static int
http_sendFile(int i, const char* filename, const char* contentType, int cacheSeconds)
{
  network_assertValidDashboard(i);
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


static int
http_send(int i, const char* (*renderer)(unsigned int), const char* contentType, int cacheSeconds, unsigned int dashboardIndex)
{
  network_assertValidClient(i);
  network_assertValidDashboard(dashboardIndex);

  if (renderer) {
    const char* buffer = renderer(dashboardIndex);
    http_sendResponse(i, 200, "OK", contentType, cacheSeconds, buffer, strlen(buffer));
    return 1;
  }

  return 0;
}


static char*
http_matchPath(int dashboardIndex, const char* path)
{
  network_assertValidDashboard(dashboardIndex);

  static char buffer[1024];
  snprintf(buffer, sizeof(buffer), "GET /%s/%s", global.rootPath, path);
  return http_matchRequest(dashboardIndex, buffer, sizeof(buffer));
}


#ifdef ROIDSERVER_ENABLE_PROXY
static int
http_checkProxyConnection(unsigned int dashboardIndex)
{
  network_assertValidDashboard(dashboardIndex);

  char* xff = http_find(dashboardIndex, ROIDSERVER_XFF_HEADER, sizeof(ROIDSERVER_XFF_HEADER));

  int allowed = 1;

  if (xff) {
    allowed = 0;
    xff += strlen(ROIDSERVER_XFF_HEADER);
    uint32_t addr =  inet_addr(xff);

    unsigned int i;

    for (i = 0; i < global.dashboardAllowList.num; i++) {
      if (network_matchAddr(global.dashboardAllowList.entries[i].addr, addr, global.dashboardAllowList.entries[i].mask)) {
	allowed = 1;
	break;
      }
    }

    if (!allowed) {
#ifdef AMIGA
      log_printf("network_addConnection: blocked connection from: %s %x\n", Inet_NtoA(addr), addr);
#else
      struct in_addr ia;
      ia.s_addr = addr;
      log_printf("network_addConnection: blocked connection from: %s %x\n", inet_ntoa(ia), addr);
#endif
    }
  }

  return allowed;
}
#else
#define http_checkProxyConnection(x) 1
#endif


static int
http_matchWildcard(unsigned int dashboardIndex, const char* path)
{
  network_assertValidDashboard(dashboardIndex);

  char* ptr;

  if ((ptr = http_matchRequest(dashboardIndex, "GET ", sizeof("GET "))) != NULL) {
    return  http_matchRequest(dashboardIndex, path, strlen(path)) != NULL;
  }

  return 0;
}


static void
http_processRequest(int i)
{
  network_assertValidDashboard(i);

  int found = 0;
  if (http_checkProxyConnection(i)) {
    if (http_matchPath(i, "status") != NULL) {
      found = http_send(i, dashboard_renderStatusHTML, "text/html", 0, i);
    } else if (http_matchPath(i, "reload") != NULL) {
      found = http_send(i, dashboard_renderReloadHTML, "text/html", 0, i);
    } else if (http_matchPath(i, "roid.css") != NULL) {
      found = http_sendFile(i, "roid.css", "text/css", ROIDSERVER_CACHE_TIMEOUT_SECONDS);
    } else if (http_matchRequest(i, "GET /favicon.ico", sizeof("GET /favicon.ico")) != NULL) {
      found = http_sendFile(i, "roid.ico", "image/vnd.microsoft.icon", ROIDSERVER_CACHE_TIMEOUT_SECONDS);
    } else if (http_matchPath(i, "reset") != NULL) {
      found = http_send(i, dashboard_renderResetHTML, "text/html", 0, i);
    } else if (http_matchPath(i, "dashboard") != NULL) {
      found = http_sendFile(i, "roid.html", "text/html", ROIDSERVER_CACHE_TIMEOUT_SECONDS);
    } else if (http_matchPath(i, "disconnect") != NULL) {
      found = http_send(i, dashboard_renderDisconnectHTML, "text/html", 0, i);
    } else if (http_matchPath(i, "exit") != NULL) {
      network_exit(0);
    }
  }


  if (!found) {
    if (http_matchWildcard(i, "Sans.ttf")) {
      found = http_sendFile(i, "Sans.ttf", "font/ttf", ROIDSERVER_CACHE_TIMEOUT_SECONDS);
    }
    if (!found) {
      http_sendFile(i, "404.html", "text/html", ROIDSERVER_CACHE_TIMEOUT_SECONDS);
    }
  }
}


static void
http_processRequests(char* ptr, int i)
{
  network_assertValidDashboard(i);

  global.dashboard[i].buffer[global.dashboard[i].bufferIndex] = 0;

  *ptr = 0;

#ifdef ROIDSERVER_MEASURE_TIME
  clock_t startTime, endTime;
  startTime = clock();
#endif

  http_processRequest(i);
  ptr += strlen(ROIDSERVER_HTTP_REQUEST_SEPARATOR);
  char* end = &global.dashboard[i].buffer[global.dashboard[i].bufferIndex];
  char* dest = global.dashboard[i].buffer;
  global.dashboard[i].bufferIndex -= (ptr - global.dashboard[i].buffer);
  if (global.dashboard[i].bufferIndex < 0) {
    /* this should not be possible */
    global.dashboard[i].bufferIndex = 0;
  } else {
    while (ptr < end) {
      *dest++ = *ptr++;
    }
  }

#ifdef ROIDSERVER_MEASURE_TIME
  endTime = clock();
  printf("%f\n", 1000.0 * ((double)(endTime-startTime) / (double)(CLOCKS_PER_SEC)));
#endif

  global.dashboard[i].buffer[global.dashboard[i].bufferIndex] = 0;
}


static void
dashboard_sendClientData(void)
{
  unsigned int i;
  for (i = 0; i < countof(global.dashboard); i++) {
    if (global.dashboard[i].socketFD >= 0) {
      char* ptr;
      while ((ptr = http_find(i, ROIDSERVER_HTTP_REQUEST_SEPARATOR, sizeof(ROIDSERVER_HTTP_REQUEST_SEPARATOR))) != NULL) { /* end of request */
	http_processRequests(ptr, i);
      }
    }
  }
}

static void
network_addDashboardConnection(int socketFD)
{
  unsigned int i, allowed = 0;

  struct sockaddr_in addr;
  socklen_t addr_size = sizeof(addr);

  getpeername(socketFD, (struct sockaddr *)&addr, &addr_size);

  for (i = 0; i < global.dashboardAllowList.num; i++) {
    if (network_matchAddr(global.dashboardAllowList.entries[i].addr, addr.sin_addr.s_addr, global.dashboardAllowList.entries[i].mask)) {
      allowed = 1;
      break;
    }
  }

  if (!allowed) {
#ifdef AMIGA
    log_printf("network_addDashboardConnection: blocked connection from: %s %x\n", Inet_NtoA(addr.sin_addr.s_addr), addr.sin_addr.s_addr);
#else
    log_printf("network_addDashboardConnection: blocked connection from: %s %x\n", inet_ntoa(addr.sin_addr), addr.sin_addr.s_addr);
#endif
    return;
  }

  for (i = 0; i < countof(global.dashboard); i++) {
    if (global.dashboard[i].socketFD < 0) {
      global.dashboard[i].socketFD = socketFD;
      global.dashboard[i].bufferIndex = 0;
      global.dashboard[i].buffer[global.dashboard[i].bufferIndex] = 0;
      log_printf("network_addDashboardConnection: new dashboard slot: %d fd: %d\n", i, socketFD);
      return;
    }
  }

  network_closeSocket(socketFD);
  log_printf("network_addDashboardConnection: no free slots\n");
}


static int
dashboard_loadRootPath(void)
{
  FILE* fp = fopen("root.txt", "r");
  int success = 0;

  memset(global.rootPath, 0, sizeof(global.rootPath));

  if (fp) {
    if (fread(global.rootPath, 1, sizeof(global.rootPath)-1, fp) > 0) {
      success = 1;
    }
    fclose(fp);
  } else {
    log_printf("dashboard_loadRootPath: failed to open rootPath.txt: %s\n", log_getError());
  }

  unsigned i;
  for (i = 0; i < sizeof(global.rootPath); i++) {
    if (!global.rootPath[i]) {
      break;
    }
    if (global.rootPath[i] == '\n') {
      global.rootPath[i] = 0;
    }

    if (global.rootPath[i] == '\r') {
      global.rootPath[i] = 0;
    }
  }

  return success;
}

static void
dashboard_processClientData(fd_set *read_fds)
{
  unsigned int i;
  for (i = 0; i < countof(global.dashboard); i++) {
    if (global.dashboard[i].socketFD >= 0 &&( FD_ISSET(global.dashboard[i].socketFD, read_fds))) {
      int done = 0;
      do {
	int len = recv(global.dashboard[i].socketFD, &global.dashboard[i].buffer[global.dashboard[i].bufferIndex], sizeof(global.dashboard[i].buffer)-global.dashboard[i].bufferIndex, MSG_DONTWAIT);
	if (len > 0) {
	  global.dashboard[i].bufferIndex += len;
	  if (global.dashboard[i].bufferIndex >= (int)(countof(global.dashboard[i].buffer)-1)) {
	    global.dashboard[i].bufferIndex = countof(global.dashboard[i].buffer)-1;
	  }
	} else {
	  if (len == 0) {
	    dashboard_removeConnection(i);
	  }
	  done = 1;
	}
      } while (!done);
    }
  }
}

#endif //ROIDSERVER_DASHBOARD

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
      log_printf("network_accept: accept() failed: %s\n", log_getError());
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
    global.clients[index].networkPlayer = count;
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
  if (packet == 0xdeadbeef) {
    global.clients[index].state++;
    log_printf("network_processPing: %d: %x networkPlayer: %d\n", index, packet,     global.clients[index].networkPlayer);
    uint32_t networkPlayer = htonl(global.clients[index].networkPlayer);
    network_send(index, (void*)&networkPlayer, sizeof(networkPlayer));
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



static void
network_processClientData(fd_set *read_fds)
{
  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id > 0 && (FD_ISSET(global.clients[i].socketFD, read_fds))) {
      int done = 0;
      do {
	int len = recv(global.clients[i].socketFD, &global.clients[i].buffer[global.clients[i].bufferIndex], 1, MSG_DONTWAIT);
	if (len > 0) {
	  global.clients[i].recv++;
	  if (global.clients[i].bufferIndex < (int)(countof(global.clients[i].buffer)-1)) {
	    global.clients[i].bufferIndex++;
	  } else {
	    log_printf("network_processClientData: failed: buffer exhausted\n");
	    network_removeConnection(i);
	    done = 1;
	  }
	} else {
	  if (len == 0) {
	    log_printf("network_processClientData: failed: %s\n", log_getError());
	    network_removeConnection(i);
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
  int maxFD = global.serverFD;

#ifdef ROIDSERVER_DASHBOARD
  FD_SET(global.dashboardFD, read_fds);
  if (global.dashboardFD > maxFD) {
    maxFD = global.dashboardFD;
  }
#endif

  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id) {
      if (global.clients[i].socketFD > maxFD) {
	maxFD = global.clients[i].socketFD;
      }
      FD_SET(global.clients[i].socketFD, read_fds);
    }
  }

#ifdef ROIDSERVER_DASHBOARD
  for (i = 0; i < countof(global.dashboard); i++) {
    if (global.dashboard[i].socketFD >= 0) {
      if (global.dashboard[i].socketFD > maxFD) {
	maxFD = global.dashboard[i].socketFD;
      }
      FD_SET(global.dashboard[i].socketFD, read_fds);
    }
  }
#endif

  return maxFD;
}


static void
network_addConnection(int socketFD)
{
  unsigned int i;

  struct sockaddr_in addr;
  socklen_t addr_size = sizeof(addr);

  getpeername(socketFD, (struct sockaddr *)&addr, &addr_size);

  for (i = 0; i < global.denyList.num; i++) {
    if (network_matchAddr(global.denyList.entries[i].addr, addr.sin_addr.s_addr, global.denyList.entries[i].mask)) {
#ifdef AMIGA
    log_printf("network_addConnection: blocked connection from: %s %x\n", Inet_NtoA(addr.sin_addr.s_addr), addr.sin_addr.s_addr);
#else
    log_printf("network_addConnection: blocked connection from: %s %x\n", inet_ntoa(addr.sin_addr), addr.sin_addr.s_addr);
#endif
      return;
    }
  }

  for (i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id == 0) {
      memset(&global.clients[i], 0, sizeof(global.clients[i]));
      global.clients[i].id = 1;
      global.clients[i].socketFD = socketFD;
      global.clients[i].addr = addr;
      time(&global.clients[i].connected);
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




int
main(int argc, char** argv)
{
  global.loggingEnabled = 1;

  if (argc == 2) {
    global.loggingEnabled = !(strcmp(argv[1], "--quiet") == 0);
  }

#ifdef AMIGA
  SocketBase = OpenLibrary((APTR)"bsdsocket.library", 4);
  if (!SocketBase) {
    network_exit(1);
  }
#endif

#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2,2), &wsaData);
#endif


#ifdef ROIDSERVER_DASHBOARD
  if (!dashboard_loadRootPath()) {
    log_printf("main: failed to load root\n");
    return 1;
  }

  dashboard_renderReloadHTML(0); // load configuration

  unsigned int i;
  for (i = 0; i < countof(global.dashboard); i++) {
    global.dashboard[i].socketFD = -1;
  }

  global.dashboardFD = network_serverTCP(ROIDSERVER_DASHBOARD_PORT);
  if (global.dashboardFD < 0) {
    network_exit(2);
  }
#else
  main_loadDenyList();
#endif

  global.serverFD = network_serverTCP(ROIDSERVER_GAME_PORT);

  if (global.serverFD < 0) {
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
      log_printf("main: select failed: %s\n", log_getError());
      network_exit(3);
      break;
    default:
      if (FD_ISSET(global.serverFD, &read_fds)) {
	int clientFD = network_accept(global.serverFD);
	if (clientFD < 0) {
	  log_printf("main: network_accept failed: %s\n", log_getError());
	} else {
	  network_addConnection(clientFD);
	}
      }

#ifdef ROIDSERVER_DASHBOARD
      if (FD_ISSET(global.dashboardFD, &read_fds)) {
	int clientFD = network_accept(global.dashboardFD);
	if (clientFD < 0) {
	  log_printf("main: network_accept failed: %s\n", log_getError());
	} else {
	  network_addDashboardConnection(clientFD);
	}
      }
      dashboard_processClientData(&read_fds);
      dashboard_sendClientData();
#endif

      network_processClientData(&read_fds);
      network_sendClientData();
    }
  } while (1);


  network_exit(0);
}
