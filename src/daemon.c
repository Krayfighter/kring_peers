
#include "stdio.h"
#include "string.h"
#include "errno.h"
#include "stdlib.h"
#include "unistd.h"
#include "stdbool.h"
#include "assert.h"
// #include "pthread.h"
#include "poll.h"
#include "ctype.h"

#include "sys/socket.h"
// #include "libiptc/libiptc.h" // TODO use port mapping to allow capture of regular services through the network

#include "arpa/inet.h"
#include "netinet/in.h"

#include "ipc.h"

char *local_error_string = NULL;

typedef struct{
  struct in_addr address;
  uint16_t recv_port;
} Peer;

// returns -1 on error
int open_daemon_listener(FILE *logger) {
  unlink(daemon_socket_path);

  int daemon_socket = socket(PF_UNIX, SOCK_DGRAM, 0);
  if (daemon_socket == -1) {
    if (logger != NULL) {
      fprintf(logger, "Failed to create unix socket -> %s\n", strerror(errno));
    }
    return -1;
  }

  socklen_t daemon_addr_size = SUN_LEN(&daemon_socket_addr);
  int unix_socket_bind_result = bind(daemon_socket, (struct sockaddr *)&daemon_socket_addr, daemon_addr_size);
  if (unix_socket_bind_result == -1) {
    if (logger != NULL) {
      fprintf(logger, "Failed to bind unix socket to `%s`\n", daemon_socket_path);
    }
    return -1;
  }

  return daemon_socket;
}

// Attempt to send a connection request packet to a peer
// given an address (and port) in the form of a string
// like would be received from the frontend
//
// on error, this functions sets errno, and local_error_string
// and returns -1 (as oposed to 0 for success)
int attemp_peer_connect_by_string(int udp_socket, char *peer_addr_str, size_t addr_len) {
  assert(udp_socket > -1);
  assert(peer_addr_str != NULL);
  if (addr_len == 0) {
    errno = EINVAL;
    local_error_string = "Cannot parse address string of size 0";
    return -1;
  }
  void *colon_pointer = memchr(peer_addr_str, ':', addr_len);
  if (colon_pointer == NULL) {
    errno = EINVAL;
    local_error_string = "Expected a colon to seperate the address from the port";
    return -1;
  }
  size_t colon_index = ((intptr_t)colon_pointer) - ((intptr_t)peer_addr_str);
  
  struct in_addr peer_address;
  peer_addr_str[colon_index] = '\0';
  int address_parse_result = inet_aton(peer_addr_str, &peer_address);
  peer_addr_str[colon_index] = ':';
  if (address_parse_result == 0) {
    local_error_string = "Failed to parse peer_addr_str";
    return -1;
  }

  size_t port_len = addr_len - (colon_index + 1);
  if (port_len > 5) {
    PORT_NUMBER_TOO_LARGE: {};
    errno = EINVAL;
    local_error_string = "Port string exceeds maximum port size";
    return -1;
  }
  size_t port_number = 0;
  size_t power_of_ten = 1;
  for (size_t i = 1; i < port_len; i += 1) {
    char chr = peer_addr_str[addr_len-i];
    if (!isdigit(chr)) {
      errno = EINVAL;
      local_error_string = "Found non-numberic character in port number";
      fprintf(stderr, "DBG: iteration %lu | non-numberic %c\n", i, chr);
      return -1;
    }
    port_number += ('0' - chr) * power_of_ten;
    power_of_ten *= 10;
  }
  if (port_number > 0xffff) { goto PORT_NUMBER_TOO_LARGE; }
  fprintf(stderr, "DBG: port number is %u\n", (uint16_t)port_number);

  // #define CONNECTION_MESSAGE_BUFFER_SIZE 256
  // char connection_message_buffer[CONNECTION_MESSAGE_BUFFER_SIZE];
  // size_t message_len;

  const char connection_message[] = "connection-init:";

  struct sockaddr_in peer_sock_addr = {
    .sin_family = AF_INET,
    .sin_addr = peer_address,
    .sin_port = port_number,
  };

  ssize_t write_size = sendto(
    udp_socket, connection_message, sizeof(connection_message), 0x0,
    (struct sockaddr *)&peer_sock_addr, sizeof(peer_sock_addr)
  );
  if (write_size == -1) {
    local_error_string = "Failed to send connection packet to peer (socket error)";
    return -1;
  }
  assert(write_size == sizeof(connection_message));

  return 0;
}

typedef enum {
  FRONT_CMD_INVALID,
  FRONT_CMD_QUIT,
  FRONT_CMD_ECHO,
  FRONT_CMD_CONNECT,
} FrontendCommandType;

