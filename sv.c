/*
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sqlite3.h>
#include <sys/time.h>
#include <sys/types.h>

//Controles Gerais
#define PORT "3490"   // pota que os usuarios irao se conectar
#define BACKLOG 100   // quantas conexoes ficaram na fila

#define NUM_ALTERNATIVAS 4 // numero de questoes por pergunta
#define MAXDATASIZE 1200   // tamanho maximo do protocolo de aplicacao
#define MAXNOMESIZE 20     // tamanho maximo do nome do jogador
#define MAXJOGADORSALA 4   // o maximo de jogadores nas salas
#define PONTOS_ACERTO 17   // pontos por pergunta, quando o jogador acerta
#define PONTOS_ERRO -9     // pontos por pegunta, quando o jogador erra
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

typedef struct _alternativa {
  unsigned char alternativa[200];
  int correta;
  int id;
  struct _alternativa *prox;
}Alternativa;

typedef struct _pergunta {
  unsigned char pergunta[200];

  struct _alternativa *alt;
  struct _pergunta *prox;
}Pergunta;

typedef struct _conexao {
  pthread_t tEnvia;
  char nome[MAXNOMESIZE];
  int sid;                 // socket id
  int estado;              // maquina de Estados
  int pontos;              // pontos atuais em determinado Jogo (Sala)
  int salaId;              // id da sala em que se encontra
  int ativo;               // controle de jogadas
  struct _conexao *prox;
}Conexao;

typedef struct _sala {
  pthread_t tSala;
  int numJogadores;        // quantidade maxima de jogadores na sala
  int id;                  // id da sala
  int ativo[MAXJOGADORSALA];
  int verifica[MAXJOGADORSALA];
  int deleta;
  struct _pergunta *perg;
  struct _conexao *con[MAXJOGADORSALA];
  struct _sala *prox;
}Sala;

typedef struct _salaJogo {
  int id;
  struct _sala *sala;
}SalaJogo;

typedef struct _salaChat {
  int id;
  struct _sala *sala;
}SalaChat;

Pergunta *listaPergunta; 
Conexao *listaConexao;
Sala *listaSala;
pthread_mutex_t mutexsum;

Pergunta *listaPerguntas(Pergunta *ini);
Pergunta *inserePerguntaInicio(const unsigned char *p);
Pergunta *novaPergunta(const unsigned char *p);
Alternativa *insereAlternativaInicio(const unsigned char *p, int correct, int i);
Alternativa *novaAlternativa(const unsigned char *p, int correct, int i);
Pergunta *getUltimaPergunta(Pergunta *ini);
void *trataConexao(void *targs);
Pergunta *sorteiaPergunta(Pergunta *ini);
Conexao *novaConexao(int sid, int estado);
Conexao *insereConexaoInicio(int sid, int estado);
Conexao *insereConexaoFinal(Conexao *ini, int sid, int estado);
Pergunta *selecionaPerguntas();
Sala *getUltimaSala();
Sala *novaSala(Conexao *con, int numJogadores);
Sala *insereSalaFinal(Conexao *con, int numJogadores);
Sala *insereSalaInicio(Conexao *con, int numJogadores);
void *esperaSalaCheia(void *tsala);
Sala *procuraSala(Conexao *con, int numJogadores);
void *chatRecebe(void *tsalaChat);
SalaChat *novaSalaChat(Sala *sala, int id);
void *iniciaJogo(void *tsala);
void *jogoRecebe(void *tsalaJogo);
SalaJogo *novaSalaJogo(Sala *sala, int id);

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
int main(void) {
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }
    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }
    freeaddrinfo(servinfo); // all done with this structure
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    
   listaPergunta = NULL;
   listaPergunta = listaPerguntas(listaPergunta); //Cria uma lista ligada com todas as perguntas até neste instante no banco de dados
   listaConexao = NULL;
   listaSala = NULL;
   
   printf("server: waiting for connections...\n");
    while(1) {  // main accept() loop
	Conexao *con = NULL;
	
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }
        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);
	
	if(listaConexao == NULL) {
          listaConexao = insereConexaoInicio(new_fd, TELA_INICIAL);
	  con = listaConexao;
	} else {
	  con = insereConexaoFinal(listaConexao, new_fd, TELA_INICIAL);
	}
	
	// Cria uma thread em uma lista para cada cliente conectado
	pthread_create(&con->tEnvia, NULL, trataConexao, (void *)con);
    }
    return 0;
}

/***********************************************************
******************** Maquinha de Estados *******************
***********************************************************/

