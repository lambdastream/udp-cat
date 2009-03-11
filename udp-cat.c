/*
 * Copyright 2003, 2006 Samuel Rivas <samuel@lambdastream.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>


#define BUFFER_LENGTH 1500

typedef struct _InputParams {
  char *address;
  char *lAddress;
  int outputFile;
  int hasOutput;
  int hasLAddress;
  int port;
  short hasTimeout;
  int timeout;
} InputParams;

static void printUsage(const char *name);
static InputParams parseCommand(int argc, char **argv);
static void parseAddress(char *addressLine, InputParams *inputParams);
static int openSocket(InputParams inputParams);
static void writeBytes(const char *buffer, int bytes, int output);

int main(int argc, char **argv) {

  InputParams inputParams;
  struct pollfd pollfd;
  int udpSocket;
  char buffer[BUFFER_LENGTH];
  int bytes, byteCount, timeout, output;
  time_t currentSecond, startSecond;

  /* Parse command line */
  inputParams = parseCommand(argc, argv);

  /* Open the input socket */
  udpSocket = openSocket(inputParams);

  /* Get current second for timeouts */
  if ((startSecond = time(NULL)) < 0) {
    perror("time");
    exit(EXIT_FAILURE);
  }

  /* Prepare to read */
  byteCount = 0;
  pollfd.fd = udpSocket;
  pollfd.events = POLLIN | POLLPRI;
  timeout = (inputParams.hasTimeout) ? inputParams.timeout * 1000 : -1;

  output = (inputParams.hasOutput) ? inputParams.outputFile : 1;

  do {

    if (poll(&pollfd, 1, timeout) < 0) {
      perror("poll");
      exit(EXIT_FAILURE);
    }

    /* Write available data */
    if (pollfd.revents & (POLLIN | POLLPRI)) {

      if ((bytes = recv(udpSocket, buffer, BUFFER_LENGTH, 0)) < 0) {
	perror("recv");
	exit(EXIT_FAILURE);
      }
      writeBytes(buffer, bytes, output);
      byteCount += bytes;
    }

    /* Update timeout */
    if (inputParams.hasTimeout) {
      if ((currentSecond = time(NULL)) < 0) {
	perror("time");
	exit(EXIT_FAILURE);
      }
      timeout = (inputParams.timeout - (currentSecond - startSecond)) * 1000;
    }
  } while (!inputParams.hasTimeout || timeout > 0);

  fprintf(stderr, "Read %i bytes\n", byteCount);

  close(udpSocket);
  return EXIT_SUCCESS;
}

static InputParams parseCommand(int argc, char **argv) {

  InputParams inputParams;
  register int i;

  memset(&inputParams, 0, sizeof (struct _InputParams));

  if (argc < 2) {
    printUsage(argv[0]);
    exit(EXIT_FAILURE);
  }

  parseAddress(argv[1], &inputParams);

  for (i = 2; i < argc; i++) {
    if (!strncmp(argv[i], "-timeout=", 9)) {
      inputParams.hasTimeout = 1;
      inputParams.timeout = atoi(argv[i] + 9);
    } else if(!strncmp(argv[i], "-lAddress=", 10)) {
      inputParams.lAddress = argv[i]+10;
      inputParams.hasLAddress = 1;
    } else if (!strncmp(argv[i], "-outputFile=", 12)) {
      inputParams.outputFile = open(argv[i]+12, O_APPEND|O_WRONLY);
      if (inputParams.outputFile < 0) {
	fprintf(stderr, "Invalid file %s\n", argv[i]+12);
	exit(EXIT_FAILURE);
      } else {
	inputParams.hasOutput = 1;
      }
    } else {
      printUsage(argv[0]);
      exit(EXIT_FAILURE);
    }
  }
  return inputParams;
}

static void parseAddress(char *addressLine, InputParams *inputParams) {

  char *port;

  if ((port = index(addressLine, ':')) == NULL) {
    inputParams -> address = NULL;
    inputParams -> port = atoi(addressLine);

  } else {
    inputParams -> address = addressLine;

    /* Shield against wrong addresses like "foo:" */
    if (port[1] == '\0') {
      fprintf(stderr, "Invalid address %s\n", addressLine);
      exit(EXIT_FAILURE);
    }
    inputParams -> port = atoi(port + 1);
    port[0] = 0;
  }

  /* Check the port to avoid mistaken addresses like "foo" of "foo:bar" */
  if (inputParams -> port == 0) {
    fprintf(stderr, "Invalid port: %s\n", addressLine);
    exit(EXIT_FAILURE);
  }
}

static void printUsage(const char *name) {
  fprintf(stderr, "\nUsage: %s <addr> [options]\n\n"
	  "<addr> may be either ip:port (multicast) or port (unicast)\n\n"
	  "Options:\n"
	  "\t-timeout=N\t\tListens N seconds and exits printing the amount of data read.\n"
	  "\t-outputFile=File\tWrite the results to File instead of stdout.\n"
	  "\t-lAddress=Address\tSpecifies the address to be used to listen to <addr>.\n"
	  "\n", name);
}

static int openSocket(InputParams inputParams) {

  int udpSocket;
  int true = 1;
  struct sockaddr_in sockAddrIn;
  struct ip_mreqn mreqn;
  struct in_addr mcastAddr;
  struct hostent *h;

  /* Open the socket */
  if ((udpSocket = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
    perror("socket");
    exit(1);
  }

  /* Allow multiple listeners */
  if (setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR, &true,
		 sizeof(true)) == -1) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  /* Bind it */
  sockAddrIn.sin_family = AF_INET;
  sockAddrIn.sin_port = htons(inputParams.port);
  if (inputParams.hasLAddress && (inputParams.address==NULL)) { 
    sockAddrIn.sin_addr.s_addr = inet_addr(inputParams.lAddress); 
  } else { 
    sockAddrIn.sin_addr.s_addr = INADDR_ANY;
  } 

  if (bind(udpSocket, (struct sockaddr *) &sockAddrIn, sizeof(sockAddrIn))
      == -1) {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  /* Multicast it if required */
  if (inputParams.address != NULL) {

    /* Prepare a multicast port */
    h = gethostbyname(inputParams.address);
    memcpy(&mcastAddr, (h -> h_addr_list)[0], h -> h_length);
    mreqn.imr_multiaddr.s_addr = mcastAddr.s_addr;
    if (inputParams.hasLAddress) {
      mreqn.imr_address.s_addr = inet_addr(inputParams.lAddress);
    } else {
      mreqn.imr_address.s_addr = INADDR_ANY;
    } 
    mreqn.imr_ifindex = 0;

    if (setsockopt(udpSocket, SOL_IP, IP_ADD_MEMBERSHIP, &mreqn,
		   sizeof(mreqn)) == -1) {
      perror("setsockopt");
      exit(EXIT_FAILURE);
    }
  }
  return udpSocket;
}

static void writeBytes(const char *buffer, int bytes, int output) {

  int written;

  while (bytes > 0) {
    if ((written = write(output, buffer, bytes)) < 0) {
      perror("write");
      exit(EXIT_FAILURE);
    }
    bytes -= written;
  }
}
