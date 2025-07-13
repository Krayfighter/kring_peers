
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
    errno = EINVAL;
    local_error_string = "Port string exceeds maximum port size";
    return -1;
  }
  size_t port_number = 0;
  size_t power_of_ten = 1;
  for (size_t i = 1; i <= port_len; i += 1) {
    char chr = peer_addr_str[addr_len-i];
    if (!isdigit(chr)) {
      errno = EINVAL;
      local_error_string = "Found non-numberic character in port number";
      return -1;
    }
    port_number += (chr - '0') * power_of_ten;
    power_of_ten *= 10;
  }
  if (port_number > 0xffff) {
    errno = EINVAL;
    local_error_string = "Port number exceeds 16 bit unsigned int size";
    return -1;
  }

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
  FRONT_CMD_PRINT,
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
  // memset(frontend_packet_buffer, 0, FRONTEND_PACKET_BUFFER_SIZE);
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
  frontend_packet_buffer[read_size] = '\0';
  returned_command->client_addr = client_addr;

  returned_command->cmd_type = FRONT_CMD_INVALID;
  returned_command->body = frontend_packet_buffer;
  returned_command->body_len = read_size;
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
  }else if (strncmp("print:", frontend_packet_buffer, 6) == 0) {
    returned_command->cmd_type = FRONT_CMD_PRINT;
    returned_command->body = frontend_packet_buffer + 6;
    returned_command->body_len = read_size - 6;
  }else {
    fprintf(stderr, "DBG: unmatch packet command -> %s\n", frontend_packet_buffer);
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
// replaces `client_addr` with the address of the client from which the packet was received
int read_udp_packet(int fd, void *buffer, size_t *buffer_len, struct sockaddr_in *client_addr, FILE *logger) {
  
  // struct sockaddr_in client_addr = { .sin_family = AF_INET };
  *client_addr = (struct sockaddr_in){ .sin_family = AF_INET };
  socklen_t addr_len = sizeof(struct sockaddr_in);
  ssize_t read_size = recvfrom(fd, buffer, *buffer_len, 0x0, (struct sockaddr *)&client_addr, &addr_len);

  if (read_size == -1) {
    // fprintf(stderr, "FATAL: failed call to recvfrom -> %s\n", strerror(errno));
    if (logger != NULL) { fprintf(logger, "Failed call to recvfrom -> %s\n", strerror(errno)); }
    return -1;
  }

  *buffer_len = (size_t)read_size;
  return 0;
}

// compares `sockaddr` with each of the members of `peer_array` to identify
// if it is contained by that array
bool array_contains_sockaddr(Peer *peer_array, size_t peer_count, struct sockaddr_in *sockaddr) {
  for (size_t i = 0; i < peer_count; i += 1) {
    if (
      peer_array[i].address.s_addr == sockaddr->sin_addr.s_addr
      && peer_array[i].recv_port == sockaddr->sin_port
    ) { return true; }
  }
  return false;
}

int main() {
  init_ipc();

  int listener = open_udp_server(stderr);
  int daemon_listener = open_daemon_listener(stderr);

  fprintf(stdout, "INFO: servering at 0.0.0.0:12000\n");

  #define ACTIVE_PEERS_MAX 32
  Peer active_peers[ACTIVE_PEERS_MAX];
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
            if (active_peer_count == ACTIVE_PEERS_MAX) {
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
          }; break;
          case FRONT_CMD_PRINT: {
            // this buffer must be large enough to print one
            // copy of every active peer
            //     address    | port
            // 4  |4  |4  |4  |5
            // xxx.xxx.xxx.xxx:xxxxx <- 21 characters (bytes)
            // plus the newline at the end.
            // Thus the minimum storage required for this
            // buffer is 22 * ACTIVE_PEERS_MAX + 1 for a null byte
            //   + strlen("print:")
            const char message_prefix[] = "print:";
            const size_t print_cmd_buffer_size = 22 * ACTIVE_PEERS_MAX + 1 + strlen(message_prefix);
            char *print_cmd_buffer = alloca(print_cmd_buffer_size);

            size_t message_len = 0;
            size_t remaining_buffer_space = print_cmd_buffer_size;
            int _result = snprintf(print_cmd_buffer, print_cmd_buffer_size, message_prefix);
            assert(_result == strlen(message_prefix));
            message_len += strlen(message_prefix);
            remaining_buffer_space -= strlen(message_prefix);

            for (size_t i = 0; i < active_peer_count; i += 1) {
              Peer *peer = &active_peers[i];
              int write_size = snprintf(
                print_cmd_buffer + message_len, remaining_buffer_space,
                "%u.%u.%u.%u:%u\n",
                (uint8_t)((peer->address.s_addr >> 3 * 8) & 0x000000ff),
                (uint8_t)((peer->address.s_addr >> 2 * 8) & 0x000000ff),
                (uint8_t)((peer->address.s_addr >> 1 * 8) & 0x000000ff),
                (uint8_t)((peer->address.s_addr)          & 0x000000ff),
                peer->recv_port
              );
              assert(write_size > -1); // this should always be true of ISO C
              // this should always be true unless there is a logic error in this code
              assert((size_t)write_size <= remaining_buffer_space);

              message_len += write_size;
              remaining_buffer_space -= write_size;
            }

            ssize_t write_size = sendto(
              daemon_listener, print_cmd_buffer, message_len + 1, 0x0,
              (struct sockaddr *)&client_socket_addr, SUN_LEN(&client_socket_addr)
            );
            if (write_size == -1) {
              fprintf(stderr, "Failed to write result of print: command to frontend socket -> %s\n", strerror(errno));
            }else {
              assert((size_t)write_size == message_len + 1);
            }
            fprintf(stderr,
              "DBG: peer_count: %lu | wrote bytes %lu | message contents %s\n",
              active_peer_count, write_size, print_cmd_buffer
            );
          }; break;
        }
      }
    }else if (file_descriptors[1].revents != 0) {
      size_t read_bytes = sizeof(packet_buffer) - 1;
      struct sockaddr_in client_address;
      int result = read_udp_packet(listener, packet_buffer, &read_bytes, &client_address, stderr);
      if (result == -1) {
        fprintf(stderr, "Error on udp socket - continuing");
        continue;
      }
      packet_buffer[read_bytes] = '\0';

      if (strncmp("connection-init:", packet_buffer, 16) == 0) {
        fprintf(stderr, "DBG: received peer connection init packet\n");
        if (array_contains_sockaddr(active_peers, active_peer_count, &client_address)) {
          const char response[] = "connection-ack:already_connected";
          ssize_t write_size = sendto(
            listener, response, sizeof(response), 0x0,
            (struct sockaddr *)&client_address, sizeof(client_address)
          );
          if (write_size == -1) {
            fprintf(stderr, "Failed to response to peer that was already connected -> %s\n", strerror(errno));
          }
        }else {
          active_peers[active_peer_count] = (Peer) {
            .address = client_address.sin_addr,
            .recv_port = client_address.sin_port
          };
          active_peer_count += 1;
        }
      }else if (strncmp("connection-ack:", packet_buffer, 15) == 0) {
        fprintf(stderr, "DBG: received peer connection acknowledgement packet\n");
        if (!array_contains_sockaddr(active_peers, active_peer_count, &client_address)) {
          if (active_peer_count == ACTIVE_PEERS_MAX) {
            fprintf(stderr, "WARN: received a \"connection-ack:\" message from peer while currently connected to the maximum number of peers\n");
            continue;
          }
          active_peers[active_peer_count] = (Peer) {
            .address = client_address.sin_addr,
            .recv_port = client_address.sin_port
          };
          active_peer_count += 1;
        }
      }else {
        fprintf(stderr, "DBG: unhandled/invalid packet header from peer -> %s\n", packet_buffer);
      }
      // TODO implement a method noting that a peer has not responded to a "connection-init:" message
    }

  }
  AFTER_MAINLOOP: {};

  close(listener);
  close(daemon_listener);
  unlink(daemon_socket_path);

  return EXIT_SUCCESS;
}

