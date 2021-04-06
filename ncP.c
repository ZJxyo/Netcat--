#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include "commonProto.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <netdb.h>

#define BUF_LEN 128
#define THREAD_COUNT 5

void server_mode(struct commandOptions cmdOps);
void process_server(int index, struct pollfd *pfds, int *fd_count, int option_r);
void process_stdin(int index, struct pollfd *pfds, int *fd_count);
void process_client(int index, struct pollfd *pfds, int *fd_count, int option_r, int option_k);
void client_mode(struct commandOptions cmdOps);
void get_local_address(struct sockaddr_in *local_address, int option_v);
void bind_address_and_port(int socket, struct sockaddr_in *local_address, int port, int option_v);

int main(int argc, char **argv)
{

  // This is some sample code feel free to delete it
  // This is the main program for the thread version of nc

  struct commandOptions cmdOps;
  int retVal = parseOptions(argc, argv, &cmdOps);

  if (cmdOps.option_v)
  {
    printf("Command parse outcome %d\n", retVal);
    printf("-k = %d\n", cmdOps.option_k);
    printf("-l = %d\n", cmdOps.option_l);
    printf("-v = %d\n", cmdOps.option_v);
    printf("-r = %d\n", cmdOps.option_r);
    printf("-ai = %d\n", cmdOps.option_p);
    printf("-ai port = %u\n", cmdOps.source_port);
    printf("-w  = %d\n", cmdOps.option_w);
    printf("Timeout value = %u\n", cmdOps.timeout);
    printf("Host to connect to = %s\n", cmdOps.hostname);
    printf("Port to connect to = %u\n", cmdOps.port);
  }

  // check options
  if ((cmdOps.option_k == 1 || cmdOps.option_r == 1) && cmdOps.option_l == 0)
  {
    if (cmdOps.option_v)
    {
      perror("-k or -r without -l\n");
    }
    exit(1);
  }

  if (cmdOps.option_p == 1 && cmdOps.option_l == 1)
  {
    if (cmdOps.option_v)
    {
      perror("-ai with -l\n");
    }
    exit(1);
  }

  if (cmdOps.option_l)
  {
    server_mode(cmdOps);
  }
  else
  {
    client_mode(cmdOps);
  }
}

void client_mode(struct commandOptions cmdOps)
{
  // create and bind client socket
  int client_socket = socket(PF_INET, SOCK_STREAM, 0);

  struct sockaddr_in client_address;
  get_local_address(&client_address, cmdOps.option_v);

  int source_port = 0;

  if (cmdOps.option_p)
  {
    source_port = cmdOps.source_port;
  }

  bind_address_and_port(client_socket, &client_address, source_port, cmdOps.option_v);

  // lookup server ip address
  struct addrinfo hints, *server_addresses, *ai;

  memset(&hints, 0, sizeof(hints));

  hints.ai_family = PF_INET;
  hints.ai_socktype = SOCK_STREAM;

  char buf[6];

  sprintf(buf, "%u", cmdOps.port);

  if (getaddrinfo(cmdOps.hostname, buf, &hints, &server_addresses))
  {
    if (cmdOps.option_v)
    {
      perror("no ip address found\n");
    }
    exit(2);
  }

  for (ai = server_addresses; ai; ai = ai->ai_next)
  {
    if (connect(client_socket, ai->ai_addr, ai->ai_addrlen) == 0)
    {
      if (cmdOps.option_v)
      {
        printf("success\n");
      }
      break;
    }
    else if (cmdOps.option_v)
    {
      perror("failed to connect\n");
    }
  }

  if (ai == NULL)
  {
    if (cmdOps.option_v)
    {
      perror("all addresses failed to connect\n");
    }
    exit(2);
  }

  freeaddrinfo(server_addresses);

  // poll parameters setup
  struct pollfd pfds[2];

  pfds[0].fd = client_socket;
  pfds[0].events = POLLIN;

  pfds[1].fd = 0;
  pfds[1].events = POLLIN;

  int timeout = -1;

  if (cmdOps.option_w)
  {
    timeout = cmdOps.timeout;
  }

  while (1)
  {
    int message_received = poll(pfds, 2, timeout * 1000);

    // EOF
    if (message_received == 0)
    {
      close(client_socket);
      if (cmdOps.option_v)
      {
        printf("socket closed due to timeout\n");
      }
      exit(0);
    }

    if (pfds[0].revents & POLLIN) // client socket has a message
    {
      pfds[0].events = POLLIN;
      char buf[BUF_LEN];

      int num_received = recv(pfds[0].fd, buf, BUF_LEN, 0);

      if (num_received) // received a message
      {
        buf[num_received] = '\0';
        printf("%s", buf);
      }
      else
      {
        if (cmdOps.option_v)
        {
          perror("socket closed by the server\n");
        }
        exit(2);
      }
    }

    if (pfds[1].revents & POLLIN) // stdin fd has message
    {
      pfds[1].events = POLLIN;
      char buf[BUF_LEN];

      int num_received = read(pfds[1].fd, buf, BUF_LEN);

      send(pfds[0].fd, buf, num_received, 0);
    }
  }
}

