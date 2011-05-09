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

#define PORT "3490" // the port client will be connecting to 
#define MAXDATASIZE 200 // max number of bytes we can get at once 
#define NP "200 OK"

int telaInicial(int sockfd);
int trataConexao(int sockfd);
int charToint(char c);

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
int trataConexao(int sockfd) {
  char buf[MAXDATASIZE];
  int ms, sair;
  
  sair = 0;
  ms = 0;
  while(!sair) {
    if(ms == 0)
      ms = telaInicial(sockfd);
    else if(ms == 1)
      ms = iniciaJogo(sockfd);
    else if(ms == 3)
      sair = 1;
    else
      printf("Operação inválida");
  }
  
  close(sockfd);
  return 1;
}

int iniciaJogo(int sockfd) {
  char buf[MAXDATASIZE];
  int i, num_perg;
  
  //Recebe o número de perguntas
  recv(sockfd, buf, MAXDATASIZE, 0);
  num_perg = atoi(buf);

  strcpy(buf, NP); 
  send(sockfd, buf, strlen(buf)+1, 0); //Confirmação/Espera de recebimento (No Problem)
  
  for(i=0; i<num_perg; i++) {
    //Recebe e exibe o enunciado da pergunta
    recv(sockfd, buf, MAXDATASIZE, 0);
    printf("\n\n%s\n\n", buf);

    strcpy(buf, NP);
    send(sockfd, buf, strlen(buf)+1, 0); //Confirmação/Espera de recebimento (No Problem)
    
    recebeAlternativas(sockfd);
    //printf("ae");
    //recv(sockfd, buf, MAXDATASIZE, 0); //Confirmação/Espera de recebimento (No Problem)

    //scanf("%s", buf);
    //Recebe e envia a resposta da pergunta
    printf("\nDigite o número da resposta:\n");
    scanf("%s", buf);
    send(sockfd, buf, strlen(buf)+1, 0);

    recv(sockfd, buf, MAXDATASIZE, 0);
    printf("%s", buf);
  }
  printf("\n\nFim de jogo!\n\n");
  return 0;
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

int telaInicial(int sockfd) {
  char buf[MAXDATASIZE];
  int numbytes;
  
  recv(sockfd, buf, MAXDATASIZE, 0);
  printf("%s\n", buf);
  scanf("%s", buf);

  send(sockfd, buf, strlen(buf)+1, 0);

  return atoi(buf);  
}

int charToint(char c) {
  return c - 'a';
}