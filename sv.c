/*
** server.c -- a stream socket server demo
INSERT INTO Pergunta (pergunta) VALUES("");
INSERT INTO Alternativa (idPergunta, alternativa, correta) VALUES (1, "", 0);
SELECT p.idPergunta, p.pergunta, a.alternativa, a.correta FROM Pergunta p, Alternativa a WHERE a.idPergunta = p.idPergunta;
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

#define PORT "3490"  // the port users will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold

#define NUM_ALTERNATIVAS 4 // Numero de questoes por pergunta
#define MAXDATASIZE 1200
#define NP "200 OK"
#define NUM_THREADS 100000
#define MAX_SALA_JOGO 100

#define TELA_INICIAL 1
#define SELECIONA_JOGO 2
#define RANK 3
#define SAIR 4

#define JOGAR_2 5
#define JOGAR_4 6
#define JOGAR_8 7

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
  pthread_t tcon; //thread da conexão
  int sid;        //socket id
  int estado;     //Maquina de Estados
  int pontos;
  int ctJogo;
  struct _conexao *prox;
}Conexao;

typedef struct _salaJogo {
  pthread_t tsala;
  int receiv;
  int jogoFim;
  int sidResp;
  struct _pergunta *perg;
  struct _conexao *con1;
  int con1Resp;
  struct _conexao *con2;
  int con2Resp;
  struct _salaJogo *prox;
}SalaJogo;

Pergunta *listaPergunta; 
Conexao *listaConexao;
pthread_mutex_t mutexsum;
SalaJogo *salaJogo2;
SalaJogo *listaJogo4;
SalaJogo *listaJogo8;

Pergunta *listaPerguntas(Pergunta *ini);
int inserePergunta(Pergunta *ini, const unsigned char *p);
int inserePerguntaFinal(Pergunta *ini, const unsigned char *p);
Pergunta *inserePerguntaInicio(const unsigned char *p);
Pergunta *novaPergunta(const unsigned char *p);
int insereAlternativa(Pergunta *pergunta, const unsigned char *p, int correct, int i);
int insereAlternativaFinal(Alternativa *ini, const unsigned char *p, int correct, int i);
Alternativa *insereAlternativaInicio(const unsigned char *p, int correct, int i);
Alternativa *novaAlternativa(const unsigned char *p, int correct, int i);
Pergunta *getUltimaPergunta(Pergunta *ini);
void *trataConexao(void *targs);
Pergunta *sorteiaPergunta(Pergunta *ini);
Conexao *novaConexao(int sid, int estado);
Conexao *insereConexaoInicio(int sid, int estado);
Conexao *insereConexaoFinal(Conexao *ini, int sid, int estado);
SalaJogo *insereSalaJogoInicio(Conexao *con);
SalaJogo *novaSalaJogo(Conexao *con);
SalaJogo *getUltimaSalaJogo();
void *gerenciaSalaJogo(void *t);
Pergunta *selecionaPerguntas();
void *controlaJogo1(void *tsalaJogo);
void *controlaJogo2(void *tsalaJogo);

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
   
   salaJogo2 = NULL;

   int rc;
   
   rc = 0;
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

	rc = pthread_create(&con->tcon, NULL, trataConexao, (void *)con);		
    }
    return 0;
}

// 0 - Tela Inicial // 1 - Jogar // 2 - Rank // 3 - Sair
void *trataConexao(void *tcon) {
  Conexao *con = (Conexao *)tcon;
  int sair, receiv;
  char buf[MAXDATASIZE];

  
  telas(con);
  
  //printf("XDDD, %d, %d!\n", con->sid, con->estado);

  //sleep(1);

  /*
  sair = 0;
  ms = 0;
  while(!sair){
    if(ms == 0)
      ms = telaInical(sockfd);
    else if(ms == 1)
      ms = iniciaJogo(sockfd);
    else if(ms == 2)
      ms = exibeRank(sockfd);
    else if(ms == 3)
      sair = 1;
  }*/
  printf("conct closed");
  fflush(stdout);
  
  pthread_exit(NULL);
  close(con->sid);
}