void server_mode(struct commandOptions cmdOps)
{
  // setup and bind
  int server_socket = socket(PF_INET, SOCK_STREAM, 0);

  struct sockaddr_in server_address;
  get_local_address(&server_address, cmdOps.option_v);

  bind_address_and_port(server_socket, &server_address, cmdOps.source_port, cmdOps.option_v);

  listen(server_socket, THREAD_COUNT);

  // poll parameters setup
  int fd_count = 0;
  struct pollfd pfds[2 + THREAD_COUNT];

  pfds[0].fd = server_socket;
  pfds[0].events = POLLIN;
  fd_count++;

  dprintf(2, "waiting\n");

  while (1)
  {
    poll(pfds, fd_count, -1);

    if (pfds[0].fd == server_socket && pfds[0].revents & POLLIN) // server socket has message
    {
      process_server(0, pfds, &fd_count, cmdOps.option_r);
    }

    if (pfds[1].fd == 0 && pfds[1].revents & POLLIN) // stdin fd has message
    {
      process_stdin(1, pfds, &fd_count);
    }

    for (int i = 2; i < fd_count; i++)
    {
      if (pfds[i].revents & POLLIN) // client has message
      {
        process_client(i, pfds, &fd_count, cmdOps.option_r, cmdOps.option_k);
      }
    }
  }
}

void process_server(int index, struct pollfd *pfds, int *fd_count, int option_r)
{
  int client_socket = accept(pfds[index].fd, NULL, NULL);

  struct sockaddr_in addr;
  socklen_t addr_size = sizeof(struct sockaddr_in);
  getsockname(client_socket, (struct sockaddr *)&addr, &addr_size);
  char *client_ip = inet_ntoa(addr.sin_addr);
  int client_port = ntohs(addr.sin_port);
  // printf("ip address: %s\n", client_ip);
  // printf("port: %d\n", client_port);
  dprintf(2, "accepted\n");
  dprintf(2, "%s\n", client_ip);
  dprintf(2, "%d\n", client_port);


  // only server socket in pfds
  if (*fd_count == 1)
  {
    pfds[1].fd = 0;
    pfds[1].events = POLLIN;

    (*fd_count)++;
  }

  // store client socket
  pfds[*fd_count].fd = client_socket;
  pfds[*fd_count].events = POLLIN;
  (*fd_count)++;

  // stop accepting clients
  if (!option_r || *fd_count >= 2 + THREAD_COUNT)
  {
    pfds[0].fd = -pfds[0].fd;
  } else {
    dprintf(2, "waiting\n");
  }

  return;
}

void process_stdin(int index, struct pollfd *pfds, int *fd_count)
{
  pfds[index].events = POLLIN;
  char buf[BUF_LEN];

  int num_received = read(pfds[index].fd, buf, BUF_LEN);

  // something unexpected happened
  if (num_received <= 0)
  {
    return;
  }

  // send message to clients
  for (int i = 2; i < *fd_count; i++)
  {
    send(pfds[i].fd, buf, num_received, 0);
  }
  return;
}

void process_client(int index, struct pollfd *pfds, int *fd_count, int option_r, int option_k)
{
  pfds[index].events = POLLIN;
  char buf[BUF_LEN];

  int num_received = recv(pfds[index].fd, buf, BUF_LEN, 0);

  if (num_received) // received a message
  {
    buf[num_received] = '\0';
    printf("%s", buf);

    for (int i = 2; i < *fd_count; i++)
    {
      if (i != index) // not this client
      {
        send(pfds[i].fd, buf, num_received, 0);
      }
    }
  }
  else // EOF
  {
    close(pfds[index].fd);
    (*fd_count)--;

    if (*fd_count > 2 && index != *fd_count) // this client is not the last client in the array
    {
      // move the last client
      pfds[index].fd = pfds[*fd_count].fd;
      pfds[index].revents = pfds[*fd_count].revents;

      if (pfds[index].revents & POLLIN) // switched client has message
      {
        process_client(index, pfds, fd_count, option_r, option_k);
      }
    }
    else if (*fd_count == 2) // no clients, stop polling for stdin
    {
      (*fd_count)--;
    }

    if (!option_k && *fd_count == 1) // no k and no clients
    {
      exit(0);
    }

    if (!option_r || *fd_count == 1 + THREAD_COUNT) // no -r or has slot, start accepting
    {
      dprintf(2, "waiting\n");
      pfds[0].fd = -pfds[0].fd;
    }
  }
  return;
}

void get_local_address(struct sockaddr_in *local_address, int option_v)
{
  struct ifaddrs *server_addresses, *ifa;

  char *address;

  getifaddrs(&server_addresses);

  for (ifa = server_addresses; ifa; ifa = ifa->ifa_next)
  {
    if (ifa->ifa_addr && ifa->ifa_addr->sa_family == PF_INET)
    {
      local_address->sin_addr.s_addr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
      local_address->sin_family = PF_INET;
      address = inet_ntoa(local_address->sin_addr);
      if (strcmp(address, "127.0.0.1") != 0)
      {
        break;
      }
    }
  }

  if (ifa == NULL)
  {
    if (option_v)
    {
      perror("no usable addresses\n");
    }
    exit(2);
  }

  if (option_v)
  {
    printf("ip address: %s\n", address);
  }

  freeifaddrs(server_addresses);
  return;
}

void bind_address_and_port(int socket, struct sockaddr_in *local_address, int port, int option_v)
{
  setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (int *)1, sizeof(int));

  int num = port;
  if (port <= 0)
  {
    while (1)
    {
      num = rand();
      if (num >= 1024 && num <= 65535)
      {
        local_address->sin_port = htons(num);
        break;
      }
    }
  }

  while (bind(socket, (struct sockaddr *)local_address, sizeof(*local_address)))
  {
    while (1)
    {
      num = rand();
      if (num >= 1024 && num <= 65535)
      {
        local_address->sin_port = htons(num);
        break;
      }
    }
  }

  if (option_v)
  {
    printf("port: %d\n", num);
  }

  return;
}