//Recebe os pacotes e passa para serem tratados pela maquina de estados
void *trataConexao(void *tcon) {
  Conexao *con = (Conexao *)tcon;
  char buf[MAXDATASIZE];
  
  
  serializeKTCP(buf, TELA_INICIAL, COK, NONE, "");
  enviaMsgCR(con, buf);
  con->estado = TELA_INICIAL;
  //pthread_create(&con->tRecebe, NULL, controleRecebe, (void *)con);
  
  int i = 0;
  while(!i) {
    recv(con->sid, buf, MAXDATASIZE, 0);
    i = trataRecebimento(con, buf);
  }
    
  pthread_exit(NULL);
  close(con->sid);
}

//Maquina de estado, controla em que parte do jogo o jogador 
//se encontra e envia essa informacao para o mesmo
int trataRecebimento(Conexao *con, char buf[MAXDATASIZE]) {
  int proxEstado = deserializeKTCPProxEstado(buf);
  if(con->estado == TELA_INICIAL) {
    if(proxEstado == 1) {
      serializeKTCP(buf, SELECIONA_JOGO, COK, NONE, "");
      enviaMsgCR(con, buf);
      con->estado = SELECIONA_JOGO;
    }
    if(proxEstado == 2) {
      char aux[MAXDATASIZE];
      getExibeRank(aux);
      serializeKTCP(buf, RANK, COK, NONE, aux);
      enviaMsgCR(con, buf);
      serializeKTCP(buf, TELA_INICIAL, COK, NONE, "");
      enviaMsgCR(con, buf);
      con->estado = TELA_INICIAL;
    }
    if(proxEstado == 3) {
      serializeKTCP(buf, SAIR, COK, NONE, "");
      enviaMsgCR(con, buf);
      con->estado = SAIR;
    }
  }
  else if(con->estado == SELECIONA_JOGO) {
    if(proxEstado == 1) {
      serializeKTCP(buf, JOGAR_2, COK, NONE, "");
      enviaMsgCR(con, buf);
      con->estado = JOGAR_2;
      controleJogo(con, 2);
    }
    if(proxEstado == 2) {
      serializeKTCP(buf, JOGAR_4, COK, NONE, "");
      enviaMsgCR(con, buf);
      con->estado = JOGAR_4;
      controleJogo(con, 4);
    }
    if(proxEstado == 3) {
      serializeKTCP(buf, TELA_INICIAL, COK, NONE, "");
      enviaMsgCR(con, buf);
      con->estado = TELA_INICIAL;
    }
  }
  
  return 0;
}

/***********************************************************
******************* Jogo Multiplayer (Sala) ****************
***********************************************************/

//Procura uma sala, espera a sala encher e depois "inicia jogo"
int controleJogo(Conexao *con, int numJogadores) {
  Sala *sala = NULL;
  con->ativo = 1;
  getNomeJogador(con);
  sala = procuraSala(con, numJogadores);
  if(sala != NULL)
    pthread_create(&sala->tSala, NULL, esperaSalaCheia, (void *)sala);
  
  while(con->ativo) { sleep(1); }
  con->ativo = 1;
  if(sala != NULL)
    pthread_create(&sala->tSala, NULL, iniciaJogo, (void *)sala);
  while(con->ativo) { sleep(1); }
  return 0;
}

