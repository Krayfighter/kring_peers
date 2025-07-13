
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

const char *frontend_socket_path = "/tmp/kringpeers_frontend";
struct sockaddr_un frontend_socket_addr = {
  .sun_family = AF_UNIX
};

void init_ipc() {
  strcpy(daemon_socket_addr.sun_path, daemon_socket_path);
  strcpy(frontend_socket_addr.sun_path, frontend_socket_path);
}

struct sockaddr_un unix_dgram_recvfrom(
  int socket, void *buffer, size_t buffer_size, size_t *read_size
) {
  struct sockaddr_un frontend_addr  = {
    .sun_family = AF_UNIX
  };
  socklen_t frontend_addr_len = sizeof(frontend_addr);

  errno = 0;
  ssize_t _read_size = recvfrom(
    socket, buffer, buffer_size, 0x0,
    (struct sockaddr *)&frontend_addr, &frontend_addr_len
  );
  if (errno == 0) { return (struct sockaddr_un){ 0 }; }
  if (_read_size == -1) { return (struct sockaddr_un){ 0 }; }
  assert(frontend_addr_len != 0);

  *read_size = (size_t)_read_size;
  return frontend_addr;
}