typedef struct {
  char *body;
  size_t body_len;
  FrontendCommandType cmd_type;
  struct sockaddr_un client_addr;
} FrontendCommand;

#define FRONTEND_PACKET_BUFFER_SIZE 4096
char frontend_packet_buffer[FRONTEND_PACKET_BUFFER_SIZE];

// returns -1 on error, 0 on success
int read_frontend_packet(int fd, FILE *logger, FrontendCommand *returned_command) {
  memset(frontend_packet_buffer, 0, FRONTEND_PACKET_BUFFER_SIZE);
  struct sockaddr_un client_addr  = {
    .sun_family = AF_UNIX
  };
  socklen_t client_addr_len = sizeof(client_addr);
  ssize_t read_size = recvfrom(
    fd, frontend_packet_buffer, FRONTEND_PACKET_BUFFER_SIZE - 1, 0x0,
    (struct sockaddr *)&client_addr, &client_addr_len
  );
  assert(client_addr_len != 0);

  if (read_size == -1) {
    if (logger != NULL) { fprintf(logger, "Failed to read packet from socket -> %s", strerror(errno)); }
    return -1;
  }
  returned_command->client_addr = client_addr;

  returned_command->cmd_type = FRONT_CMD_INVALID;
  returned_command->body = NULL;
  returned_command->body_len = 0;
  // TODO make memory safety better

  assert(strncmp("echo:", "echo:", 5) == 0);
  if (strncmp("echo:", frontend_packet_buffer, 5) == 0) {
    returned_command->cmd_type = FRONT_CMD_ECHO;
    returned_command->body = frontend_packet_buffer + 5;
    returned_command->body_len = read_size - 5;
  }else if (strncmp("quit:", frontend_packet_buffer, 5) == 0) {
    returned_command->cmd_type = FRONT_CMD_QUIT;
  }else if (strncmp("connect:", frontend_packet_buffer, 8) == 0) {
    returned_command->cmd_type = FRONT_CMD_CONNECT;
    returned_command->body = frontend_packet_buffer + 8;
    returned_command->body_len = read_size - 8;
  }

  return 0;
}

// returns -1 on error, positive fd on success
int open_udp_server(FILE *logger) {
  int listener = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (listener == -1) {
    if (logger != NULL) { fprintf(logger, "Failed to open udp socket -> %s\n", strerror(errno)); }
    return -1;
  }

  struct sockaddr_in bind_address = {
    .sin_port = 12000,
    .sin_addr = INADDR_ANY,
    .sin_family = AF_INET,
  };
  int bind_result = bind(listener, (struct sockaddr *)&bind_address, sizeof(struct sockaddr_in));
  if (bind_result == -1) {
    if (logger != NULL) { fprintf(logger, "Failed to bind udp socket to 0.0.0.0:12000 -> %s\n", strerror(errno)); }
    return -1;
  }

  return listener;
}

// returns -1 on error, 0 on success
//
// replaces `buffer_len` with the number of bytes read
int read_udp_packet(int fd, void *buffer, size_t *buffer_len, FILE *logger) {

  struct sockaddr_in client_addr = { .sin_family = AF_INET };
  socklen_t addr_len = sizeof(struct sockaddr_in);
  ssize_t read_size = recvfrom(fd, buffer, *buffer_len, 0x0, (struct sockaddr *)&client_addr, &addr_len);

  if (read_size == -1) {
    // fprintf(stderr, "FATAL: failed call to recvfrom -> %s\n", strerror(errno));
    if (logger != NULL) { fprintf(logger, "Failed call to recvfrom -> %s\n", strerror(errno)); }
    return -1;
  }

  *buffer_len = (size_t)read_size;
  return 0;

  // char *peer_addr_string = inet_ntoa(client_addr.sin_addr);

  // fprintf(stdout, "INFO: recieved packet - exiting\n");
  // fprintf(
  //   stdout,
  //   "INFO: Recieved packet from peer -> address %s on port %u\naddr_len = %u\n",
  //   // client_addr.sin_addr.s_addr        & ~0x000000ff,
  //   // (client_addr.sin_addr.s_addr >> 2) & ~0x000000ff,
  //   // (client_addr.sin_addr.s_addr >> 4) & ~0x000000ff,
  //   // (client_addr.sin_addr.s_addr >> 6) & ~0x000000ff,
  //   peer_addr_string,
  //   client_addr.sin_port,
  //   addr_len
  // );

  // fprintf(stdout, "packet info (string) ->");
  // fwrite(packet_buffer, 0, read_size, stdout);
  // fprintf(stdout, "\n");
  // fflush(stdout);

}

