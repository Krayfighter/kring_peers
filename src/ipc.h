
#include "sys/socket.h"
#include "sys/un.h"

extern const char *daemon_socket_path;
extern struct sockaddr_un daemon_socket_addr;

extern const char *frontend_socket_path;
extern struct sockaddr_un frontend_socket_addr;

void init_ipc();
// returns (sockaddr_un){ 0 } on error
// clears errno before recvfrom syscall
struct sockaddr_un unix_dgram_recvfrom(
  int socket, void *buffer, size_t buffer_size, size_t *read_size
);

