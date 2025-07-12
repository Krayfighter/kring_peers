
#include "stdbool.h"
#include "string.h"
#include "errno.h"
#include "assert.h"

#include "sys/socket.h"
#include "sys/un.h"

// #include "ipc.h"


const char *daemon_socket_path = "/tmp/kringpeers_daemon";
struct sockaddr_un daemon_socket_addr = {
  .sun_family = AF_UNIX
};

const char *client_socket_path = "/tmp/kringpeers_client";
struct sockaddr_un client_socket_addr = {
  .sun_family = AF_UNIX
};

void init_ipc() {
  strcpy(daemon_socket_addr.sun_path, daemon_socket_path);
  strcpy(client_socket_addr.sun_path, client_socket_path);
}

struct sockaddr_un unix_dgram_recvfrom(
  int socket, void *buffer, size_t buffer_size, size_t *read_size
) {
  struct sockaddr_un client_addr  = {
    .sun_family = AF_UNIX
  };
  socklen_t client_addr_len = sizeof(client_addr);

  errno = 0;
  ssize_t _read_size = recvfrom(
    socket, buffer, buffer_size, 0x0,
    (struct sockaddr *)&client_addr, &client_addr_len
  );
  if (errno == 0) { return (struct sockaddr_un){ 0 }; }
  if (_read_size == -1) { return (struct sockaddr_un){ 0 }; }
  assert(client_addr_len != 0);

  *read_size = (size_t)_read_size;
  return client_addr;
}

