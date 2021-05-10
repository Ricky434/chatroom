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
#define MAX_USERS 3
#define MAX_MSG_LEN 1024
#define MAX_UNAME_LEN 25

//informazioni da passare al thread che gestisce il client
typedef struct {
    int in_use;
    int *users; //utenti attualmente collegati
    int clifd; //file descriptor del client
    FILE *logfile; //file di log
} clinfo;

struct msgargs {
    clinfo *clients;
    FILE *logfile;
};

//====THREADS====

static void *parla_con_client(void *clientinfo)
{
    //FORSE 1KB E' TROPPO?
    char sendBuff[MAX_MSG_LEN]; //buffer messaggio da inviare
    char recvBuff[MAX_MSG_LEN]; //buffer messaggio da ricevere
    int nRead; //numero di byte letti
    time_t ticks; //timestamp
    char welcome[] = "Welcome!\nInsert username:";
    char username[MAX_UNAME_LEN]; //username di massimo 24 caratteri 

    //salvo le informazioni
    clinfo *info = clientinfo;

    //"pulisco" le locazioni di memoria dei buffer dei messaggi
    memset(sendBuff, 0, sizeof(sendBuff));
    memset(recvBuff, 0, sizeof(recvBuff));

    //chiedi nome utente
    write(info->clifd, welcome, strlen(welcome));
    read(info->clifd, username, sizeof(username));
    username[strlen(username)-1] = 0; //imposto l'ultimo carattere a 0 per eliminare lo \n

    //comincio il ciclo in cui ogni volta che il client manda un messaggio, io lo leggo e lo rimando indietro con attaccato il timestamp. Quando ricevo un messaggio lungo solo un byte (per es. uno \n, cioe' quando il client preme invio senza scrivere niente) interrrompo il ciclo
    while ((nRead = read(info->clifd, recvBuff, sizeof(recvBuff))) > 1) {
        //salvo il timestamp
        ticks = time(NULL);

        //scrivo il timestamp nel messaggio da inviare
        snprintf(sendBuff, sizeof(sendBuff), "%.24s [%s]: ", ctime(&ticks), username);

        //attacco il messaggio ricevuto dopo il timestamp, stando attento a non andare oltre alla fine del buffer e lasciando un byte per aggiungere un carattere finale
        strncat(sendBuff, recvBuff, sizeof(sendBuff)-strlen(sendBuff));

        //salvo il messaggio nel file di log
        fputs(sendBuff, info->logfile); //PROVARE A UsARE PIPE O FIFO CON gestisci_messaggi, che poi dovra salvare nel file di log
        fflush(info->logfile);

        //DA FAR DIVENTARE LOG: scrivo il messaggio anche su stdout perche' posso
        write(1, sendBuff, strlen(sendBuff)-1);

        //pulisco i buffer per inviare e ricevere il messaggio
        memset(sendBuff, 0, sizeof(sendBuff));
        memset(recvBuff, 0, sizeof(recvBuff));
    }
    
    //una volta finito di parlare con il client chiudo la connessione
    close(info->clifd);
    *(info->users) -= 1;
    info->in_use = 0;
    return 0;
    //exit?
}

static void *gestisci_messaggi(void *clients_log)
{
    char sendBuff[MAX_MSG_LEN];

    struct msgargs *args = clients_log;

    memset(sendBuff, 0, sizeof(sendBuff));

    while (1) {
        read(fileno(args->logfile), sendBuff, sizeof(sendBuff)); //NON FUNZIONA, provare a usare pipes o fifo

        for (int i = 0; i < MAX_USERS; i++) {
            if (args->clients[i].in_use) {
                write(args->clients[i].clifd, sendBuff, sizeof(sendBuff));
            }
        }
    }
}

//====FUNZIONI====

//funzione per lanciare un errore
void error(char *msg)
{
    perror(msg);
    exit(1);
}

void chiudi()
{
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

//====MAIN====

int main(int argc, char *argv[])
{
    //creo le variabili dei file descriptors, del numero di utenti collegati, delle informazioni del socket del server e del client, della lunghezza del socket client e delle informazioni da passare al thread che gestisce il client
    int listenfd, tempfd, clino, users = 0;
    struct sockaddr_in serv_addr;
    clinfo client_info[MAX_USERS];
    char message[] = "Il server e' pieno, riprovare piu' tardi"; //messaggio da usare in caso il server sia pieno
    pthread_t clithread_id[MAX_USERS], msgthread_id;
    FILE *logfile;
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

    //apro il file di log
    logfile = fopen("log.txt", "w+");
    if (logfile == NULL)
        error("ERRORE apertura file");

    //creo thread che si occupa della gestione dei messaggi
    msgthread_args.clients = client_info;
    msgthread_args.logfile = logfile;
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
        client_info[clino].logfile = logfile;
        client_info[clino].in_use = 1;

        //creo un thread per gestire il client appena accettato e aumento il contatore degli utenti
        pthread_create(&clithread_id[clino], 0, &parla_con_client, &client_info[clino]);
        users++;
        sleep(1);

        //thread join dove?
    }
}