int telas(Conexao *con) {
  char buf[MAXDATASIZE];
  int receiv;
  
  while(1) {
    if(con->estado == TELA_INICIAL) {
      telaInical(con);
      receiv = recv(con->sid, buf, MAXDATASIZE, 0);
      proximoEstado(con, atoi(buf));
    }
    else if(con->estado == SELECIONA_JOGO) {
      selecionaJogo(con);
      receiv = recv(con->sid, buf, MAXDATASIZE, 0);
      proximoEstado(con, atoi(buf));
    }
    else if(con->estado == JOGAR_2) {  
      iniciaJogo2(con);
      receiv = recv(con->sid, buf, MAXDATASIZE, 0);
      proximoEstado(con, atoi(buf));
    }
    /*else if(con->estado == JOGAR_4) {  
      iniciaJogo2(con);
      receiv = recv(con->sid, buf, MAXDATASIZE, 0);
      proximoEstado(con, atoi(buf));
    }
    else if(con->estado == JOGAR_8) {  
      iniciaJogo2(con);
      receiv = recv(con->sid, buf, MAXDATASIZE, 0);
      proximoEstado(con, atoi(buf));
    }*/
    else if(con->estado == SAIR) {
      sprintf(buf, "%d", SAIR);
      send(con->sid, buf, strlen(buf)+1, 0);
      break;
    }
  }
  
  return 0;
}

int proximoEstado(Conexao *con, int proxEstado) {
  if(con->estado == TELA_INICIAL) {
    if(proxEstado == 1)
      con->estado = SELECIONA_JOGO;
    else if(proxEstado == 2)
      con->estado = RANK;
    else if(proxEstado == 3)
      con->estado = SAIR;    
  }
  else if(con->estado == SELECIONA_JOGO) {
    if(proxEstado == 1)
      con->estado = JOGAR_2;
    else if(proxEstado == 2)
      con->estado = JOGAR_4;
    else if(proxEstado == 3)
      con->estado = JOGAR_8;          
  }
  else if(con->estado == JOGAR_2) {
    if(proxEstado == 1)
      con->estado = SELECIONA_JOGO;
  }
  
  return 0;
}

/*****************************
************ JOGO ************
******************************/
int iniciaJogo2(Conexao *con) {
  char buf[MAXDATASIZE];
  int i, numbytes, pontos, rc;
  int numJogadores = 2;
  int num_perg = 5;
  SalaJogo *aux;
  
  sprintf(buf, "%d", JOGAR_2);
  numbytes = send(con->sid, buf, strlen(buf)+1, 0);
  pthread_mutex_lock(&mutexsum);
  con->ctJogo = 1;
  pthread_mutex_unlock(&mutexsum);
  if(verificaSalaJogo(con, numJogadores) == -1) {
    criaSalaJogo(con, numJogadores);
    aux = getUltimaSalaJogo();
    rc = pthread_create(&aux->tsala, NULL, gerenciaSalaJogo, (void *)aux);
  }

  while(con->ctJogo == 1) sleep(1);
  return 0;
}

void *gerenciaSalaJogo(void *tsalaJogo) {
  SalaJogo *salaJogo = (SalaJogo *)tsalaJogo;
  Pergunta *perguntas = selecionaPerguntas();
  char buf[MAXDATASIZE];
  
  aguardaJogadores(salaJogo);
  enviarPerguntas(salaJogo, perguntas);

  return 0;
}

