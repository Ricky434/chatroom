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
#define MAX_USERS 2
#define MAX_MSG_LEN 1024
#define MAX_UNAME_LEN 25
#define EXIT_CMD "!exit"

//informazioni da passare al thread che gestisce il client
typedef struct {
    int in_use;
    int *users; //utenti attualmente collegati --probabilmente inutile
    int clifd; //file descriptor del client
    int *pipefd; //file descriptor della pipe
} clinfo;

typedef struct {
    time_t timestamp; //timestamp di invio del messaggio
    char msg[MAX_MSG_LEN]; //messaggio
    char usr[MAX_UNAME_LEN]; //mittente del messaggio
    int isannounce; //se = 1 il messaggio e' un annuncio
} message_s;

struct msgargs {
    clinfo *clients;
    int *pipefd;
};

//====FUNZIONI====

//OLTRE A OTTIMIZZAZIONI (users e nRead per esempio) E COMMENTI DOVREBBE ESSERE FATTO IL LATO SERVER
//FARE MAKEFILE
//mutex per clientinfo?
//BUG VARI
//suddividere in funzioni?
//cerca dove possibile di sostituire tutti i vari strcpy, ecc con snprintf
//liberare tutti i puntatori, anche quelli senza malloc?
//forse non servono tutti tutti i memset...

//funzione per lanciare un errore
void error(char *msg)
{
    perror(msg);
    exit(1);
}