//Inicia o jogo, enviando as perguntas, alternativas e recebendo as respostas
void *iniciaJogo(void *tsala) {
  Sala *sala = (Sala *)tsala;
  sala->perg = selecionaPerguntas();
  Pergunta *p = sala->perg;
  SalaJogo *salaJogo = NULL;
  char buf[MAXDATASIZE], aux[MAXDATASIZE];
  pthread_t tJogoRecebe[sala->numJogadores];
  int i, j, k;
  zeraPontos(sala);
  
  while(p != NULL && k<2) {
    sprintf(buf, "\n%s\n\n", p->pergunta);
    enviaMsgSala(sala, buf, EXIBE_MSG, COK);
    verificaRecebimento(sala);
    
    enviaAlternativas(sala, p);
    verificaRecebimento(sala);
    
    enviaMsgSala(sala, "", JOGA, COK);
    verificaRecebimento(sala);
    
    for(i=0; i<sala->numJogadores; i++) {
      salaJogo = novaSalaJogo(sala, i);
      pthread_create(&tJogoRecebe[i], NULL, jogoRecebe, (void *)salaJogo);
    }    
    
    sala->id = -1;
    ativaVerifica(sala);
    while(verificaAtivo(sala) == -1 && sala->id == -1) { sleep(1); }
    desativaVerifica(sala);
    for(i=0; i<sala->numJogadores; i++)
      pthread_cancel(tJogoRecebe[i]);
    enviaMsgSala(sala, "", NONE, CFF);
    verificaRecebimento(sala);
    
    enviaMsgSala(sala, "", NONE, COK);
    verificaRecebimento(sala);
    p = p->prox;
    sala->perg = p;
    k++;
  }
  int vid = enviaMsgVenceu(sala);
  verificaRecebimento(sala);
  
  adicionaRank(sala->con[vid]->nome, sala->con[vid]->pontos);
  
  enviaMsgSala(sala, "", TELA_INICIAL, COK);
  verificaRecebimento(sala);
  atualizaSalaEstado(sala, TELA_INICIAL);
  
  desativaSala(sala);
  sala->deleta = 1;
  deletaSala();
  free(sala->perg);
  free(sala);
  pthread_exit(NULL);
}

//Recebe uma resposta para cada alternativa de cada jogador
void *jogoRecebe(void *tsalaJogo) {
  SalaJogo *salaJogo = (SalaJogo *)tsalaJogo;
  char buf[MAXDATASIZE], aux[MAXDATASIZE];
  int sid, id;
  id = salaJogo->id;
  sid = salaJogo->sala->con[id]->sid;

  recv(sid, buf, MAXDATASIZE, 0);
  deserializeKTCPDados(buf, aux);
    
  if(verificaResposta(salaJogo->sala->perg, aux)) {
    if(salaJogo->sala->id == -1) {
      pthread_mutex_lock(&mutexsum);
      salaJogo->sala->id = id;
      pthread_mutex_unlock(&mutexsum);
    }
    salaJogo->sala->con[id]->pontos += PONTOS_ACERTO;
    sprintf(buf, "\nO jogador %s acertou e ganhou %d pontos!\n", salaJogo->sala->con[id]->nome, PONTOS_ACERTO);
    enviaMsgSalaLess(salaJogo->sala, sid, buf, NONE, NONE);
    sprintf(buf, "\nVoce acertou e ganhou %d pontos!\n", PONTOS_ACERTO);
  } else {
    sprintf(buf, "\nVoce errou e perdeu %d pontos!\n", -1*PONTOS_ERRO);
    salaJogo->sala->con[id]->pontos += PONTOS_ERRO;
  }
  serializeKTCP(aux, NONE, NONE, NONE, buf);
  enviaMsg(salaJogo->sala->con[id], aux);
  salaJogo->sala->verifica[id] = 0;

  pthread_exit(NULL);
}

//Indica que o jogo em determinada Sala terminou e a sala pode ser deletada
int deletaSala() {
  pthread_mutex_lock(&mutexsum);
  Sala *aux = listaSala;
  if(aux->deleta == 1) {
    listaSala = aux->prox;
    return 0;
  }
  while(aux->prox->deleta == 0)
    aux = aux->prox;
  aux->prox = aux->prox->prox;
  pthread_mutex_unlock(&mutexsum);
  return 0;
}