int enviarPerguntas(SalaJogo *salaJogo, Pergunta *perguntas) {
  salaJogo->perg = perguntas;
  Pergunta *p = salaJogo->perg;
  char buf[MAXDATASIZE];
  pthread_t con1, con2;
  int receiv, rc;
  int i = 0;
  receiv = verificaRecebimento(salaJogo); //Msg de controle para estabelecer sincronização entre as conexões na Sala

  salaJogo->receiv = 0;
  salaJogo->jogoFim = 0;
  salaJogo->con1->pontos = 0;
  salaJogo->con2->pontos = 0;
  rc = pthread_create(&con1, NULL, controlaJogo1, (void *)salaJogo);
  rc = pthread_create(&con2, NULL, controlaJogo2, (void *)salaJogo);
  
  while(p != NULL && i < 1) {
    //Envie o enunciado da pergunta
    sprintf(buf, "\n%s\n\n", p->pergunta);
    enviaMsgSalaJogo(salaJogo, buf);
  
    //receiv = verificaRecebimento(salaJogo);
    enviaAlternativas(salaJogo, p);
    
    //Recebe resposta
    //receiv = verificaRecebimento(salaJogo, buf);
    //sockReceiv = recv(salaJogo->con1->sid, buf, MAXDATASIZE, 0);
    sleep(1);
    pthread_mutex_lock(&mutexsum);
    salaJogo->receiv = 1;
    salaJogo->sidResp = 0;
    salaJogo->con1Resp = 0;
    salaJogo->con2Resp = 0;
    pthread_mutex_unlock(&mutexsum);
    while(salaJogo->sidResp == 0) {
      if(salaJogo->con1Resp == 1 && salaJogo->con2Resp == 1) 
	break;
      sleep(1);
    }
    pthread_mutex_lock(&mutexsum);
    if(salaJogo->sidResp != 0) adicinaPonto(salaJogo);
    if(salaJogo->con1Resp == 1 && salaJogo->sidResp != salaJogo->con1->sid) removePonto(salaJogo->con1);
    if(salaJogo->con2Resp == 1 && salaJogo->sidResp != salaJogo->con2->sid) removePonto(salaJogo->con2);
    salaJogo->receiv = 0;
    salaJogo->sidResp = 0;
    pthread_mutex_unlock(&mutexsum);
    p = p->prox;
    salaJogo->perg = p;
    i++;
  }
  pthread_mutex_lock(&mutexsum);
  salaJogo->jogoFim = 1;
  pthread_mutex_unlock(&mutexsum);
  enviaMsgVencedor(salaJogo);
  
  sprintf(buf, "Você, socket %d marcou %d pontos\n\n", salaJogo->con1->sid, salaJogo->con1->pontos);
  send(salaJogo->con1->sid, buf, strlen(buf)+1, 0);
  sprintf(buf, "Você, socket %d marcou %d pontos\n\n", salaJogo->con2->sid, salaJogo->con2->pontos);
  send(salaJogo->con2->sid, buf, strlen(buf)+1, 0);
  sleep(1);
  enviaMsgSalaJogo(salaJogo, "eog"); //End of Game

  sprintf(buf, "%d", TELA_INICIAL);
  enviaMsgSalaJogo(salaJogo, buf);

  pthread_mutex_lock(&mutexsum);
  salaJogo->con1->ctJogo = 0;
  salaJogo->con2->ctJogo = 0;
  pthread_mutex_unlock(&mutexsum);
  return 0;
}

int removePonto(Conexao *con) {
  con->pontos -= 9;
  return 0;
}

int enviaMsgVencedor(SalaJogo *salaJogo) {
  char buf[MAXDATASIZE];
  int venc;
  
  venc = salaJogo->con1->pontos;
  if(venc == salaJogo->con2->pontos) {
    sprintf(buf, "\nO socket %d e %d empatou com %d pontos!!\n", salaJogo->con1->sid, salaJogo->con2->sid, salaJogo->con2->pontos);
    enviaMsgSalaJogo(salaJogo, buf);
  } else if(venc < salaJogo->con2->pontos) {
    sprintf(buf, "\nO socket %d venceu a partida com %d pontos!!\n", salaJogo->con2->sid, salaJogo->con2->pontos);
    enviaMsgSalaJogo(salaJogo, buf);
  } else {
    sprintf(buf, "\nO socket %d venceu a partida com %d pontos!!\n", salaJogo->con1->sid, salaJogo->con1->pontos);
    enviaMsgSalaJogo(salaJogo, buf);
  }

  return 0;
}

void *controlaJogo1(void *tsalaJogo) {  
  SalaJogo *salaJogo = (SalaJogo *)tsalaJogo;
  char buf[MAXDATASIZE];

  while(!salaJogo->jogoFim) {
    if(salaJogo->receiv) {
      if(!salaJogo->con1Resp) {
	//Recebe a resposta da Pergunta
	recv(salaJogo->con1->sid, buf, MAXDATASIZE, 0);

	if(strcmp(buf, "") != 0) {
	  if(salaJogo->sidResp == 0 && verificaResposta(salaJogo->perg, tolower(buf[0]))) {
	    pthread_mutex_lock(&mutexsum);
	    salaJogo->sidResp = 1;
	    pthread_mutex_unlock(&mutexsum);
	    sprintf(buf, "\nResposta [%c] Correta!\n", buf[0]);
	  } 
	  else sprintf(buf, "\nResposta [%c] Errada!\n", buf[0]);
	  send(salaJogo->con1->sid, buf, strlen(buf)+1, 0);
	}
	pthread_mutex_lock(&mutexsum);
	salaJogo->con1Resp = 1;
	pthread_mutex_unlock(&mutexsum);
      }
    }
    sleep(1);
  }
  pthread_exit(NULL);
}

