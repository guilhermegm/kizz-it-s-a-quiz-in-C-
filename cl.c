/*
** client.c -- a stream socket client demoae
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

//Controles Gerais
#define PORT "3490"   // pota que os usuarios irao se conectar
#define BACKLOG 100   // quantas conexoes ficaram na fila
#define MAXDATASIZE 1200   // tamanho maximo do protocolo de aplicacao
#define NP "200 OK"
//End-Controles Gerais

//Controles da Maquina de Estado, utilizado no protoloco de aplicacao
//Telas
#define NONE 0             // nao definido/nao faz nada
#define TELA_INICIAL 1
#define SELECIONA_JOGO 2
#define RANK 3
#define SAIR 4
//End-Telas
//Jogo
#define JOGAR_2 100 
#define JOGAR_4 101
#define JOGAR_8 102
#define ESPERA_SALA_CHEIA 201
#define JOGA 202
//End-Jogo
//Geral
#define EXIBE_MSG 203
//End-Geral
//End-Controles da Maquina de Estado

//Controles de verificação, utilizado no protoloco de aplicacao
#define COK 1              // Pergunta se a mensagem enviada foi recebida (Recebeu?)
#define CYEP 2             // Resposta para a pergunta do COK (Sim)
#define CFT 3              // Controle Finaliza Tarefa sem confirmacao de recebimento
#define CFF 4              // Controle Forçar Finalizacao com confirmacao de recebimento
//End-Controles de verificação

typedef struct _salaChat {
  pthread_t tChatRecebe;
  pthread_t tChatEnvia;
  int sid;
  int ativo;
}SalaChat;

typedef struct _salaJogo {
  pthread_t tSalaJogoRecebe;
  pthread_t tSalaJogoEnvia;
  int sid;
  int ativo;
}SalaJogo;

pthread_t tEnvia, tRecebe;
int estado;

void *enviaResposta(void *aux);
void *controleEnvia(void *tsid);
void *controleRecebe(void *tsid);
void *chatRecebe(void *tsalaChat);
void *chatEnvia(void *tsalaChat);
SalaChat *novaSalaChat(int sid);
void *salaJogoEnvia(void *tsalaJogo);
void *salaJogoRecebe(void *tsalaJogo);
SalaJogo *novaSalaJogo(int sid);

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
    
    //pthread_create(&tEnvia, NULL, controleEnvia, (void *)sockfd);
    pthread_create(&tRecebe, NULL, controleRecebe, (void *)sockfd);
    
    pthread_exit(NULL);
    close(sockfd);
    return 0;
}

void *controleEnvia(void *tsid) {
  int sid = (int)tsid;
  char buf[MAXDATASIZE];
  
  while(1) {
    scanf("%s", buf);
    serializeKTCP(buf, NONE, CYEP, atoi(buf), buf);
    send(sid, buf, strlen(buf)+1, 0);
  }
  
  pthread_exit(NULL);
}

void *controleRecebe(void *tsid) {
  int sid = (int)tsid;
  char buf[MAXDATASIZE];
  int eog = 0; //End of Game
  
  while(!eog) {
    recv(sid, buf, MAXDATASIZE, 0);
    eog = trataRecebimento(sid, buf);
  }
 
  pthread_exit(NULL);
}

int trataRecebimento(int sid, char buf[MAXDATASIZE]) {
  char buf2[MAXDATASIZE];
  int controle = deserializeKTCPControle(buf);
  if(controle != NONE) {
    if(controle == COK) {
      serializeKTCP(buf2, NONE, CYEP, NONE, "");
      send(sid, buf2, strlen(buf2)+1, 0);
    }
  }

  int tela = deserializeKTCPTela(buf);
  if(tela != NONE) {
    if(tela == TELA_INICIAL) {
      exibeTelaInicial();
      enviaMsgJogador(sid);
    }
    if(tela == RANK) {
      char aux[MAXDATASIZE];
      deserializeKTCPDados(buf, aux);
      exibeMsg(aux);
      fflush(stdout);
    }
    else if(tela == SAIR) {
      exibeTelaSair();
      return 1;
    }
    else if(tela == SELECIONA_JOGO) {
      exibeSelecionaJogo();
      enviaMsgJogador(sid);
    }
    else if(tela == JOGAR_2) {
      exibeGetNomeJogador();
      enviaMsgJogador(sid);
    }
    else if(tela == JOGAR_4) {
      exibeGetNomeJogador();
      enviaMsgJogador(sid);
    }
    else if(tela == ESPERA_SALA_CHEIA) {
      char aux[MAXDATASIZE];
      deserializeKTCPDados(buf, aux);
      exibeMsg(aux);
      iniciaChat(sid);
    }
    else if(tela == EXIBE_MSG) {
      char aux[MAXDATASIZE];
      deserializeKTCPDados(buf, aux);
      fflush(stdout);
      exibeMsg(aux);
    }
    else if(tela == JOGA) {
      iniciaJogo(sid);
    }
  }
  return 0;
}

//Jogo
int iniciaJogo(int sid) {
  SalaJogo *salaJogo = novaSalaJogo(sid);
  salaJogo->ativo = 1;
  pthread_create(&salaJogo->tSalaJogoEnvia, NULL, salaJogoEnvia, (void *)salaJogo);
  pthread_create(&salaJogo->tSalaJogoRecebe, NULL, salaJogoRecebe, (void *)salaJogo);
  
  while(salaJogo->ativo) { sleep(1); }

  pthread_cancel(salaJogo->tSalaJogoRecebe);
  free(salaJogo);
  return 0;
}

void *salaJogoEnvia(void *tsalaJogo) {
  SalaJogo *salaJogo = (SalaJogo *)tsalaJogo;
  enviaMsgJogador(salaJogo->sid);
  pthread_exit(NULL);
}

void *salaJogoRecebe(void *tsalaJogo) {
  SalaJogo *salaJogo = (SalaJogo *)tsalaJogo;
  char buf[MAXDATASIZE], aux[MAXDATASIZE];
  while(1) {
    recv(salaJogo->sid, buf, MAXDATASIZE, 0);
    if(deserializeKTCPControle(buf) == CFF) {
      pthread_cancel(salaJogo->tSalaJogoEnvia);
      serializeKTCP(aux, NONE, CYEP, NONE, "");
      send(salaJogo->sid, aux, strlen(aux)+1, 0);
      break;
    }
    deserializeKTCPDados(buf, aux);
    printf("%s", aux);
    sleep(1);
  }
  salaJogo->ativo = 0;
  pthread_exit(NULL);
}

SalaJogo *novaSalaJogo(int sid) {
  SalaJogo *novo = (SalaJogo *)malloc(sizeof(SalaJogo));
  novo->sid = sid;
  return novo;
}

//Chat
int iniciaChat(int sid) {
  SalaChat *salaChat = novaSalaChat(sid);
  salaChat->ativo = 1;
  pthread_create(&salaChat->tChatEnvia, NULL, chatEnvia, (void *)salaChat);
  pthread_create(&salaChat->tChatRecebe, NULL, chatRecebe, (void *)salaChat);
  //pthread_exit(NULL);
  while(salaChat->ativo) { sleep(1); }
  pthread_cancel(salaChat->tChatRecebe);
  pthread_cancel(salaChat->tChatEnvia);
  return 0;
}

void *chatEnvia(void *tsalaChat) {
  SalaChat *salaChat = (SalaChat *)tsalaChat;
  while(1) {
    enviaMsgJogadorGets(salaChat->sid);
  }
  pthread_exit(NULL);
}

void *chatRecebe(void *tsalaChat) {
  SalaChat *salaChat = (SalaChat *)tsalaChat;
  char buf[MAXDATASIZE], aux[MAXDATASIZE];
  while(1) {
    recv(salaChat->sid, buf, MAXDATASIZE, 0);
    if(deserializeKTCPControle(buf) == CFT)
      break;
    deserializeKTCPDados(buf, aux);
    exibeMsg(aux);
  }
  salaChat->ativo = 0;
  pthread_exit(NULL);
}

SalaChat *novaSalaChat(int sid) {
  SalaChat *novo = (SalaChat *)malloc(sizeof(SalaChat));
  novo->sid = sid;
  return novo;
}
//End-Chat

//Controles
int enviaMsgJogador(int sid) {
  char buf[MAXDATASIZE], aux[MAXDATASIZE];
  scanf("%s", aux);
  serializeKTCP(buf, NONE, NONE, atoi(aux), aux);
  send(sid, buf, strlen(buf)+1, 0);
  return 0;
}

int enviaMsgJogadorGets(int sid) {
  char buf[MAXDATASIZE], aux[MAXDATASIZE];
  gets(aux);
  serializeKTCP(buf, NONE, NONE, atoi(aux), aux);
  send(sid, buf, strlen(buf)+1, 0);
  return 0;
}

//Kuizz Transfer Control Protocol
int serializeKTCP(char buf[MAXDATASIZE], int tela, int controle, int proxEstado, char dados[MAXDATASIZE]) {
  sprintf(buf, "%3d%1d%1d%s", tela, controle, proxEstado, dados);
  return 0;
}

int deserializeKTCPTela(char buf[MAXDATASIZE]) {
  char aux[MAXDATASIZE];
  sprintf(aux, "%1c%1c%1c", buf[0], buf[1], buf[2]);
  return atoi(aux);
}

int deserializeKTCPControle(char buf[MAXDATASIZE]) {
  char aux[MAXDATASIZE];
  sprintf(aux, "%1c", buf[3]);
  return atoi(aux);
}

int deserializeKTCPProxEstado(char buf[MAXDATASIZE]) {
  char aux[MAXDATASIZE];
  sprintf(aux, "%1c", buf[4]);
  return atoi(aux);
}

int deserializeKTCPDados(char buf[MAXDATASIZE], char destino[MAXDATASIZE]) {
  substring(buf, 5, strlen(buf)-5, destino);
  return 0;
}

int substring(char origem[MAXDATASIZE], int inicio, int quantidade, char destino[MAXDATASIZE]) {
  int i;
  for(i=0; i<=quantidade; i++, inicio++) {
    destino[i] = origem[inicio];
  }
  destino[i+1] = '\0';
  return 0;
}
//End-Controles

/***********************************************
 ******************* Telas *********************
 ***********************************************/
int exibeTelaInicial() {
  char buf[MAXDATASIZE];
  printf("\nKUIZZ\n\n1) Jogar\n2) Rank\n3) Sair\n");
  return 0;
}

int exibeTelaSair() {
  char buf[MAXDATASIZE];
  printf("\nObrigado por jogar!\n");
  return 0;
}

int exibeSelecionaJogo() {
  char buf[MAXDATASIZE];
  printf("\nKUIZZ\n\n1) 2 Jogadores\n2) 4 Jogadores\n3) Voltar\n");
  return 0;
}

int exibeGetNomeJogador() {
  char buf[MAXDATASIZE];
  printf("\nDigite seu nome:\n");
  return 0;  
}

int exibeMsg(char buf[MAXDATASIZE]) {
  printf("%s", buf);  
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
      //telaInicial(sid);
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

/*int iniciaJogo(int sockfd) {
  char buf[MAXDATASIZE];
  int receiv;
  
  //Aguardando Jogadores
  while(1) {
    receiv = recv(sockfd, buf, MAXDATASIZE, 0);
    if(strcmp(buf, NP) == 0)
      break;
    printf("%s", buf);
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
}*/

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
