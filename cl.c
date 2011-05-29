/*
** client.c -- a stream socket client demo
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/types.h>

#define PORT "3490" // the port client will be connecting to 
#define MAXDATASIZE 1200 // max number of bytes we can get at once 
#define NP "200 OK"

#define TELA_INICIAL 1
#define SELECIONA_JOGO 2
#define RANK 3
#define SAIR 4

#define JOGAR_2 5
#define JOGAR_4 6
#define JOGAR_8 7

void *enviaResposta(void *aux);

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
int main(int argc, char *argv[])
{
    int sockfd, numbytes;  
    char buf[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s[INET6_ADDRSTRLEN];
    if (argc != 2) {
        fprintf(stderr,"usage: client hostname\n");
        exit(1);
    }
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
    printf("client: connecting to %s\n", s);
    freeaddrinfo(servinfo); // all done with this structure
    
    trataConexao(sockfd);
    
    /*if ((numbytes = recv(sockfd, buf, MAXDATASIZE-1, 0)) == -1) {
        perror("recv");
	
        exit(1);
    }
    buf[numbytes] = '\0';
    printf("client: received '%s'\n",buf);
    close(sockfd);*/
    return 0;
}

// 0 - Tela Inicial // 1 - Jogar // 2 - Rank // 3 - Sair
int trataConexao(int sid) {
  char buf[MAXDATASIZE];
  int receiv, ms, sair;
  
  telas(sid);
  /*sair = 0;
  ms = 0;
  while(!sair) {
    if(ms == 0)
      ms = telaInicial(sockfd);
    else if(ms == 1)
      ms = iniciaJogo(sockfd);
    else if(ms == 2)
      ms = exibeRank(sockfd);
    else if(ms == 3)
      sair = 1;
    else
      printf("Operação inválida");
  }*/
  
  close(sid);
  return 1;
}

int telas(sid) {
  char buf[MAXDATASIZE];
  int tela, receiv;
  
  while(1) {
    receiv = recv(sid, buf, MAXDATASIZE, 0);
    tela = atoi(buf);

    if(tela == TELA_INICIAL) {
      telaInicial(sid);
    }
    else if(tela == SELECIONA_JOGO) {
      selecionaJogo(sid);
    }
    else if(tela == JOGAR_2) {
      iniciaJogo(sid);
    }
    else if(tela == SAIR) {
      break;
    }
  }
  
  return 0;
}

int telaInicial(int sid) {
  char buf[MAXDATASIZE];
  int numbytes;
  
  printf("KUIZZ\n\n1) Jogar\n2) Rank\n3) Sair\n\n");
  scanf("%s", buf);
  send(sid, buf, strlen(buf)+1, 0);

  return 0;
}

int selecionaJogo(int sid) {
  char buf[MAXDATASIZE];
  int numbytes;
  
  printf("KUIZZ\n\n1) 2 Jogadores\n2) 4 Jogadores\n3) 8 Jogadores\n\n");
  scanf("%c", buf);

  send(sid, buf, strlen(buf)+1, 0);

  return 0;
}

int exibeRank(int sockfd) {
  char buf[MAXDATASIZE];
  
  printf("\n\n");
  while(1) {
    recv(sockfd, buf, MAXDATASIZE, 0);
    
    if(strcmp(buf, "eor") == 0) break;
    printf("%s\n", buf);
    
    strcpy(buf, NP); 
    send(sockfd, buf, strlen(buf)+1, 0); //Confirmação/Espera de recebimento (No Problem)
  }
  
  strcpy(buf, NP); 
  send(sockfd, buf, strlen(buf)+1, 0); //Confirmação/Espera de recebimento (No Problem)  

  recv(sockfd, buf, MAXDATASIZE, 0);
  printf("%s\n\n", buf);
  
  strcpy(buf, NP); 
  send(sockfd, buf, strlen(buf)+1, 0); //Confirmação/Espera de recebimento (No Problem)
  
  return 0;
}
int fimJogo;

int iniciaJogo(int sockfd) {
  char buf[MAXDATASIZE];
  int receiv;
  
  //Aguardando Jogadores
  while(1) {
    receiv = recv(sockfd, buf, MAXDATASIZE, 0);
    if(strcmp(buf, NP) == 0)
      break;
    printf("%s", buf);
    fflush(stdout);
  }
  
  //Msg de controle para estabelecer sincronização entre as conexões na Sala
  strcpy(buf, NP);
  send(sockfd, buf, strlen(buf)+1, 0);
  
  pthread_t con;
  int rc = 0;
  fimJogo = 0;
  rc = pthread_create(&con, NULL, enviaResposta, (void *)sockfd);
  
  while(1) {    
    receiv = recv(sockfd, buf, MAXDATASIZE, 0);
    if(strcmp(buf, "eog") == 0) break;
    printf("%s", buf);
    sleep(1);
  }

  fimJogo = 1;
  return 0;
}

void *enviaResposta(void *tsid) {
  char buf[MAXDATASIZE];
  int sid = (int)tsid;
  
  while(!fimJogo) {
    scanf("%s", buf);
    send(sid, buf, strlen(buf)+1, 0);  
  }
  
  pthread_exit(NULL);
}

int recebeAlternativas(int sockfd) {
  char buf[MAXDATASIZE];
  int i;

  i = 1;
  while(1) {
    //Recebe e exibe a alternativa da pergunta
    recv(sockfd, buf, MAXDATASIZE, 0);

    if(strcmp(buf, "eoa") == 0) break; //End of Alternativas
    printf("%d) %s\n", i, buf);
    
    strcpy(buf, NP); 
    send(sockfd, buf, strlen(buf)+1, 0); //Confirmação/Espera de recebimento (No Problem)

    i++;
  }
  
  strcpy(buf, NP); 
  send(sockfd, buf, strlen(buf)+1, 0); //Confirmação/Espera de recebimento (No Problem)  
  
  return 1;
}

int charToint(char c) {
  return c - 'a';
}

int recvtimeout(int s, char *buf, int len, int timeout)
{
  fd_set fds;
  int n;
  struct timeval tv;
  
  // set up the file descriptor set
  FD_ZERO(&fds);
  FD_SET(s, &fds);
  
  // set up the struct timeval for the timeout
  tv.tv_sec = timeout;
  tv.tv_usec = 0;
  
  // wait until timeout or data received
  n = select(s+1, &fds, NULL, NULL, &tv);
  
  if (n == 0) return -2; // timeout!
  if (n == -1) return -1; // error

  // data must be here, so do a normal recv()
  return recv(s, buf, len, 0);
}