void *controlaJogo2(void *tsalaJogo) {  
  SalaJogo *salaJogo = (SalaJogo *)tsalaJogo;
  char buf[MAXDATASIZE];

  while(!salaJogo->jogoFim) {
    if(salaJogo->receiv) {
      if(!salaJogo->con2Resp) {
	//Recebe a resposta da Pergunta
	recv(salaJogo->con2->sid, buf, MAXDATASIZE, 0);
	
	if(!salaJogo->con2Resp && strcmp(buf, "") != 0) {
	  if(salaJogo->sidResp == 0 && verificaResposta(salaJogo->perg, tolower(buf[0]))) {
	    pthread_mutex_lock(&mutexsum);
	    salaJogo->sidResp = 2;
	    pthread_mutex_unlock(&mutexsum);
	    sprintf(buf, "\nResposta [%c] Correta!\n", buf[0]);
	  } 
	  else sprintf(buf, "\nResposta [%c] Errada!\n", buf[0]);
	  send(salaJogo->con2->sid, buf, strlen(buf)+1, 0);
	}
	pthread_mutex_lock(&mutexsum);
	salaJogo->con2Resp = 1;
	pthread_mutex_unlock(&mutexsum);
      }
    }
    sleep(1);
  }
  pthread_exit(NULL);
}

int adicinaPonto(SalaJogo *salaJogo) {
  if(salaJogo->sidResp == 1)
    salaJogo->con1->pontos += 17;
  else if(salaJogo->sidResp == 2)
    salaJogo->con2->pontos += 17;

  return 0;
}

int verificaRecebimento(SalaJogo *salaJogo) {
  char buf[MAXDATASIZE];
  int i = 0;
  
  while(1) {
    if(recv(salaJogo->con1->sid, buf, MAXDATASIZE, 0) != -1)
      i++;
    if(recv(salaJogo->con2->sid, buf, MAXDATASIZE, 0) != -1)
      i++;
    if(i == 2)
      break;
      
    sleep(1);
  }
  
  return 0;
}

int enviaAlternativas(SalaJogo *salaJogo, Pergunta *p) {
  int i = 'A';
  char buf[MAXDATASIZE];
  Alternativa *aux = p->alt;

  //Envia a alternativa da pergunta
  sprintf(buf, "A) %s\nB) %s\nC) %s\nD) %s\n", aux->alternativa, aux->prox->alternativa, aux->prox->prox->alternativa, aux->prox->prox->prox->alternativa);
  enviaMsgSalaJogo(salaJogo, buf);

  return 0;
}

Pergunta *selecionaPerguntas() {
  Pergunta *p = listaPergunta;
  return p;
}

int aguardaJogadores(SalaJogo *salaJogo) {
  char buf[MAXDATASIZE];
  int i, j;

  j = 0;
  while(1) {
    i = numJogadoresSalaJogo(salaJogo);
    sprintf(buf, "Aguardando jogadores! [%d/2]\n", i);
    
    if(j != i) {
      enviaMsgSalaJogo(salaJogo, buf);
      j = i;
    }

    if(i == 2)
      break;
  }
  strcpy(buf, NP);
  enviaMsgSalaJogo(salaJogo, buf);

  return 0;
}

int enviaMsgSalaJogo(SalaJogo *salaJogo, char msg[MAXDATASIZE]) {
  pthread_mutex_lock(&mutexsum);
  if(salaJogo->con1 != NULL)
    send(salaJogo->con1->sid, msg, strlen(msg)+1, 0);
  if(salaJogo->con2 != NULL)
    send(salaJogo->con2->sid, msg, strlen(msg)+1, 0);
  pthread_mutex_unlock(&mutexsum);
  sleep(1);
  
  return 0;
}

int numJogadoresSalaJogo(SalaJogo *salaJogo) {
  int i = 0;
  pthread_mutex_lock(&mutexsum);
  if(salaJogo->con1 != NULL)
    i++;
  if(salaJogo->con2 != NULL)
    i++;
  pthread_mutex_unlock(&mutexsum);
  return i;
}

int verificaSalaJogo(Conexao *con, int numJogadores) {
  SalaJogo *salaAux = salaJogo2;
  int i;

  if(numJogadores == 2) {
    while(salaAux != NULL) {
      if(salaAux->con2 == NULL) {
	salaAux->con2 = con;
	return 0;
      }
      salaAux = salaAux->prox;
    }
  }

  return -1;
}

int criaSalaJogo(Conexao *con, int numJogadores) {
  pthread_mutex_lock(&mutexsum);
  if(salaJogo2 == NULL) {
    salaJogo2 = insereSalaJogoInicio(con);
  } else {
    insereSalaJogoFinal(salaJogo2, con);
  }
  pthread_mutex_unlock(&mutexsum);
  return 0;
}