void chiudi() //FARGLI FARE QUALCOSA DI UTILE, MAGARI NON USANDO SIGINT MA TROVANDO UN MODO "GIUSTO" PER SPEGNERE IL SERVER
{
    printf("\rShutting down...\n"); //NON FUNZIONA
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

void ordina(message_s *listamsg[], int nmsg)
{
    int m;
    message_s *temp;

    temp = malloc(sizeof(message_s));
    memset(temp, 0, sizeof(message_s));
    
    for (int i = 0; i < nmsg; i++) { //selection sort, inefficiente come poche cose, per come l'ho implementato
        m = i;
        for (int j = i+1; j < nmsg; j++) {
            //invece di fare cosi forse e' meglio aggiungere un timestamp anche al messaggio di announce
            if (listamsg[j]->timestamp < listamsg[m]->timestamp) //il timestamp e' lungo 10 caratteri
                m = j;
        }
        
        memcpy(temp, listamsg[i], sizeof(message_s));
        memcpy(listamsg[i], listamsg[m], sizeof(message_s));
        memcpy(listamsg[m], temp, sizeof(message_s));
    }
    free(temp);
}

//riempie il message_s DEST con il MESSAGE, il SENDER e IS_ANNOUNCE, e ricava il timestamp da MESSAGE
//conviene implementarlo qui o nel client?
void gen_msg(message_s *dest, char *message, char *sender, int is_announce)
{
    char *sepPtr; //puntatore al carattere che separa il timestamp dal messaggio effettivo

    memset(dest, 0, sizeof(*dest));

    strcpy(dest->usr, sender);
    dest->isannounce = is_announce;
    dest->timestamp = atol(message);

    sepPtr = strchr(message, ':'); //trovo il separatore tra il timestamp e il messaggio
    strcpy(dest->msg, sepPtr+1);
}

//====THREADS====

static void *parla_con_client(void *clientinfo)
{
    //FORSE 1KB E' TROPPO?
    char sendBuff[MAX_MSG_LEN]; //buffer messaggio da inviare
    char recvBuff[MAX_MSG_LEN]; //buffer messaggio da ricevere
    int nRead; //numero di byte letti
    char welcome[] = "Welcome, please insert username\n";
    char goodbye[] = " has left the chat\n";
    char announce[] = " has joined the chat!\n";
    char username[MAX_UNAME_LEN]; //username di massimo 24 caratteri
    message_s messaggio;

    //salvo le informazioni
    clinfo *info = clientinfo;

    //"pulisco" le locazioni di memoria dei buffer dei messaggi
    memset(sendBuff, 0, MAX_MSG_LEN);
    memset(recvBuff, 0, MAX_MSG_LEN);
    memset(username, 0, MAX_UNAME_LEN);

    //chiedi username
    write(info->clifd, welcome, strlen(welcome));
    nRead = read(info->clifd, recvBuff, MAX_MSG_LEN);
    recvBuff[nRead-1] = 0; //imposto l'ultimo carattere a 0 per eliminare lo \n

    //se il client ha inviato !exit come username, vuol dire che e' terminato, quindi faccio terminare anche il thread che se ne occupa
    if (strcmp(recvBuff, EXIT_CMD) == 0 || nRead == 0) { 
            *(info->users) -= 1;
            info->in_use = 0;
            return 0; //exit o return?
        }

    //imposto username
    strncpy(username, strchr(recvBuff, ':')+1, MAX_UNAME_LEN);

    //annuncia
    sprintf(sendBuff, "%s%s", recvBuff, announce); //questo o due strcat?
    gen_msg(&messaggio, sendBuff, username, 1);
    write(info->pipefd[1], &messaggio, sizeof(messaggio));

    //pulisci buffer
    memset(sendBuff, 0, MAX_MSG_LEN);
    memset(recvBuff, 0, MAX_MSG_LEN);

    //una volta inviato il messaggio di annuncio, per il quale mi serve il timestamp, sostituisco la variabile username con solo il nome effettivo


    //comincio il ciclo in cui ogni volta che il client manda un messaggio, io lo leggo e lo rimando indietro con attaccato il timestamp. Quando ricevo un messaggio lungo solo un byte (per es. uno \n, cioe' quando il client preme invio senza scrivere niente) interrrompo il ciclo (questa cosa verra' tolta, devo trovare un altro modo per chiudere la connessione)
    while (1) {
        if (read(info->clifd, recvBuff, sizeof(recvBuff)) == 0) {
            printf("ERROR reading from client %s\n", username); //necessario il print?
            break; //o pthread_exit()? -> no, devo settare il clientfd ecc a non in uso
        }

        if (strcmp(recvBuff, EXIT_CMD) == 0) { //forse !exit da mettere tra i #define
            break;
        }

        gen_msg(&messaggio, recvBuff, username, 0);

        //mando il messaggio al thread che se ne occupa tramite pipe
        write(info->pipefd[1], &messaggio, sizeof(messaggio));

        //pulisco i buffer per inviare e ricevere il messaggio
        memset(sendBuff, 0, sizeof(sendBuff));
        memset(recvBuff, 0, sizeof(recvBuff));
    }
    
    //una volta finito di parlare con il client chiudo la connessione
    close(info->clifd);

    sprintf(sendBuff, "%ld:%s%s", time(NULL), username, goodbye);
    *(info->users) -= 1;
    info->in_use = 0; //serve tmux

    gen_msg(&messaggio, sendBuff, username, 1);
    write(info->pipefd[1], &messaggio, sizeof(messaggio));
    return 0;
    //exit o return?
}

static void *gestisci_messaggi(void *clients_log)
{
    char sendBuff[MAX_MSG_LEN];
    message_s messaggio;
    FILE *logfile;
    struct msgargs *args = clients_log;
    struct tm *loctime;

    memset(sendBuff, 0, sizeof(sendBuff));

    //apro il file di log
    logfile = fopen("log.txt", "w+"); //forse lo posso far aprire dal thread che se ne occupa
    if (logfile == NULL)
        error("ERROR opening file");

    while (1) {
        read(args->pipefd[0], &messaggio, sizeof(messaggio));

        loctime = localtime(&(messaggio.timestamp));

        if (messaggio.isannounce) {
            strcpy(sendBuff, messaggio.msg);
        } else {
            snprintf(sendBuff, sizeof(sendBuff), "(%02d:%02d:%02d) [%s]: %s", loctime->tm_hour, loctime->tm_min, loctime->tm_sec, messaggio.usr, messaggio.msg);
        }

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

static void *gestisci_messaggi_ordinati(void *clients_log)
{
    message_s *msgBuff[10]; //lista di 10 pointers, che conterranno fino a 10 messaggi
    FILE *logfile;
    struct msgargs *args = clients_log;
    int msgcounter = 0, select_ris;
    char sendBuff[MAX_MSG_LEN];
    fd_set fdset; //set di file descriptors da ascoltare
    struct timeval timeout;
    struct tm *loctime;


    memset(msgBuff, 0, sizeof(msgBuff));
    memset(sendBuff, 0, sizeof(sendBuff));

    for (int i = 0; i < 10; i++) {
        msgBuff[i] = malloc(sizeof(message_s)); //COME FACCIO IL FREE? -> forse posso mandare un messaggio a questo thread sulla pipe quando sto per chiudere il server, e se gli arriva questo messaggio esce dal while
    }

    //apro il file di log
    logfile = fopen("log.txt", "w+"); //forse lo posso far aprire dal thread che se ne occupa
    if (logfile == NULL)
        error("ERROR opening file");

    while (1) {
        timeout.tv_sec = 2; //2 secondi
        timeout.tv_usec = 0; // 0 microsecondi

        //NON FUNZIONA
        while (msgcounter < 10) { //per due secondi o finche' il buffer non e' pieno raccoglie tutti i messaggi che riceve
            FD_ZERO(&fdset);
            FD_SET(args->pipefd[0], &fdset);
            select_ris = select((args->pipefd[0])+1, &fdset, NULL, NULL, &timeout);

            if (select_ris == -1)
                error("ERROR reading from pipe");

            if (select_ris == 0) {
                break;
            }
            else {
                read(args->pipefd[0], msgBuff[msgcounter], sizeof(message_s));
                msgcounter++;
                //non so se funziona, in pratica ricalcolo il tempo da aspettare la prossima volta
                //non dovrebbe servire perche select mette dentro timeout il tempo non usato dal wait
                //timeout.tv_sec = (2 - timediff);
                //timeout.tv_usec = (2 - timediff) - timeout.tv_sec;
            }
        }

        if (msgcounter == 0)
            continue;

        ordina(msgBuff, msgcounter);

        //forse la parte dentro a questo ciclo si puo' spostare in una funzione che puo usare anche l'altro thread
        for (int i = 0; i < msgcounter; i++) {

            loctime = localtime(&(msgBuff[i]->timestamp));

            if (msgBuff[i]->isannounce) {
                strcpy(sendBuff, msgBuff[i]->msg);
            } else {
                snprintf(sendBuff, sizeof(sendBuff), "(%02d:%02d:%02d) [%s]: %s", loctime->tm_hour, loctime->tm_min, loctime->tm_sec, msgBuff[i]->usr, msgBuff[i]->msg);
            }
            
            fputs(sendBuff, logfile);
            fflush(logfile);


            for (int j = 0; j < MAX_USERS; j++) {
                if (args->clients[j].in_use) {
                    write(args->clients[j].clifd, sendBuff, strlen(sendBuff));
                }
            }
            memset(sendBuff, 0, sizeof(sendBuff));
        }

        for (int i = 0; i < msgcounter; i++) {
            memset(msgBuff[i], 0, sizeof(message_s)); //pulisco tutte le locazioni di memoria usate
        }
        msgcounter = 0;
    }
}

//====MAIN====

int main(int argc, char *argv[])
{
    //creo le variabili dei file descriptors, del numero di utenti collegati, delle informazioni del socket del server e del client, della lunghezza del socket client e delle informazioni da passare al thread che gestisce il client
    int listenfd, tempfd, clino, users = 0;
    struct sockaddr_in serv_addr;
    clinfo client_info[MAX_USERS];
    char error_full[] = "Il server e' pieno, riprovare piu' tardi\n"; //messaggio da usare in caso il server sia pieno
    pthread_t clithread_id[MAX_USERS], msgthread_id;
    int pipefd[2]; //file descriptor della pipe
    struct msgargs msgthread_args;

    //per sigaction ====
    struct sigaction act;
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGINT);

    act.sa_flags = 0;
    act.sa_mask = set;
    act.sa_handler = &chiudi;

    sigaction(SIGINT, &act, NULL);
    //fine sigaction ====

    //se non ci sono abbastanza argomenti 
    if(argc != 3) {
        //MAGARI TRASFORMARLO IN ERROR
        printf("\n Usage: %s <port> <nmetodo>\n Dove: nmetodo = 1 per messaggi ordinati in base al tempo di ricezione del server\n       nmetodo = 2 per messaggi ordinati in base al tempo di invio",argv[0]);
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
    if (atoi(argv[2]) == 1) {
        pthread_create(&msgthread_id, 0, &gestisci_messaggi, &msgthread_args);
    } else {
        pthread_create(&msgthread_id, 0, &gestisci_messaggi_ordinati, &msgthread_args);
    }

    //il server comincia ad accettare messaggi
    while(1) {
        //accetto la connessione in arrivo e apro un socket con file descriptor connfd per comunicare
        tempfd = accept(listenfd, (struct sockaddr*)NULL, NULL); //forse cli_addr e clilen sono inutili

        //se il server e' pieno manda un messaggio al client che sta provando a connettersi, chiudi il suo file descriptor e torna all'inizio del ciclo
        //SINCRONIZZAZIONE?
        if ((clino = get_client_number(client_info)) == -1) {
            write(tempfd, error_full, strlen(error_full));
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