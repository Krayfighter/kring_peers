
#include "stdio.h"
#include "string.h"
#include "errno.h"
#include "stdlib.h"
#include "unistd.h"
#include "stdbool.h"

#include "sys/socket.h"
// #include "sys/uio.h"

#include "arpa/inet.h"
#include "netinet/in.h"
// #include "netinet/ip.h"
// #include <time.h>


// find first occurence of `delim` in source
// returns -1 on error (such as delim not found)
ssize_t str_find(const char *source, size_t source_len, char delim) {
  for (size_t i = 0; i < source_len; i += 1) {
    if (source[i] == delim) { return i; }
  }
  return -1;
}



typedef struct {
  uint16_t source_port;
  uint16_t destination_port;
  uint16_t packet_size;
  uint16_t checksum;
} UDP_Packet;


int main(int argc, char **argv) {

  if (argc > 1) { // run client if any args are passed

    ssize_t colon_index = str_find(argv[1], strlen(argv[1]), ':');
    if (colon_index == -1) {
      fprintf(stderr, "FATAL: there is no default port, so the port must be specified\n");
      return EXIT_FAILURE;
    }
    if (colon_index + 1 == strlen(argv[1])) {
      fprintf(stderr, "FATAL: expected port number after colon\n");
      return EXIT_FAILURE;
    }

    argv[1][colon_index] = 0;

    char *addr_string = argv[1];
    char *port_string = argv[1] + colon_index + 1;
    fprintf(stdout, "address string -> %s\n", addr_string);
    fprintf(stdout, "port_string -> %s\n", port_string);

    errno = 0;
    struct in_addr peer_address;
    int addr_from_string_result = inet_aton(addr_string, &peer_address);
    if (addr_from_string_result == 0) {
      fprintf(
        stderr,
        "FATAL: failed to convert peer address argument -> result number=%i | %s",
        addr_from_string_result, strerror(errno)
      );
      return EXIT_FAILURE;
    }

    errno = 0;
    char *end_pointer;
    unsigned long port_number = strtoul(port_string, &end_pointer, 10);
    // if (end_pointer != argv[1] + strlen(argv[1])) {
    //   fprintf(stderr, "FATAL: unexpected characters at the end of port number -> %s", strerror(errno));
    //   return EXIT_FAILURE;
    // }
    if (port_number > 0xffff) {
      fprintf(stderr, "FATAL: destination port exceeds 16 bits in size");
      return EXIT_FAILURE;
    }

    struct sockaddr_in peer_sockaddr = {
      .sin_family = AF_INET,
      .sin_addr = peer_address,
      .sin_port = (uint16_t)port_number
    };

    int udp_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket == -1) {
      fprintf(stderr, "FATAL: failed to create socket -> %s\n", strerror(errno));
      return EXIT_FAILURE;
    }

    const char *message = (argc > 2) ? argv[2] : "Test Message";

    // struct sockaddr_in target_addr = {
    //   .sin_addr = htonl(0x7f000001),
    //   .sin_port = 12000,
    //   .sin_family = AF_INET
    // };
    // sendto(udp_socket, message, strlen(message), 0x0);
    ssize_t write_size = sendto(
      udp_socket,
      message, strlen(message),
      0x0, (struct sockaddr *)&peer_sockaddr,
      sizeof(peer_sockaddr)
    );

    if (write_size == -1) {
      fprintf(stderr, "FATAL: failed to write to UDP socket -> %s\n", strerror(errno));
      return EXIT_FAILURE;
    }

    fprintf(stdout, "INFO: write %lu bytes to UDP socket\n", (size_t)write_size);

    return 0;
    
  }else {

    int listener = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (listener == -1) {
      fprintf(stderr, "FATAL: unable to open upd socket -> %s\n", strerror(errno));
      return EXIT_FAILURE;
    }

    struct sockaddr_in bind_address = {
      .sin_port = 12000,
      .sin_addr = INADDR_ANY,
      .sin_family = AF_INET,
    };
    int bind_result = bind(listener, (struct sockaddr *)&bind_address, sizeof(bind_address));
    if (bind_result == -1) {
      fprintf(stderr, "FATAL: failed to bind upd socket to 0.0.0.0:13000 -> %s\n", strerror(errno));
      return EXIT_FAILURE;
    }

    socklen_t socket_addr_size = sizeof(bind_address);
    int result = getsockname(listener, (struct sockaddr *)&bind_address, &socket_addr_size);
    if (result == -1) {
      fprintf(stderr, "FATAL: failed to get socket address info on listener socket -> %s\n", strerror(errno));
      return EXIT_FAILURE;
    }

    fprintf(
      stdout,
      "INFO: servering at address %x and port %u\n",
      bind_address.sin_addr.s_addr,
      bind_address.sin_port
    );

    // getsockopt(listener, SOL_IP, 0x0, (void *)(&bind_addres), );

    char packet_buffer[0xffff]; // max size of udp packet is the max size of a uint16_t
  
    char msg_name_buffer[256];
    while (true) {

      // // TODO handle ancillary data
      // struct msghdr msg_ctx = {
      //   .msg_name = msg_name_buffer,
      //   .msg_namelen = 256,
      //   .msg_iov = &packet_vector,
      //   .msg_iovlen = 1,
      // };

      // ssize_t read_size = recvmsg(listener, &msg_ctx, 0x0);
      struct sockaddr_in client_addr = { 0 };
      socklen_t addr_len = 0;
    
      ssize_t read_size = recvfrom(listener, packet_buffer, 0xffff, 0x0, (struct sockaddr *)&client_addr, &addr_len);

      if (read_size == -1) {
        fprintf(stderr, "FATAL: failed call to recvmsg -> %s\n", strerror(errno));
        return EXIT_FAILURE;
      }else if (read_size == 0) {
        fprintf(stderr, "WARN: recvmsg read 0 bytes");
        continue;
      }

      // fprintf(stdout, "INFO: recieved packet - exiting\n");
      fprintf(
        stdout,
        "INFO: Recieved packet from peer -> address %u.%u.%u.%u on port %u\naddr_len = %u\n",
        client_addr.sin_addr.s_addr        & 0x000000ff,
        (client_addr.sin_addr.s_addr >> 2) & 0x000000ff,
        (client_addr.sin_addr.s_addr >> 4) & 0x000000ff,
        (client_addr.sin_addr.s_addr >> 6) & 0x000000ff,
        client_addr.sin_port,
        addr_len
      );

      packet_buffer[read_size + 1] = 0; // BUG possible off by one overflow of array
      fprintf(stdout, "packet info (string) -> %s\n", packet_buffer);
    
    }
  }

}

