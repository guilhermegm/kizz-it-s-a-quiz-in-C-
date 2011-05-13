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

#define PORT "3490"  // the port users will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold

#define NUM_ALTERNATIVAS 4 // Numero de questoes por pergunta
#define MAXDATASIZE 200
#define NP "200 OK"

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
int trataConexao(int sockfd, Pergunta *listaPergunta);
int iniciaJogo(int sockfd, Pergunta *listaPergunta);
int verificaResposta(Pergunta *p, int c);
int enviaAlternativas(int sockfd, Pergunta *p);
Pergunta *sorteiaPergunta(Pergunta *ini);
int listaSize(Pergunta  *ini);
int telaInical(int sockfd);
int charToint(char c);

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
    Pergunta *listaPergunta;  
  
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
    /*Pergunta *aux;
    Alternativa *altx;
    for(aux=listaPergunta; aux!=NULL; aux=aux->prox) {
      printf("%s\n", aux->pergunta);
      for(altx=aux->alt; altx!=NULL; altx=altx->prox) {
	printf("%c.[%d] %s\n", altx->id + 'a', altx->correta, altx->alternativa);
      }
    }*/
    
   /* printf("server: waiting for connections...\n");
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            exit(1);
        }
        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);
            close(sockfd); // child doesn't need the listener
	    trataConexao(new_fd, listaPergunta);
	    close(new_fd);*/
   
   
   printf("server: waiting for connections...\n");
    while(1) {  // main accept() loop
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
        if (!fork()) { // this is the child process
            close(sockfd); // child doesn't need the listener
            if (trataConexao(new_fd, listaPergunta) == -1)
                perror("send");
            printf("server: lost connection from %s\n", s);
            close(new_fd);
            exit(0);
        }
        close(new_fd);  // parent doesn't need this
    }
    return 0;
}

// 0 - Tela Inicial // 1 - Jogar // 2 - Rank // 3 - Sair
int trataConexao(int sockfd, Pergunta *listaPergunta) {
  int ms, sair;

  sair = 0;
  ms = 0;
  while(!sair){
    if(ms == 0)
      ms = telaInical(sockfd);
    else if(ms == 1)
      ms = iniciaJogo(sockfd, listaPergunta);
    else if(ms == 2)
      ms = exibeRank(sockfd);
    else if(ms == 3)
      sair = 1;
  }
  return 1;
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

int iniciaJogo(int sockfd, Pergunta *listaPergunta) {
  char buf[MAXDATASIZE];
  int i, numbytes, pontos;
  int num_perg = 5;
  char str[10];
  //Envia o número de perguntas
  //strcpy(buf, "3");
  sprintf(buf, "%i\0", num_perg); 
  send(sockfd, buf, strlen(buf)+1, 0);
  
  recv(sockfd, buf, MAXDATASIZE, 0); //Confirmação/Espera de recebimento
  pontos = 0;
  Pergunta *p = listaPergunta;
  for(i=0; i<num_perg; i++) {
    Pergunta *p = sorteiaPergunta(listaPergunta);

    //Envie o enunciado da pergunta
    strcpy(buf, p->pergunta);
    send(sockfd, buf, strlen(buf)+1, 0);

    recv(sockfd, buf, MAXDATASIZE, 0); //Confirmação/Espera de recebimento (No Problem)

    enviaAlternativas(sockfd, p);
    
    recv(sockfd, buf, MAXDATASIZE, 0);
    if(verificaResposta(p, atoi(buf)-1)) {
      strcpy(buf, "Resposta Correta!");
      pontos += 19;
    } else {
      strcpy(buf, "Resposta Errada!");
      pontos -= 17;
    }
    send(sockfd, buf, strlen(buf)+1, 0);
    
    //p = p->prox;
  }
  
  recv(sockfd, buf, MAXDATASIZE, 0); //Confirmação/Espera de recebimento (No Problem)

  //Envie o enunciado da pergunta
  sprintf(buf, "Você fez %d pontos.", pontos);
  send(sockfd, buf, strlen(buf)+1, 0);
  
  //Recebe o nome para guardar no banco de dados (Rank)
  recv(sockfd, buf, MAXDATASIZE, 0);
  
  adicionaRank(buf, pontos);
  
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

int enviaAlternativas(int sockfd, Pergunta *p) {
  char buf[MAXDATASIZE];
  Alternativa *aux = p->alt;

  while(aux != NULL) {
    //Envia a alternativa da pergunta
    strcpy(buf, aux->alternativa);
    
    send(sockfd, buf, strlen(buf)+1, 0);
    
    recv(sockfd, buf, MAXDATASIZE, 0); //Confirmação/Espera de recebimento (No Problem)
    
    aux = aux->prox;
  }
  
  //End of Alternativas
  strcpy(buf, "eoa");
  send(sockfd, buf, strlen(buf)+1, 0);
  
  recv(sockfd, buf, MAXDATASIZE, 0); //Confirmação/Espera de recebimento (No Problem)
  
  return 1;
}

int telaInical(int sockfd) {
  char buf[MAXDATASIZE];
  int numbytes;
  
  strcpy(buf, "KUIZZ\n\n1) Jogar\n2) Rank\n3) Sair");
  
  send(sockfd, buf, strlen(buf)+1, 0);
  recv(sockfd, buf, MAXDATASIZE, 0);

  return atoi(buf);
}

int verificaResposta(Pergunta *p, int n) {
  Alternativa *aux = p->alt;
  int i;
  
  for(i=0; i<n; i++)
    aux = aux->prox;
  
  return aux->correta;
}

Pergunta *sorteiaPergunta(Pergunta *ini) {
  int i, n;
  Pergunta *aux = ini->prox;
  
  n = randomN(listaSize(ini));
  for(i=0; i<n-1; i++) aux = aux->prox;
  
  return aux;
}

//Gera um número aleatório de 1 até N
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