SalaJogo *getUltimaSalaJogo() {
  SalaJogo *aux = salaJogo2;
  while(aux->prox != NULL) aux = aux->prox;
  return aux;
}

//Função que insere no final da lista ligada das Perguntas, uma pergunta
int insereSalaJogoFinal(SalaJogo *ini, Conexao *con) {
  SalaJogo *aux = ini;
  while(aux->prox != NULL) aux = aux->prox;
  SalaJogo *novo = novaSalaJogo(con);
  aux->prox = novo;
  
  return 0;
}

//Função que insere no início (cabeça) da lista ligada Perguntas
//Retorna um ponteiro para a cabeça
SalaJogo *insereSalaJogoInicio(Conexao *con) {
  SalaJogo *novo = novaSalaJogo(con);
  return novo;
}

//Função responsável por criar um "Nó" para ser colocado na lista ligada das Perguntas
//Retorna um ponteiro para o Nó criado
SalaJogo *novaSalaJogo(Conexao *con) {
  SalaJogo *novo = malloc(sizeof(SalaJogo));
  novo->con1 = con;
  novo->con2 = NULL;
  novo->prox = NULL;
  
  return novo;
}

/*****************************
************ TELA ************
******************************/
int telaInical(Conexao *con) {
  char buf[MAXDATASIZE];
  int numbytes;
  
  sprintf(buf, "%d", TELA_INICIAL);
  numbytes = send(con->sid, buf, strlen(buf)+1, 0);

  return 0;
}

int selecionaJogo(Conexao *con) {
  char buf[MAXDATASIZE];
  int numbytes;
  
  sprintf(buf, "%d", SELECIONA_JOGO);
  numbytes = send(con->sid, buf, strlen(buf)+1, 0);

  return 0;  
}

int exibeRank(int sockfd) {
    char buf[MAXDATASIZE];
    int retval, row, i;
    sqlite3 *handle;
    sqlite3_stmt *stmt;
    
    retval = sqlite3_open("kizz.sqlite", &handle);
    
    retval = sqlite3_prepare_v2(handle, "SELECT r.nome, r.pontos FROM Rank r ORDER BY r.pontos;", -1, &stmt, 0);
    if(retval) {
      perror("SELECT database");
      return 0;
    }
    
    i = 0;
    row = 0;
    while(1) {
      retval = sqlite3_step(stmt);
      
      if(retval == SQLITE_ROW) {
	//Processo encarregado de guardar na lista ligada determinada Pergunta e suas Alternativas com a quantidade constante definida por NUM_ALTERNATIVAS 
	sprintf(buf, "%s\t%s", sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1));
	send(sockfd, buf, strlen(buf)+1, 0);
	
	recv(sockfd, buf, MAXDATASIZE, 0); //Confirmação/Espera de recebimento (No Problem)
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
    
    strcpy(buf, "eor");
    send(sockfd, buf, strlen(buf)+1, 0);
    
    recv(sockfd, buf, MAXDATASIZE, 0); //Confirmação/Espera de recebimento (No Problem)

    strcpy(buf, "Nome\tPontos");
    send(sockfd, buf, strlen(buf)+1, 0);
  
  recv(sockfd, buf, MAXDATASIZE, 0); //Confirmação/Espera de recebimento (No Problem)

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

int verificaResposta(Pergunta *p, int n) {
  Alternativa *aux = p->alt;
  int i;

  n = n - 'a';
  for(i=0; i<n && aux->prox != NULL; i++) {
    aux = aux->prox;
  }
  
  return aux->correta;
}

Pergunta *sorteiaPergunta(Pergunta *ini) {
  int i, n;
  Pergunta *aux = ini->prox;
  
  n = randomN(listaSize(ini));
  for(i=0; i<n-1; i++) aux = aux->prox;
  
  return aux;
}

//Gera um número aleatório de 0 até N
//Autor: Guilherme G Moreira
int randomN(int n) {
  int r = random()/(RAND_MAX / n);
  return r;
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

int charToint(char c) {
  return c - 'a';
}


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

//Função responsável por pegar o último Nó de uma lista ligada das Perguntas
//Retorna um ponteiro para o último nó
Pergunta *getUltimaPergunta(Pergunta *ini) {
  Pergunta *aux = ini;
  while(aux->prox != NULL) aux = aux->prox;
  return aux;
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