//Atualiza a maquina de estados de todos os jogadores em uma Sala
int atualizaSalaEstado(Sala *sala, int estado) {
  int i;
  for(i=0; i<sala->numJogadores; i++)
    sala->con[i]->estado = estado;
  return 0;
}

//Envia uma mensagem para todos da sala indicando o vencedor da sala
int enviaMsgVenceu(Sala *sala) {
  int i, vid, pontos;
  char buf[MAXDATASIZE];
  vid = 0;
  pontos = sala->con[vid]->pontos;
  for(i=1; i<sala->numJogadores; i++) {
    if(sala->con[i]->pontos > pontos) {
      pontos = sala->con[i]->pontos;
      vid = i;
    }
  }
  sprintf(buf, "\nO jogador %s é o vencedor com %d pontos. Parabeins!!\n", sala->con[vid]->nome, pontos);
  enviaMsgSala(sala, buf, EXIBE_MSG, COK);
  return vid;
}

//Verifica o recebimento dos pacotes enviados pelos jogadores de uma sala
int verificaRecebimento(Sala *sala) {
  char buf[MAXDATASIZE];
  int i;
  for(i=0; i<sala->numJogadores; i++)
    recv(sala->con[i]->sid, buf, MAXDATASIZE, 0);
  return 0;
}

//Zera os pontos dos jogadores de uma sala
int zeraPontos(Sala *sala) {
  int i;
  for(i=0; i<sala->numJogadores; i++)
    sala->con[i]->pontos = 0;
   return 0;
}

//Verifica se os jogadores em uma sala estao ativo (controle de pergunta-resposta)
int verificaAtivo(Sala *sala) {
  int i;
  for(i=0; i<sala->numJogadores; i++)
    if(sala->verifica[i] == 1)
      return -1;
  return 0;
}

//Verifica se o jogo terminou, entre outras coisas
int verificaJogoAtivo(Sala *sala) {
  int i;
  for(i=0; i<sala->numJogadores; i++)
    if(sala->ativo[i] == 1)
      return -1;
  return 0;
}

int ativaVerifica(Sala *sala) {
  int i;
  for(i=0; i<sala->numJogadores; i++)
    sala->verifica[i] = 1;
  return 0;
}

int desativaVerifica(Sala *sala) {
  int i;
  for(i=0; i<sala->numJogadores; i++)
    sala->verifica[i] = 0;
  return 0;
}

int ativaJogo(Sala *sala) {
  int i;
  for(i=0; i<sala->numJogadores; i++)
    sala->ativo[i] = 1;
  return 0;
}

int desativaJogo(Sala *sala) {
  int i;
  for(i=0; i<sala->numJogadores; i++)
    sala->ativo[i] = 0;
  return 0;
}

int enviaAlternativas(Sala *sala, Pergunta *p) {
  char buf[MAXDATASIZE];
  Alternativa *aux = p->alt;
  sprintf(buf, "A) %s\nB) %s\nC) %s\nD) %s\n", aux->alternativa, aux->prox->alternativa, aux->prox->prox->alternativa, aux->prox->prox->prox->alternativa);
  enviaMsgSala(sala, buf, EXIBE_MSG, COK);
  return 0;
}

SalaJogo *novaSalaJogo(Sala *sala, int id) {
  SalaJogo *novo = (SalaJogo *)malloc(sizeof(SalaJogo));
  novo->id = id;
  novo->sala = sala;
  return novo;
}

