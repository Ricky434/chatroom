#include <sys/socket.h> //probabilmente non serve perche incluso da in.h
#include <netinet/in.h>
//#include <arpa/inet.h>
#include <stdio.h> //IO
#include <string.h> //gestione stringhe
#include <unistd.h> //chiamate di sistema
#include <time.h> //time
#include <sys/types.h> //serve per il tipo del socket
#include <stdlib.h> //serve per gli errori (in questo caso)
#include <pthread.h> //threads
#include <signal.h> //signal handler


//utenti massimi ammessi
#define MAX_USERS 5
#define MAX_MSG_LEN 1024
#define MAX_UNAME_LEN 25

//informazioni da passare al thread che gestisce il client
typedef struct {
    int in_use;
    int *users; //utenti attualmente collegati --probabilmente inutile
    int clifd; //file descriptor del client
    int *pipefd; //file descriptor della pipe
} clinfo;

struct msgargs {
    clinfo *clients;
    int *pipefd;
};

//====FUNZIONI====

//MANCA ANCORA DA FARE L'ORDINAMENTO IN BASE AL TEMPO DI SPEDIZIONE (E QUINDI SPOSTARE IL TIMESTAMP LATO CLIENT) E POI OLTRE A OTTIMIZZAZIONI (users per esempio) E COMMENTI DOVREBBE ESSERE FATTO IL LATO SERVER
//C'E' UN BUG CHE QUANDO FACCIO SPENGO I CLIENT PRIMA DI INSERIRE L'USERNAME IL SERVER CRASHA -> sopprimere SIGPIPE?
//FARE MAKEFILE
//mutex per clientinfo?
//BUG VARI

//funzione per lanciare un errore
void error(char *msg)
{
    perror(msg);
    exit(1);
}

void chiudi() //FARGLI FARE QUALCOSA DI UTILE, MAGARI NON USANDO SIGINT MA TROVANDO UN MODO "GIUSTO" PER SPEGNERE IL SERVER
{
    printf("Shutting down..."); //NON FUNZIONA
    exit(0);
}

int get_client_number(clinfo client_info[])
{
    for (int i = 0; i < MAX_USERS; i++) {
        if (!client_info[i].in_use)
            return i;
    }
    return -1;
}

//====THREADS====

static void *parla_con_client(void *clientinfo)
{
    //FORSE 1KB E' TROPPO?
    char sendBuff[MAX_MSG_LEN]; //buffer messaggio da inviare
    char recvBuff[MAX_MSG_LEN]; //buffer messaggio da ricevere
    int nRead; //numero di byte letti
    time_t timer; //tempo dall'epoch ad ora
    struct tm *now; //struct contenente ore, minuti, secondi, ecc del timer
    char welcome[] = "Welcome, please insert username\n";
    char goodbye[] = " has left the chat\n";
    char announce[23+MAX_UNAME_LEN]; //per il messaggio di annuncio, 21 caratteri per " has joined the chat!" + username + 1 finale \0
    char username[MAX_UNAME_LEN]; //username di massimo 24 caratteri 

    //salvo le informazioni
    clinfo *info = clientinfo;

    //"pulisco" le locazioni di memoria dei buffer dei messaggi
    memset(sendBuff, 0, sizeof(sendBuff));
    memset(recvBuff, 0, sizeof(recvBuff));

    //chiedi nome utente
    while (strlen(username) == 0) {
        write(info->clifd, welcome, strlen(welcome));
        read(info->clifd, username, sizeof(username));
        username[strlen(username)-1] = 0; //imposto l'ultimo carattere a 0 per eliminare lo \n
    }

    //annuncia
    strcpy(announce, username);
    strcat(announce, " has joined the chat!\n");
    write(info->pipefd[1], announce, strlen(announce));

    //comincio il ciclo in cui ogni volta che il client manda un messaggio, io lo leggo e lo rimando indietro con attaccato il timestamp. Quando ricevo un messaggio lungo solo un byte (per es. uno \n, cioe' quando il client preme invio senza scrivere niente) interrrompo il ciclo (questa cosa verra' tolta, devo trovare un altro modo per chiudere la connessione)
    while (1) {
        read(info->clifd, recvBuff, sizeof(recvBuff));

        if (strcmp(recvBuff, "!exit") == 0) { //forse !exit da mettere tra i #define
            break;
        }

        //salvo il timestamp
        timer = time(NULL);
        now = localtime(&timer);

        //scrivo il timestamp nel messaggio da inviare (TEMPO DI RICEZIONE, FARE ANCHE TEMPO DI INVIO)
        snprintf(sendBuff, sizeof(sendBuff), "(%d:%d:%d) [%s]: ", now->tm_hour, now->tm_min, now->tm_sec, username);

        //attacco il messaggio ricevuto dopo il timestamp, stando attento a non andare oltre alla fine del buffer
        strncat(sendBuff, recvBuff, sizeof(sendBuff)-strlen(sendBuff));

        //mando il messaggio al thread che se ne occupa tramite pipe
        write(info->pipefd[1], sendBuff, strlen(sendBuff));

        //pulisco i buffer per inviare e ricevere il messaggio
        memset(sendBuff, 0, sizeof(sendBuff));
        memset(recvBuff, 0, sizeof(recvBuff));
    }
    
    //una volta finito di parlare con il client chiudo la connessione
    close(info->clifd);

    strcpy(sendBuff, username);
    strcat(sendBuff, goodbye);
    *(info->users) -= 1;
    info->in_use = 0;

    write(info->pipefd[1], sendBuff, strlen(sendBuff));
    return 0;
    //exit?
}

