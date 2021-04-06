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
#include <unistd.h>
#include <netdb.h>
#include "Thread.h"
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>

#define BUF_LEN 128
#define THREAD_NUM 5

void client_mode(struct commandOptions cmdOps);
void server_mode(struct commandOptions cmdOps);
void get_local_address(struct sockaddr_in *local_address, int option_v);
void bind_address_and_port(int socket, struct sockaddr_in *local_address, int port, int option_v);
void *server_thread(void *arg);
void *client_thread(void *arg);
void *stdin_thread(void *arg);

struct thread_params
{
  struct commandOptions cmdOps;
  int socket;
  int *fds;
  int thread_num;
  int index;
};

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

  // check options validity
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
  //create and bind
  int client_socket = socket(PF_INET, SOCK_STREAM, 0);

  struct sockaddr_in client_address;
  get_local_address(&client_address, cmdOps.option_v);

  int source_port = 0;

  if (cmdOps.option_p)
  {
    source_port = cmdOps.source_port;
  }

  bind_address_and_port(client_socket, &client_address, source_port, cmdOps.option_v);

  // lookup server ip
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

  // setup thread parameters
  int fds[1];
  memset(fds, -1, sizeof(int));
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  // run stdin thread
  struct thread_params input_params;
  input_params.cmdOps = cmdOps;
  input_params.fds = fds;
  input_params.thread_num = 1;

  void *input_thread = createThread(stdin_thread, &input_params);

  if (runThread(input_thread, &attr) == -10)
  {
    if (cmdOps.option_v)
    {
      perror("failed to create\n");
    }
    exit(2);
  }

  // run client thread
  struct thread_params params;
  params.cmdOps = cmdOps;
  params.fds = fds;

  fds[0] = client_socket;

  void *thread = createThread(client_thread, &params);

  if (runThread(thread, &attr) == -10)
  {
    if (cmdOps.option_v)
    {
      perror("failed to create\n");
    }
    exit(2);
  }

  // wait for client thread to finish
  if (joinThread(thread, NULL))
  {
    if (cmdOps.option_v)
    {
      perror("failed to join\n");
    }
    exit(2);
  }

  if (joinThread(input_thread, NULL))
  {
    if (cmdOps.option_v)
    {
      perror("failed to join\n");
    }
    exit(2);
  }
  return;
}

void server_mode(struct commandOptions cmdOps)
{
  //socket setups
  int server_socket = socket(PF_INET, SOCK_STREAM, 0);

  struct sockaddr_in server_address;
  get_local_address(&server_address, cmdOps.option_v);

  bind_address_and_port(server_socket, &server_address, cmdOps.source_port, cmdOps.option_v);

  if (listen(server_socket, THREAD_NUM))
  {
    if (cmdOps.option_v)
    {
      perror("failed to listen\n");
    }
    exit(2);
  }

  // determine thread num
  int thread_num = 1;
  if (cmdOps.option_r)
  {
    thread_num = THREAD_NUM;
  }

  // setup for threads
  int fds[thread_num];
  memset(fds, -1, thread_num * sizeof(int));
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  // stdin thread
  struct thread_params input_params;
  input_params.cmdOps = cmdOps;
  input_params.fds = fds;
  input_params.thread_num = thread_num;

  void *input_thread = createThread(stdin_thread, &input_params);

  if (runThread(input_thread, &attr) == -10)
  {
    if (cmdOps.option_v)
    {
      perror("failed to create\n");
    }
    exit(2);
  }

  struct Thread *threads[thread_num];
  struct thread_params params[thread_num];

  dprintf(2, "waiting\n");
  // create threads
  for (int i = 0; i < thread_num; i++)
  {
    params[i].cmdOps = cmdOps;
    params[i].socket = server_socket;
    params[i].fds = fds;
    params[i].thread_num = thread_num;
    params[i].index = i;

    void *thread = createThread(server_thread, &(params[i]));

    if (runThread(thread, &attr) == -10)
    {
      if (cmdOps.option_v)
      {
        perror("failed to create\n");
      }
      exit(2);
    }

    threads[i] = thread;
  }

  // join accept threads
  for (int i = 0; i < thread_num; i++)
  {
    if (joinThread(threads[i], NULL))
    {
      if (cmdOps.option_v)
      {
        perror("failed to join\n");
      }
      exit(2);
    }
  }

  if (joinThread(input_thread, NULL))
  {
    if (cmdOps.option_v)
    {
      perror("failed to join\n");
    }
    exit(2);
  }

  return;
}

void *server_thread(void *arg)
{
  // read params
  struct thread_params *params = (struct thread_params *)arg;
  struct commandOptions cmdOps = params->cmdOps;
  int socket = params->socket;
  int *fds = params->fds;
  int thread_num = params->thread_num;
  int index = params->index;

  while (1)
  {
    int client_socket = accept(socket, NULL, NULL);

    // interrupted by signal
    if (client_socket == -1)
    {
      return NULL;
    }

    // store socket
    fds[index] = client_socket;

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

    while (1)
    {
      //read
      char buf[BUF_LEN];
      int num_received = recv(client_socket, buf, BUF_LEN, 0);

      if (num_received > 0) // received a message
      {
        buf[num_received] = '\0';
        printf("%s", buf);

        for (int i = 0; i < thread_num; i++)
        {
          if (i != index) // not this client
          {
            send(fds[i], buf, num_received, 0);
          }
        }
      }
      else
      {
        close(client_socket);
        fds[index] = -1;
        break;
      }
    }

    // check if there's other clients connected
    for (int i = 0; i < thread_num; i++)
    {
      if (fds[i] != -1)
      {
        break;
      }

      if (i == thread_num - 1 && cmdOps.option_k == 0) // raise a signal if no other clients and -k is not set
      {
        raise(14);
        return NULL;
      }
    }

    dprintf(2, "waiting\n");
  }
  return NULL;
}

void *client_thread(void *arg)
{
  struct thread_params *params = (struct thread_params *)arg;
  struct commandOptions cmdOps = params->cmdOps;
  int *fds = params->fds;

  char buf[BUF_LEN];

  while (1)
  {
    // timeout timer
    if (cmdOps.option_w && cmdOps.option_l == 0)
    {
      alarm(cmdOps.timeout);
    }

    int num_received = recv(fds[0], buf, BUF_LEN, 0);

    if (num_received > 0) // received a message
    {
      buf[num_received] = '\0';
      printf("%s", buf);
    }
    else
    {
      close(fds[0]);
      raise(14);
      return NULL;
    }
  }
}

void *stdin_thread(void *arg)
{
  struct thread_params *params = (struct thread_params *)arg;
  struct commandOptions cmdOps = params->cmdOps;
  int *fds = params->fds;
  int thread_num = params->thread_num;

  // index is the number of accepting threads
  char buf[BUF_LEN];

  while (1)
  {
    // timeout timer
    if (cmdOps.option_w && cmdOps.option_l == 0)
    {
      alarm(cmdOps.timeout);
    }

    int num_received = read(0, buf, BUF_LEN);

    if (num_received > 0) // send message if received something
    {
      for (int i = 0; i < thread_num; i++)
      {
        if (fds[i] != -1)
        {
          send(fds[i], buf, num_received, 0);
        }
      }
    }
    else
    {
      close(0);
      return NULL;
    }
  }
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