void *esperaSalaCheia(void *tsala) {
  Sala *sala = (Sala *)tsala;
  SalaChat *salaChat = NULL;
  char buf[MAXDATASIZE], aux[MAXDATASIZE];
  int i, j, k;
  pthread_t tChatRecebe[sala->numJogadores];
  k = 0;
  j = 0;
  while(1) {
    i = getSalaNumJogadores(sala);
    sprintf(buf, "\nAguardando jogadores! [%d/%d]\n", i, sala->numJogadores);
    if(j != i) {
      enviaMsgSala(sala, buf, ESPERA_SALA_CHEIA, NONE);
      salaChat = novaSalaChat(sala, k);
      pthread_create(&tChatRecebe[k], NULL, chatRecebe, (void *)salaChat);
      k++;
      j = i;
    }

    if(i == sala->numJogadores)
      break;
    sleep(1);
  }
  enviaCooldown(sala, 3);  
  enviaMsgSala(sala, aux, ESPERA_SALA_CHEIA, CFT);
  for(k=0; k<sala->numJogadores; k++)
    pthread_cancel(tChatRecebe[k]);
  desativaSala(sala);
  pthread_exit(NULL);
}

int desativaSala(Sala *sala) {
  int i;
  pthread_mutex_lock(&mutexsum);
  for(i=0; i<sala->numJogadores; i++)
    sala->con[i]->ativo = 0;
  pthread_mutex_unlock(&mutexsum);
  return 0;
}

int enviaCooldown(Sala *sala, int segundos) {
  char buf[MAXDATASIZE];
  int i = segundos;
  while(i != 0) {
    sprintf(buf, "O jogo começara em %d segundos.\n", i);
    enviaMsgSala(sala, buf, ESPERA_SALA_CHEIA, NONE);
    i--;
    sleep(1);
  }
  return 0;
}

//Envia mensagem para todos os jogadores de uma sala
int enviaMsgSala(Sala *sala, char buf[MAXDATASIZE], int tela, int controle) {
  char aux[MAXDATASIZE];
  int i;
  for(i=0; i<sala->numJogadores; i++) {
    if(sala->con[i] != NULL) {
      serializeKTCP(aux, tela, controle, NONE, buf);
      enviaMsg(sala->con[i], aux);
    }
  }
  return 0;
}

//Envia mensagem para todos os jogadores de uma sala menos o sid (socket id) indicado
int enviaMsgSalaLess(Sala *sala, int sid, char buf[MAXDATASIZE], int tela, int controle) {
  char aux[MAXDATASIZE];
  int i;
  for(i=0; i<sala->numJogadores; i++) {
    if(sala->con[i] != NULL && sala->con[i]->sid != sid) {
      serializeKTCP(aux, tela, controle, NONE, buf);
      enviaMsg(sala->con[i], aux);
    }
  }
  return 0;
}

int getSalaNumJogadores(Sala *sala) {
  int i, j;
  for(i=0, j=0; i<sala->numJogadores; i++)
    if(sala->con[i] != NULL)
      j++;
    
  return j;
}

Sala *procuraSala(Conexao *con, int numJogadores) {
  Sala *sala = NULL;
  pthread_mutex_lock(&mutexsum);
  if(listaSala == NULL) {
    sala = insereSalaInicio(con, numJogadores);
  } else {
    int n;
    if(n=procuraVagaSala(con, numJogadores) == -1)
      sala = insereSalaFinal(con, numJogadores);
  }
  pthread_mutex_unlock(&mutexsum);
  return sala;
}

int procuraVagaSala(Conexao *con, int numJogadores) {
  Sala *aux = listaSala;
  int i;
  while(aux != NULL) {
    if(aux->numJogadores == numJogadores)
      for(i=1; i<numJogadores; i++) {
	if(aux->con[i] == NULL) {
	  con->salaId = i;
	  aux->con[i] = con;
	  return 0;
	}
      }
    aux = aux->prox;
  }
  return -1;
}

Sala *insereSalaFinal(Conexao *con, int numJogadores) {
  Sala *aux = getUltimaSala();
  while(aux->prox != NULL) aux = aux->prox;
  Sala *novo = novaSala(con, numJogadores);
  aux->prox = novo;
  return novo;
}

Sala *insereSalaInicio(Conexao *con, int numJogadores) {
  Sala *novo = novaSala(con, numJogadores);
  listaSala = novo;
  return novo;
}

