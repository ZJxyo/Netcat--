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
  struct addrinfo hints, *res;
  int sockfd;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; // use IPv4
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // fill in my IP for me

  char source_port[6];

  if (cmdOps.source_port != 0) // bind to source port
  {
    sprintf(source_port, "%u", cmdOps.source_port);
  }
  else // bind to any port
  {
    sprintf(source_port, "%u", (unsigned int)0);
  }

  getaddrinfo(NULL, source_port, &hints, &res);
  sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sockfd == -1)
  {
    perror("failed to create socket\n");
    exit(-1);
  }
  else
  {
    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1)
    {
      perror("failed to bind\n");
      exit(-1);
    }
  }

  // lookup server ip
  struct addrinfo *server_addresses, *ai;

  memset(&hints, 0, sizeof(hints));

  hints.ai_family = PF_INET;
  hints.ai_socktype = SOCK_STREAM;

  char port[6];

  sprintf(port, "%u", cmdOps.port);

  if (getaddrinfo(cmdOps.hostname, port, &hints, &server_addresses))
  {
    perror("no ip address found\n");
    exit(2);
  }

  for (ai = server_addresses; ai; ai = ai->ai_next)
  {
    if (connect(sockfd, ai->ai_addr, ai->ai_addrlen) == 0)
    {
      if (cmdOps.option_v)
      {
        printf("success\n");
      }
      break;
    }
  }

  if (ai == NULL)
  {
    perror("failed to connect to all addresses\n");
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

  fds[0] = sockfd;

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
  struct addrinfo hints, *res;
  int sockfd;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; // use IPv4
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // fill in my IP for me

  char source_port[6];

  if (cmdOps.source_port != 0) // bind to source port
  {
    sprintf(source_port, "%u", cmdOps.source_port);
  }
  else // bind to any port
  {
    sprintf(source_port, "%u", (unsigned int)0);
  }

  getaddrinfo(NULL, source_port, &hints, &res);
  sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sockfd == -1)
  {
    perror("failed to create socket\n");
    exit(-1);
  }
  else
  {
    if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1)
    {
      perror("failed to bind\n");
      exit(-1);
    }
  }

  if (listen(sockfd, THREAD_NUM))
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
    params[i].socket = sockfd;
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
    int sockfd = accept(socket, NULL, NULL);

    // interrupted by signal
    if (sockfd == -1)
    {
      return NULL;
    }

    // store socket
    fds[index] = sockfd;

    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    getsockname(sockfd, (struct sockaddr *)&addr, &addr_size);
    char *client_ip = inet_ntoa(addr.sin_addr);
    int client_port = ntohs(addr.sin_port);
    // printf("ip address: %s\n", client_ip);
    // printf("port: %d\n", client_port);
    dprintf(2, "accepted\n");
    dprintf(2, "%s\n", client_ip);
    dprintf(2, "%d\n", client_port);

    while (1)
    {
      // read
      char buf[BUF_LEN];
      int num_received = recv(sockfd, buf, BUF_LEN, 0);

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
        close(sockfd);
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

  return NULL;
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