
#ifndef _WIN32
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#else
#include <winsock.h>
#define MSG_DONTWAIT 0
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef AMIGA
#include <proto/exec.h>
#include <proto/socket.h>
#endif

#include <stdint.h>

#define ROIDSERVER_NUM_PING_PACKETS 8
#define ROIDSERVER_MAX_CLIENTS 16
#define ROIDSERVER_READY_STATE (ROIDSERVER_NUM_PING_PACKETS+1)


#ifndef AMIGA
#ifdef _WIN32
#define debug_errno(x) printf(x"%s\n", WSAGetLastError());
#else
#define debug_errno(x) printf(x"%s\n", strerror(errno));
#endif
#else
#define debug_errno(x) printf(x"%s\n", Errno());
#endif

const char*
debug_itoa(int x);

#define countof(x) (sizeof(x)/sizeof(x[0]))

typedef struct {
  int socketFD;
  uint32_t id;
  uint32_t state;
  char buffer[255];
  int bufferIndex;
} client_connection_t;


typedef struct {
  int serverFD;
  client_connection_t clients[ROIDSERVER_MAX_CLIENTS];
} global_t;

static global_t global;
static const int ONE = 1;
#ifdef AMIGA
struct Library *SocketBase = 0;
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
    printf("network_assertValidClient: invalid client: %d\n", index);
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
  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    if (id == global.clients[i].id) {
      printf("network_processClientData: removing connection slot: %d\n", i);
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
    printf("network_setId: assigning %d to %x\n", index, id);
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
  printf("network_processId: %d: %x\n", index, packet);
  global.clients[index].state++;
  if (!network_setId(index, packet)) {
    network_removeConnection(index);
  }
}


static void
network_processPing(int index)
{
  network_assertValidClient(index);
  uint32_t packet = network_getPacket(index);
  printf("network_processPing: %d: %x\n", index, packet);
  if (packet == 0xdeadbeef) {
    global.clients[index].state++;
    if (send(global.clients[index].socketFD, (void*)&packet, sizeof(packet), MSG_DONTWAIT) != sizeof(packet)) {
      network_removeConnection(index);
    }

  } else {
    network_removeConnection(index);
  }
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
      } else if (global.clients[i].state == ROIDSERVER_READY_STATE) {
	done = 1;
	unsigned int d;
	for (d = 0; d < countof(global.clients); d++) {
	  if (global.clients[i].id == global.clients[d].id &&  global.clients[d].state == ROIDSERVER_READY_STATE && i != d) {
	    if (send(global.clients[d].socketFD, global.clients[i].buffer, global.clients[i].bufferIndex, MSG_DONTWAIT) < 0) {
	      network_removeConnection(i);
	    }
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
    if (global.clients[i].id > 0 &&( FD_ISSET(global.clients[i].socketFD, read_fds))) {
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
network_setupFDS(fd_set *read_fds)
{
  FD_ZERO(read_fds);
  FD_SET(global.serverFD, read_fds);

  int maxFD = global.serverFD;

  unsigned int i;
  for (i = 0; i < countof(global.clients); i++) {
    if (global.clients[i].id) {
      if (global.clients[i].socketFD > maxFD) {
	maxFD = global.clients[i].socketFD;
      }
      FD_SET(global.clients[i].socketFD, read_fds);
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
      global.clients[i].id = 1;
      global.clients[i].state = 0;
      global.clients[i].socketFD = socketFD;
      global.clients[i].bufferIndex = 0;
      printf("network_addConnection: new client slot: %d fd: %d\n", i, socketFD);
      return;
    }
  }

  printf("network_addConnection: no free slots\n");
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

  global.serverFD = network_serverTCP(8000);

  if (global.serverFD < 0) {
    network_exit(2);
  }

  fd_set read_fds;
  int maxFD = 0;

  printf("roidserver: ready\n");

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

      network_processClientData(&read_fds);
      network_sendClientData();
    }
  } while (1);


  network_exit(0);
}