Sala *novaSala(Conexao *con, int numJogadores) {
  Sala *novo = (Sala *)malloc(sizeof(Sala));
  int i;
  con->salaId = 0;
  novo->con[0] = con;
  for(i=1; i<MAXJOGADORSALA-1; i++)
    novo->con[i] = NULL;
  novo->deleta = 0;
  novo->numJogadores = numJogadores;
  novo->prox = NULL;
  return novo;
}

Sala *getUltimaSala() {
  Sala *aux = listaSala;
  while(aux->prox != NULL) aux = aux->prox;
  return aux;
}

int getNomeJogador(Conexao *con) {
  char buf[MAXNOMESIZE];
  recv(con->sid, buf, MAXNOMESIZE, 0);
  deserializeKTCPDados(buf, con->nome);
  return 0;
}

/***********************************************************
************************** Chat ****************************
***********************************************************/

//Cria um simples chat para os jogadores conversarem
//enquanto esperam a sala encher
SalaChat *novaSalaChat(Sala *sala, int id) {
  SalaChat *novo = (SalaChat *)malloc(sizeof(SalaChat));
  novo->id = id;
  novo->sala = sala;
  return novo;
}

void *chatRecebe(void *tsalaChat) {
  SalaChat *salaChat = (SalaChat *)tsalaChat;
  char buf[MAXDATASIZE], aux[MAXDATASIZE];
  int sid = salaChat->sala->con[salaChat->id]->sid;
  char nome[MAXNOMESIZE];
  strcpy(nome, salaChat->sala->con[salaChat->id]->nome);
  
  while(1) {
    recv(sid, buf, MAXDATASIZE, 0);
    deserializeKTCPDados(buf, aux);
    if(strcmp(aux, "") != 0) {
      sprintf(buf, "%s, diz: %s\n", nome, aux);
      enviaMsgSala(salaChat->sala, buf, ESPERA_SALA_CHEIA, NONE);
    }
  }
  pthread_exit(NULL);
}


/***********************************************************
************************ Controles *************************
***********************************************************/

//Envia mensagem pedindo confirmacao de recebimento
int enviaMsgCR(Conexao *con, char buf[MAXDATASIZE]) {
  while(1) {
    send(con->sid, buf, strlen(buf)+1, 0);
    recv(con->sid, buf, MAXDATASIZE, 0);
    if(deserializeKTCPControle(buf) == CYEP)
      break;
  }
  return 0;
}

int enviaMsg(Conexao *con, char buf[MAXDATASIZE]) {
  send(con->sid, buf, strlen(buf)+1, 0);
  return 0;
}


void *controleRecebe(void *tcon) {
  Conexao *con = (Conexao *)tcon;
  char buf[MAXDATASIZE];
  
  while(1) {
    recv(con->sid, buf, MAXDATASIZE, 0);
  }
 
  pthread_exit(NULL);
}


/***********************************************************
************ Kuizz Transfer Control Protocol ***************
***********************************************************/

//Serializa o protocolo de aplicacao
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


/***********************************************************
************************* Telas ****************************
***********************************************************/

int getExibeRank(char aux[MAXDATASIZE]) {
    char buf[MAXDATASIZE];
    int retval, row, i;
    sqlite3 *handle;
    sqlite3_stmt *stmt;
    
    retval = sqlite3_open("kizz.sqlite", &handle);
    
    retval = sqlite3_prepare_v2(handle, "SELECT r.nome, r.pontos FROM Rank r ORDER BY r.pontos DESC LIMIT 10;", -1, &stmt, 0);
    if(retval) {
      perror("SELECT database");
      return 0;
    }
    
    strcpy(aux, "\n");
    i = 0;
    row = 0;
    while(1) {
      retval = sqlite3_step(stmt);
      
      if(retval == SQLITE_ROW) {
	//Processo encarregado de guardar na lista ligada determinada Pergunta e suas Alternativas com a quantidade constante definida por NUM_ALTERNATIVAS 
	sprintf(buf, "%s\t\t%s\n", sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1));
	strcat(aux, buf);
      }
      //Caso não haja mais registros a serem lidos ele executará o break;
      else if(retval == SQLITE_DONE) break;
      else {
	perror("SELECT row");
	return 0;
      }
    }
    //Fecha conexão
    sqlite3_close(handle);
    strcpy(buf, "Nome\t\t\tPontos\n");
    strcat(aux, buf);
    return 0;
}


