#ifndef AMIGA
#ifndef _WIN32
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#ifndef _MSC_VER
#include <unistd.h>
char* strerror(int errnum);
#endif

#endif

#define _EAGAIN      35
#define _EINPROGRESS 36
#define _EALREADY    37
#define _EISCONN     56

#ifndef AMIGA
#ifdef _WIN32
#define debug_errno(x) debug_printInt(x, WSAGetLastError());
#else
#define debug_print(x) puts(x)
#define debug_errno(x) debug_print(x);debug_print(strerror(errno));
#define debug_printInt(x, y) printf(x); printf("%d\n", (int)y)
#endif
#else
#define debug_errno(x) debug_printInt(x, Errno());
#endif

#define countof(x) (sizeof(x)/sizeof(x[0]))

typedef struct {
  int socketFD;
  uint32_t id;
  char buffer[255];
  int bufferIndex;
} client_connection_t;

typedef struct {
  int serverFD;
  client_connection_t clients[2];
} global_t;

global_t global;

//static
const int ONE = 1;


static void
network_assertValidClient(int index)
{
  if (index < 0 || index >= (int)countof(global.clients)) {
    debug_printInt("network_assertValidClient: invalid client: ", index);
    exit(1);
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

  memset(&sa, 0, sizeof(struct sockaddr_in));
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
  for (int i = 0; i < (int)countof(global.clients); i++) {
    if (id == global.clients[i].id) {
      debug_printInt("network_processClientData: removing connection slot: ", i);
      network_closeSocket(global.clients[i].socketFD);
      global.clients[i].id = 0;
    }
  }
}


static int
network_accept(int serverFD)
{
  int accept_fd;
  struct sockaddr_in isa;

#if defined(AMIGA) || defined(_WIN32)
  int addr_size;//,sent_data;// char data_buffer[80];
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


static void
network_sendClientData(void)
{
  for (size_t i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id != 0 && global.clients[i].bufferIndex > 0) {
      for (size_t d = 0; d < countof(global.clients); d++) {
	if (global.clients[i].id == global.clients[d].id && i != d) {
	  if (send(global.clients[d].socketFD, global.clients[i].buffer, global.clients[i].bufferIndex, MSG_DONTWAIT) < 0) {
	    if (errno != EAGAIN) {
	      network_removeConnection(i);
	    }
	  }
	  global.clients[i].bufferIndex = 0;
	  break;
	}
      }
    }
  }
}


static void
network_processClientData(fd_set *read_fds, fd_set* except_fds)
{
  (void)except_fds;

  for (size_t i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id && FD_ISSET(global.clients[i].socketFD, read_fds)) {
      int done = 0;
      do {
	int len = recv(global.clients[i].socketFD, &global.clients[i].buffer[global.clients[i].bufferIndex], 1, MSG_DONTWAIT);
	if (len > 0) {
	  if (global.clients[i].bufferIndex < (int)(countof(global.clients[i].buffer)-1)) {
	    global.clients[i].bufferIndex++;
	  }
	} else {
	  if (len == 0) {
	    network_removeConnection(i);
	  }
	  done = 1;
	}
      } while (!done);
    }
  }
}


static int
network_setupFDS(fd_set *read_fds, fd_set *write_fds, fd_set *except_fds)
{
  FD_ZERO(read_fds);
  FD_ZERO(write_fds);
  FD_ZERO(except_fds);

  FD_SET(global.serverFD, read_fds);
  FD_SET(global.serverFD, except_fds);

  int maxFD = global.serverFD;

  for (size_t i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id) {
      if (global.clients[i].socketFD > maxFD) {
	maxFD = global.clients[i].socketFD;
      }
      FD_SET(global.clients[i].socketFD, read_fds);
      FD_SET(global.clients[i].socketFD, except_fds);
    }
  }

  return maxFD;
}


static void
network_addConnection(int socketFD)
{
  for (size_t i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id == 0) {
      global.clients[i].id = 1;
      global.clients[i].socketFD = socketFD;
      global.clients[i].bufferIndex = 0;
      debug_printInt("network_addConnection: new client slot: ", i);
      return;
    }
  }

  debug_print("network_addConnection: no free slots\n");
}


int
main(int argc, char** argv)
{
  (void)argc;
  (void)argv;

  global.serverFD = network_serverTCP(8000);

  if (global.serverFD < 0) {
    return 1;
  }

  fd_set read_fds;
  fd_set write_fds;
  fd_set except_fds;

  int maxFD = 0;

  do {

    maxFD = network_setupFDS(&read_fds, &write_fds, &except_fds);

    int task = select(maxFD + 1, &read_fds, NULL/*&write_fds*/, &except_fds, NULL);

    switch (task) {
    case -1:
      debug_errno("main: select failed\n");
      return 2;
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

      if (FD_ISSET(global.serverFD, &except_fds)) {
	debug_print("main: server exception\n");
      }

      network_processClientData(&read_fds, &except_fds);
      network_sendClientData();
    }
  } while (1);



  return 0;
}