static void *gestisci_messaggi(void *clients_log)
{
    char sendBuff[MAX_MSG_LEN];
    FILE *logfile;
    struct msgargs *args = clients_log;

    memset(sendBuff, 0, sizeof(sendBuff));

    //apro il file di log
    logfile = fopen("log.txt", "w+"); //forse lo posso far aprire dal thread che se ne occupa
    if (logfile == NULL)
        error("ERROR opening file");

    while (1) {
        read(args->pipefd[0], sendBuff, sizeof(sendBuff));
        fputs(sendBuff, logfile);
        fflush(logfile);

        for (int i = 0; i < MAX_USERS; i++) {
            if (args->clients[i].in_use) {
                write(args->clients[i].clifd, sendBuff, strlen(sendBuff));
            }
        }
        memset(sendBuff, 0, sizeof(sendBuff));
    }
}

//====MAIN====

int main(int argc, char *argv[])
{
    //creo le variabili dei file descriptors, del numero di utenti collegati, delle informazioni del socket del server e del client, della lunghezza del socket client e delle informazioni da passare al thread che gestisce il client
    int listenfd, tempfd, clino, users = 0;
    struct sockaddr_in serv_addr;
    clinfo client_info[MAX_USERS];
    char message[] = "Il server e' pieno, riprovare piu' tardi"; //messaggio da usare in caso il server sia pieno
    pthread_t clithread_id[MAX_USERS], msgthread_id;
    int pipefd[2]; //file descriptor della pipe
    struct msgargs msgthread_args;

    //se non ci sono abbastanza argomenti 
    if(argc != 2) {
        //MAGARI TRASFORMARLO IN ERROR
        printf("\n Usage: %s <port>\n",argv[0]);
        return 1;
    }

    //creo il socket ascoltatore e mi salvo il suo file descriptor
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) 
        error("ERROR opening socket");

    //"pulisco" le locazioni di memoria
    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(&client_info, 0, sizeof(client_info));
    memset(&clithread_id, 0, sizeof(clithread_id));
    memset(&msgthread_args, 0, sizeof(msgthread_args));

    //imposto la famiglia, la porta e l'indirizzo (ANY in modo da accettare messaggi da qualunque indirizzo) al serv_addr
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[1]));
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    //associo le informazioni di serv_addr al socket
    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    //metto in ascolto il socket, permettendo una coda di 10 richieste
    listen(listenfd, 10);

    if (pipe(pipefd) == -1)
        error("ERROR creating pipe");

    //creo thread che si occupa della gestione dei messaggi
    msgthread_args.clients = client_info;
    msgthread_args.pipefd = pipefd;
    pthread_create(&msgthread_id, 0, &gestisci_messaggi, &msgthread_args);

    //il server comincia ad accettare messaggi
    while(1) {
        //accetto la connessione in arrivo e apro un socket con file descriptor connfd per comunicare
        tempfd = accept(listenfd, (struct sockaddr*)NULL, NULL); //forse cli_addr e clilen sono inutili

        //se il server e' pieno manda un messaggio al client che sta provando a connettersi, chiudi il suo file descriptor e torna all'inizio del ciclo
        //SINCRONIZZAZIONE?
        if ((clino = get_client_number(client_info)) == -1) {
            write(tempfd, message, strlen(message));
            close(tempfd);
            sleep(1);
            continue;
        }

        //preparo le informazioni da dare al nuovo thread
        client_info[clino].clifd = tempfd;
        client_info[clino].users = &users; //useless?
        client_info[clino].pipefd = pipefd;
        client_info[clino].in_use = 1;

        //creo un thread per gestire il client appena accettato e aumento il contatore degli utenti
        pthread_create(&clithread_id[clino], 0, &parla_con_client, &client_info[clino]);
        users++;
        sleep(1);

        //thread join dove?
    }
}