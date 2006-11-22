#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BUFFER_LENGTH 1500

int main(int argc, char **argv) {
  
  int		udpSocket;
  struct	sockaddr_in sockAddrIn;
  struct	ip_mreqn mreqn;
  struct	in_addr mcastAddr;
  struct	hostent *h;
  char		buffer[BUFFER_LENGTH];
  int		bytes, multicast, written;
  char		*port;

  if (argc != 2 && argc != 3) {
    puts("Usage: udp-cat [<multicast-addr>] <port>\n");
    exit(1);
  }

  if (argc == 3) {
    multicast = 1;
    port = argv[2];
  } else {
    multicast = 0;
    port = argv[1];
  }

  if ((udpSocket = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
    perror("socket");
    exit(1);
  }

  sockAddrIn.sin_family = AF_INET;
  sockAddrIn.sin_port = htons(atoi(port));
  sockAddrIn.sin_addr.s_addr = INADDR_ANY;

  if (bind(udpSocket, (struct sockaddr *) &sockAddrIn, sizeof(sockAddrIn)) 
      == -1) {
    perror("bind");
    exit(1);
  }

  if (multicast) {
    h = gethostbyname(argv[1]);
    memcpy(&mcastAddr, (h -> h_addr_list)[0], h -> h_length);
    mreqn.imr_multiaddr.s_addr = mcastAddr.s_addr;
    mreqn.imr_address.s_addr = INADDR_ANY;
    mreqn.imr_ifindex = 0;

    if (setsockopt(udpSocket, SOL_IP, IP_ADD_MEMBERSHIP, &mreqn, 
		   sizeof(mreqn)) == -1) {
      perror("setsockopt");
      exit(1);
    }
  }

  while(1) {

    if ((bytes = recv(udpSocket, buffer, BUFFER_LENGTH, 0)) == -1) {
      perror("recv");
      exit(1);
    } 

    while (bytes > 0) {
      if ((written = write(1, buffer, bytes)) < 0) {
	perror("recv");
	exit(1);
      }
      bytes -= written;
    }
  }
  return 0;
}
