
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "errno.h"
#include "stdbool.h"
#include "unistd.h"
#include "assert.h"
#include "poll.h"

#include "sys/socket.h"
#include "sys/un.h"

#include "ipc.h"


// find first occurence of `delim` in source
// returns -1 on error (such as delim not found)
ssize_t str_find(const char *source, size_t source_len, char delim) {
  for (size_t i = 0; i < source_len; i += 1) {
    if (source[i] == delim) { return i; }
  }
  return -1;
}


int main() {

  init_ipc();
  unlink(client_socket_path);

  int daemon_socket = socket(PF_UNIX, SOCK_DGRAM, 0);
  if (daemon_socket == -1) {
    fprintf(stderr, "FATAL: failed to create unix socket -> %s\n", strerror(errno));
    return EXIT_FAILURE;
  }

  int daemon_connection_bind_result = bind(daemon_socket, (struct sockaddr *)&client_socket_addr, SUN_LEN(&client_socket_addr));
  if (daemon_connection_bind_result == -1) {
    fprintf(
      stderr,
      "FATAL: failed to bind UNIX socket to %s -> %s\n",
      daemon_socket_path, strerror(errno)
    );
    return EXIT_FAILURE;
  }

  // const char *test_message = "quit:TEST from kring_peers frontend";
  // ssize_t write_size = sendto(
  //   daemon_socket, test_message, strlen(test_message), 0x0,
  //   (struct sockaddr *)&daemon_socket_addr, sizeof(daemon_socket_addr)
  // );
  // if (write_size == -1) {
  //   fprintf(stderr, "FATAL: failed to write to unix socket at %s -> %s", daemon_socket_path, strerror(errno));
  //   return EXIT_FAILURE;
  // }

  #define DAEMON_READ_BUFFER_SIZE 4096
  char daemon_read_buffer[DAEMON_READ_BUFFER_SIZE];

  #define STDIN_READ_BUFFER_SIZE 1024
  char stdin_buffer[STDIN_READ_BUFFER_SIZE];

  struct pollfd file_descriptors[] = {
    (struct pollfd){ .fd = STDIN_FILENO, .events = POLLIN, .revents = 0x0},
    (struct pollfd){ .fd = daemon_socket, .events = POLLIN, .revents = 0x0},
  };
  const size_t file_descriptor_count = sizeof(file_descriptors) / sizeof(struct pollfd);

  while (true) {
    // stdin_pollfd.revents = 0x0;
    for (size_t i = 0; i < file_descriptor_count; i += 1) {
      file_descriptors[i].revents = 0x0;
    }
    fprintf(stdout, "\r> ");
    fflush(stdout);

    int poll_result = poll(file_descriptors, file_descriptor_count, -1);
    if (poll_result == -1) {
      fprintf(stderr, "Failed to poll stdin -> %s\n", strerror(errno));
      return EXIT_FAILURE;
    }

    if (file_descriptors[0].revents != 0x0) {
      ssize_t input_read_size = read(STDIN_FILENO, stdin_buffer, STDIN_READ_BUFFER_SIZE);
      if (input_read_size == -1) {
        fprintf(stderr, "Failed to read from stdin -> %s\n", strerror(errno));
        return EXIT_FAILURE;
      }else if (input_read_size == 0) {
        continue;
      }
      // remove trailing newline
      input_read_size -= 1;
      stdin_buffer[input_read_size] = '\0';

      if (strncmp("echo ", stdin_buffer, 5) == 0) {
        fwrite(stdin_buffer, 1, input_read_size, stderr);
        fprintf(stderr, "\n");
        stdin_buffer[4] = ':';
        ssize_t write_size = sendto(
          daemon_socket, stdin_buffer, input_read_size, 0x0,
          (struct sockaddr *)&daemon_socket_addr, SUN_LEN(&daemon_socket_addr)
        );
        if (write_size == -1) {
          fprintf(stderr, "Failed to send packet to daemon on unix socket -? %s\n", strerror(errno));
          continue;
        }
        assert(write_size == input_read_size); // the write should always perform in a single packet
        fprintf(stdout, "INFO: sent ECHO packet to daemon\n");
      }
      else if (strcmp("quit", stdin_buffer) == 0) {
        ssize_t write_size = sendto(
          daemon_socket, "quit:", 5, 0x0,
          (struct sockaddr *)&daemon_socket_addr, SUN_LEN(&daemon_socket_addr)
        );
        if (write_size == -1) {
          fprintf(stderr, "Failed to send packet to daemon -> %s\n", strerror(errno));
        }
        fprintf(stdout, "exiting\n");
        goto AFTER_MAINLOOP;
      }else if (strncmp("connect ", stdin_buffer, 8) == 0) {
        stdin_buffer[7] = ':';

        ssize_t write_size = sendto(
          daemon_socket, stdin_buffer, input_read_size, 0x0,
          (struct sockaddr *)&daemon_socket_addr, SUN_LEN(&daemon_socket_addr)
        );
        assert(write_size == input_read_size);

        fprintf(stdout, "Sent connection command to server\n");
      }
      else {
        fprintf(stdout,
          "Error: command not recognized, the valid commands are\n"
          "echo <string> - tell the server to echo the message immediately following `echo `\n"
          "quit - tell the daemon to terminate\n"
          "connect <address:port> - attempt to connect to a peer\n"
        );
      }
    }
    if (file_descriptors[1].revents != 0x0) {
      struct sockaddr_un client_address = {
        .sun_family = AF_UNIX
      };
      socklen_t client_address_size = 0;
      ssize_t read_size = recvfrom(
        daemon_socket, daemon_read_buffer, DAEMON_READ_BUFFER_SIZE, 0x0,
        (struct sockaddr *)&client_address, &client_address_size 
      );
      if (read_size == -1) {
        fprintf(stderr, "FATAL: failed to read from UNIX socket at %s -> %s", daemon_socket_path, strerror(errno));
        return EXIT_FAILURE;
      }
      assert(read_size != 0);
      if (strncmp("errlog:", daemon_read_buffer, 7) == 0) {
        fprintf(stderr, "ERROR: server sent an error\n");
        fwrite(daemon_read_buffer + 7, 1, read_size - 7, stderr);
        fprintf(stderr, "\n");
      }else if (strncmp("print:", daemon_read_buffer, 6) == 0) {
        fprintf(stdout, "INFO: received print result from daemon\n%s", daemon_read_buffer + 6);
      }else {
        fprintf(stdout, "WARN: received packet from server with a missing or misformatted message type\n");
        fwrite(daemon_read_buffer, 1, read_size, stdout);
        fprintf(stdout, "\n");
      }
    }
    // assert that at least on file descriptor recieved an event
    assert(file_descriptors[0].revents || file_descriptors[1].revents);

  }

  AFTER_MAINLOOP: {};

  // ssize_t colon_index = str_find(argv[1], strlen(argv[1]), ':');
  // if (colon_index == -1) {
  //   fprintf(stderr, "FATAL: there is no default port, so the port must be specified\n");
  //   return EXIT_FAILURE;
  // }
  // if (colon_index + 1 == strlen(argv[1])) {
  //   fprintf(stderr, "FATAL: expected port number after colon\n");
  //   return EXIT_FAILURE;
  // }

  // argv[1][colon_index] = 0;

  // char *addr_string = argv[1];
  // char *port_string = argv[1] + colon_index + 1;
  // fprintf(stdout, "address string -> %s\n", addr_string);
  // fprintf(stdout, "port_string -> %s\n", port_string);

  // errno = 0;
  // struct in_addr peer_address;
  // int addr_from_string_result = inet_aton(addr_string, &peer_address);
  // if (addr_from_string_result == 0) {
  //   fprintf(
  //     stderr,
  //     "FATAL: failed to convert peer address argument -> result number=%i | %s",
  //     addr_from_string_result, strerror(errno)
  //   );
  //   return EXIT_FAILURE;
  // }

  // errno = 0;
  // char *end_pointer;
  // unsigned long port_number = strtoul(port_string, &end_pointer, 10);
  // if (port_number > 0xffff) {
  //   fprintf(stderr, "FATAL: destination port exceeds 16 bits in size");
  //   return EXIT_FAILURE;
  // }

  // struct sockaddr_in peer_sockaddr = {
  //   .sin_family = AF_INET,
  //   .sin_addr = peer_address,
  //   .sin_port = (uint16_t)port_number
  // };

  // int udp_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  // if (udp_socket == -1) {
  //   fprintf(stderr, "FATAL: failed to create socket -> %s\n", strerror(errno));
  //   return EXIT_FAILURE;
  // }

  // const char *message = (argc > 2) ? argv[2] : "Test Message";

  // ssize_t write_size = sendto(
  //   udp_socket,
  //   message, strlen(message),
  //   0x0, (struct sockaddr *)&peer_sockaddr,
  //   sizeof(struct sockaddr_in)
  // );

  // if (write_size == -1) {
  //   fprintf(stderr, "FATAL: failed to write to UDP socket -> %s\n", strerror(errno));
  //   return EXIT_FAILURE;
  // }

  // fprintf(stdout, "INFO: write %lu bytes to UDP socket\n", (size_t)write_size);

  close(daemon_socket);
  unlink(client_socket_path);

  return 0;
}

  