int adicionaRank(char *nome, int pontos) {
  int retval;
  sqlite3 *handle;
  char sql[250];
  retval = sqlite3_open("kizz.sqlite", &handle);
  
  sprintf(sql, "INSERT INTO Rank (nome, pontos) VALUES ('%s', %d);", nome, pontos);
  retval = sqlite3_exec(handle, sql, 0, 0, 0);
  //Fecha a conexão
  sqlite3_close(handle);
  return 1;
}


/***********************************************************
************************* Pergunta *************************
***********************************************************/

//Função que controla a listagem de todas as perguntas e suas devidas alternativas no bando de dados
//Retorna uma lista ligada de Perguntas
Pergunta *listaPerguntas(Pergunta *ini) {
    Pergunta *ultimaPergunta;
    int retval, row, i;
    sqlite3 *handle;
    sqlite3_stmt *stmt;
    
    retval = sqlite3_open("kizz.sqlite", &handle);
    
    retval = sqlite3_prepare_v2(handle, "SELECT p.idPergunta, p.pergunta, a.alternativa, a.correta FROM Pergunta p, Alternativa a WHERE a.idPergunta = p.idPergunta ORDER BY p.idPergunta;", -1, &stmt, 0);
    if(retval) {
      perror("SELECT database");
      return 0;
    }

    ini = NULL;
    i = 0;
    row = 0;
    while(1) {
      retval = sqlite3_step(stmt);
      
      if(retval == SQLITE_ROW) {
	//Processo encarregado de guardar na lista ligada determinada Pergunta e suas Alternativas com a quantidade constante definida por NUM_ALTERNATIVAS 
	if(i == 0) {
	  if(ini == NULL) {
	    //Caso a lista seja NULL insere a pergunta na cabeça da lista
	    ini = inserePerguntaInicio(sqlite3_column_text(stmt, 1));
	  } else {
	    //Caso contrário insere no final da lista
	    inserePerguntaFinal(ini, sqlite3_column_text(stmt, 1));
	  }
	  //Busca a última pergunta adicionada a lista ou seja a pergunta atual
	  ultimaPergunta = getUltimaPergunta(ini);
	}
	//Com a pergunta atual (ultimaPergunta) em mãos, criamos uma lista ligada de alternativas para a pergunta
	insereAlternativa(ultimaPergunta, sqlite3_column_text(stmt, 2), atoi(sqlite3_column_text(stmt, 3)), i);	
	i++;
	if(i == NUM_ALTERNATIVAS) i = 0;
      }
      //Caso não haja mais registros a serem lidos ele executará o break;
      else if(retval == SQLITE_DONE) break;
      else {
	perror("SELECT row");
	return 0;
      }
    }
    //Fecha conexão
    sqlite3_close(handle);

    return ini;
}

Pergunta *selecionaPerguntas() {
  Pergunta *p = listaPergunta;
  return p;
}

//Verifica a resposta para determinada pergunta
int verificaResposta(Pergunta *p, char c[MAXDATASIZE]) {
  Alternativa *aux = p->alt;
  int i, n;
  n = tolower(c[0]) - 'a';
  for(i=0; i<n && aux->prox != NULL; i++) {
    aux = aux->prox;
  }
  if(aux == NULL)
    return 0;
  return aux->correta;
}

int listaSize(Pergunta  *ini) {
  int i;
  Pergunta *aux = ini;
  i = 0;
  while(aux != NULL) {
    aux = aux->prox;
    i++;
  }

  return i;
}

//Função que insere no final da lista ligada das Perguntas, uma pergunta
int inserePerguntaFinal(Pergunta *ini, const unsigned char *p) {
  Pergunta *aux = ini;
  while(aux->prox != NULL) aux = aux->prox;
  Pergunta *novo = novaPergunta(p);
  aux->prox = novo;
  return 1;
}