int main() {
  init_ipc();

  int listener = open_udp_server(stderr);
  int daemon_listener = open_daemon_listener(stderr);

  // socklen_t socket_addr_size = sizeof(struct sockaddr_in);
  // int result = getsockname(listener, (struct sockaddr *)&bind_address, &socket_addr_size);
  // if (result == -1) {
  //   fprintf(stderr, "FATAL: failed to get socket address info on listener socket -> %s\n", strerror(errno));
  //   return EXIT_FAILURE;
  // }

  fprintf(stdout, "INFO: servering at 0.0.0.0:12000\n");

  Peer active_peers[32];
  size_t active_peer_count = 0;

  char packet_buffer[0xffff]; // max size of udp packet is the max size of a uint16_t
  // char msg_name_buffer[256];

  struct pollfd file_descriptors[34] = {
    (struct pollfd) { .fd = daemon_listener, .events = POLLIN },
    (struct pollfd) { .fd = listener, .events = POLLIN },
  };
  const size_t file_descriptor_count = sizeof(file_descriptors) / sizeof(struct pollfd);
  
  while (true) {
    for (size_t i = 0; i < file_descriptor_count; i += 1) {
      file_descriptors[i].revents = 0;
    }
    int event_count = poll(file_descriptors, file_descriptor_count, -1);
    assert(event_count != 0); // impossible with current (infinite) timeout

    if (file_descriptors[0].revents != 0) {
      FrontendCommand cmd;
      int frontend_command_read_result = read_frontend_packet(daemon_listener, stderr, &cmd);
      if (frontend_command_read_result == -1) {
        fprintf(stderr, "Error reading from unix socket -> continuing\n");
      }else {
        switch (cmd.cmd_type) {
          case FRONT_CMD_ECHO: {
            assert(cmd.body != NULL);
            fprintf(stdout, "INFO: recieved echo command from client, echoing message\n-> ");
            fwrite(cmd.body, 1, cmd.body_len, stdout);
            fprintf(stdout, "\n");

            ssize_t write_size = sendto(
              daemon_listener, cmd.body, cmd.body_len, 0x0,
              (const struct sockaddr *)&cmd.client_addr, SUN_LEN(&cmd.client_addr)
            );
            if (write_size == -1) {
              fprintf(stderr, "Failed to send packet back to frontend -> %s\n", strerror(errno));
            }
          }; break;
          case FRONT_CMD_QUIT: {
            fprintf(stdout, "INFO: received QUIT command from frontend - exiting");
            goto AFTER_MAINLOOP;
          }; break;
          case FRONT_CMD_INVALID: {
            fprintf(stdout, "WARN: received invalid command from frontend\n");
            fwrite(packet_buffer, 1, FRONTEND_PACKET_BUFFER_SIZE, stdout);
            fprintf(stdout, "\n");
          }; break;
          case FRONT_CMD_CONNECT: {
            if (active_peer_count == 32) {
              fprintf(stderr, "Failed to connect to peer; The peer limit has been reached\n");
              continue;
            }
            fprintf(stdout, "INFO: sending connection request packet to new peer\n");
            local_error_string = NULL;
            int result = attemp_peer_connect_by_string(listener, cmd.body, cmd.body_len);
            if (result == -1) {
              if (local_error_string != NULL) {
                fprintf(stderr, "Failed to send connection packet to peer -> %s\n", local_error_string);
              }else {
                fprintf(stderr, "Failed to send connection packet to peer -> %s\n", strerror(errno));
              }
              const char frontend_error_message[] = "errlog:Failed to send connection request to peer";
              ssize_t write_size = sendto(
                daemon_listener, frontend_error_message, sizeof(frontend_error_message), 0x0,
                (struct sockaddr *)&client_socket_addr, sizeof(client_socket_addr)
              );
              if (write_size == -1) {
                fprintf(stderr, "Failed to send error packket to client -> %s\n", strerror(errno));
              }
            }
          }
        }
      }
    }else if (file_descriptors[1].revents != 0) {
      size_t read_bytes = sizeof(packet_buffer);
      int result = read_udp_packet(listener, packet_buffer, &read_bytes, stderr);

      if (result != -1) {
        fprintf(stdout, "TEST: recieved packet with contents -> ");
        fwrite(packet_buffer, 1, read_bytes, stdout);
        fprintf(stdout, "\n");
      }else {
        fprintf(stderr, "Error on udp socket - continuiung\n");
      }
    }

  }
  AFTER_MAINLOOP: {};

  close(listener);
  close(daemon_listener);
  unlink(daemon_socket_path);

  return EXIT_SUCCESS;
}