//Função que insere no início (cabeça) da lista ligada Perguntas
//Retorna um ponteiro para a cabeça
Pergunta *inserePerguntaInicio(const unsigned char *p) {
  Pergunta *novo = novaPergunta(p);
  return novo;
}

//Função responsável por criar um "Nó" para ser colocado na lista ligada das Perguntas
//Retorna um ponteiro para o Nó criado
Pergunta *novaPergunta(const unsigned char *p) {
  Pergunta *novo = malloc(sizeof(Pergunta));
  strcpy(novo->pergunta, p);
  novo->alt = NULL;
  novo->prox = NULL;
  
  return novo;
}

//Função responsável por pegar o último Nó de uma lista ligada das Perguntas
//Retorna um ponteiro para o último nó
Pergunta *getUltimaPergunta(Pergunta *ini) {
  Pergunta *aux = ini;
  while(aux->prox != NULL) aux = aux->prox;
  return aux;
}


/***********************************************************
************************* Alternativa **********************
***********************************************************/

//Função que controla a inserção das alternativas em determina Pergunta
int insereAlternativa(Pergunta *pergunta, const unsigned char *p, int correct, int i) {
  if(pergunta->alt == NULL) {
    pergunta->alt = insereAlternativaInicio(p, correct, i);
  } else {
    insereAlternativaFinal(pergunta->alt, p, correct, i);
  }
  return 1;
}

//Função que insere no final da lista ligada das Alternativas, uma alternativa
int insereAlternativaFinal(Alternativa *ini, const unsigned char *p, int correct, int i) {
  Alternativa *aux = ini;
  while(aux->prox != NULL) aux = aux->prox;
  Alternativa *novo = novaAlternativa(p, correct, i);
  aux->prox = novo;
  return 1;
}

//Função que insere no início (cabeça) da lista ligada das Alternativas
//Retorna um ponteiro para a cabeça
Alternativa *insereAlternativaInicio(const unsigned char *p, int correct, int i) {
  Alternativa *novo = novaAlternativa(p, correct, i);
  return novo;
}

//Função responsável por criar um "Nó" para ser colocado na lista ligada das Alternativas
//Retorna um ponteiro para o Nó criado
Alternativa *novaAlternativa(const unsigned char *p, int correct, int i) {
  Alternativa *novo = malloc(sizeof(Alternativa));
  strcpy(novo->alternativa, p);
  novo->correta = correct;
  novo->id = i;
  novo->prox = NULL;
  return novo;
}


/***********************************************************
************************* Conexao **************************
***********************************************************/

//Função que insere no final da lista ligada das Conexao, uma pergunta
Conexao *insereConexaoFinal(Conexao *ini, int sid, int estado) {
  Conexao *aux = ini;
  while(aux->prox != NULL) aux = aux->prox;
  Conexao *novo = novaConexao(sid, estado);
  aux->prox = novo;
  return novo;
}

//Função que insere no início (cabeça) da lista ligada Conxao
//Retorna um ponteiro para a cabeça
Conexao *insereConexaoInicio(int sid, int estado) {
  Conexao *novo = novaConexao(sid, estado);
  return novo;
}

//Função responsável por criar um "Nó" para ser colocado na lista ligada das Conexao
//Retorna um ponteiro para o Nó criado
Conexao *novaConexao(int sid, int estado) {
  Conexao *novo = malloc(sizeof(Conexao));
  novo->sid = sid;
  novo->estado = estado;
  novo->prox = NULL;
  
  return novo;
}


/***********************************************************
************************* Outros ***************************
***********************************************************/

//Gera um número aleatório de 0 até N
int randomN(int n) {
  int r = random()/(RAND_MAX / n);
  return r;
}

int substring(char origem[MAXDATASIZE], int inicio, int quantidade, char destino[MAXDATASIZE]) {
  int i;
  for(i=0; i<=quantidade; i++, inicio++) {
    destino[i] = origem[inicio];
  }
  destino[i+1] = '\0';
  return 0;
}