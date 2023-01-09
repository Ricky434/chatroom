/*
Il server accetta le richieste di connessione dei client, creando un thread per ogni client, fino ad un massimo di MAX_USERS.
Ogni messaggio ricevuto viene elaborato, estrapolandone il timestamp e successivamente viene rispedito a tutti i client.
Se il server e' stato lanciato con l'opzione di inviare i messaggi in base al tempo di invio, ogni 2 secondi i messaggi ricevuti vengono ordinati in base al loro timestamp e poi inviati. In questo caso quindi il server inviera' messaggi ai client solo ogni 2 secondi
*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

#define MAX_USERS 10 //numero massimo di clienti collegati contemporaneamente
#define MAX_MSG_LEN 512 // lunghezza massima del messaggio
#define MAX_UNAME_LEN 25 //lunghezza massima username (comprende carattere finale \0)
#define EXIT_CMD "!exit" //comando usato da un utente per disconnettersi dal server

//informazioni del client
typedef struct {
    int in_use; //se e' uguale a 1 vuol dire che le informazioni contenute in questo struct appartengono ad un client attualmente collegato al server 
    int *users; //numero degli utenti attualmente collegati
    int clifd; //file descriptor del client
    int *pipefd; //file descriptor della pipe
} clinfo;

//messaggio e le sue informazioni
typedef struct {
    time_t timestamp; //timestamp di invio del messaggio
    char msg[MAX_MSG_LEN-15-MAX_UNAME_LEN+1]; //messaggio [il messaggio verra' inviato ai client dal server nel formato <(hh:mm:ss) [username]: messaggio>, quindi 15+MAX_UNAME_LEN-1 caratteri (max) del messaggio vengono sprecati (il -1 e' perche' in MAX_UNAME_LEX e' compreso il carattere di fine stringa \0)]
    char usr[MAX_UNAME_LEN]; //mittente del messaggio
    int isannounce; //se = 1 il messaggio e' un annuncio
} message_s;

//informazioni per il thread che gestisce i messaggi
struct msgargs {
    clinfo *clients; //puntatore all'array delle informazioni dei client
    int *pipefd; //file descriptor della pipe
};

//====FUNZIONI====

//funzione per lanciare un errore
void error(char *msg)
{
    perror(msg);
    exit(1);
}

//handler per SIGINT
void chiudi()
{
    printf("\rShutting down...\n");
    exit(0);
}

//trova un uno struct clinfo libero da dare al nuovo client
int get_client_number(clinfo client_info[])
{
    //scorre tutti gli struct che contengono informazioni sui client, finche' non ne trova uno vuoto o il cui client non e' piu' collegato al server, e restituisce la sua posizione. Se non trova posti liberi restituisce -1
    for (int i = 0; i < MAX_USERS; i++) {
        if (!client_info[i].in_use)
            return i;
    }
    return -1;
}

//ordina i messaggi in base al loro timestamp, riceve in listamsg un array di messaggi lungo nmsg
void ordina(message_s listamsg[], int nmsg)
{
    int m; //
    message_s temp; //variabile temporanea per contenere un messaggio
    
    //selection sort
    for (int i = 0; i < nmsg; i++) {
        m = i;
        for (int j = i+1; j < nmsg; j++) {
            if (listamsg[j].timestamp < listamsg[m].timestamp)
                m = j;
        }
        
        memcpy(&temp, listamsg+i, sizeof(message_s));
        memcpy(listamsg+i, listamsg+m, sizeof(message_s));
        memcpy(listamsg+m, &temp, sizeof(message_s));
    }
}

//riempie il message_s DEST con il MESSAGE, il SENDER e IS_ANNOUNCE, e ricava il timestamp da MESSAGE
void gen_msg(message_s *dest, char *message, char *sender, int is_announce)
{
    char *sepPtr; //puntatore al carattere che separa il timestamp dal messaggio effettivo

    memset(dest, 0, sizeof(message_s)); //pulisco la locazione di memoria della destinazione

    strcpy(dest->usr, sender); //copio l'user
    dest->isannounce = is_announce; //setto il flag is_announce
    dest->timestamp = atol(message); //imposto il timestamp

    sepPtr = strchr(message, ':'); //trovo il separatore tra il timestamp e il messaggio
    strcpy(dest->msg, sepPtr+1); //copio il messaggio
}

void send_msg(message_s *msg, struct msgargs *args, FILE *logfile)
{
    char sendBuff[MAX_MSG_LEN]; //buffer messaggio da inviare
    struct tm *loctime; //variabile in cui mettere la rappresentazione del timestamp di un messaggio

    memset(sendBuff, 0, sizeof(sendBuff));
    loctime = localtime(&(msg->timestamp));

    //se il messaggio e' un annuncio, copia il messaggio cosi' com'e' nel buffer d'invio, altrimenti fai precedere il messaggio dal timestamp di invio e dallo username di chi l'ha inviato
    if (msg->isannounce) {
        strcpy(sendBuff, msg->msg);
    } else {
        snprintf(sendBuff, sizeof(sendBuff), "(%02d:%02d:%02d) [%s]: %s", loctime->tm_hour, loctime->tm_min, loctime->tm_sec, msg->usr, msg->msg);
    }
            
    //scrivi il messaggio nel file di log
    fputs(sendBuff, logfile);
    fflush(logfile);

    //invia il messaggio ad ogni client collegato (nell'array delle informazioni dei client, solo quelle che hanno in_use impostato a 1 contengono le informazioni di un client collegato)
    for (int j = 0; j < MAX_USERS; j++) {
        if (args->clients[j].in_use) {
             write(args->clients[j].clifd, sendBuff, sizeof(sendBuff));
        }
    }
}

//====THREADS====

//thread che si occupa di ricevere i messaggi da un client riceve in clientinfo uno struct clinfo contenente le informazioni del client
void *parla_con_client(void *clientinfo)
{
    char sendBuff[MAX_MSG_LEN]; //buffer messaggio da inviare
    char recvBuff[MAX_MSG_LEN]; //buffer messaggio da ricevere
    int nRead; //numero di byte letti
    char *sep; //puntatore al separatore tra timestamp e username
    char welcome[] = "Welcome, please insert username (max 24 chars)\n";
    char goodbye[] = " has left the chat";
    char announce[] = " has joined the chat!";
    char username[MAX_UNAME_LEN]; //username di massimo 24 caratteri
    message_s messaggio; //contiene il messaggio da inviare e le sue informazioni
    clinfo *info = clientinfo; //informazioni sul client

    //"pulisco" i buffer dei messaggi e dell'username
    memset(sendBuff, 0, sizeof(sendBuff));
    memset(recvBuff, 0, sizeof(recvBuff));
    memset(username, 0, sizeof(username));

    //chiedo l'username al client con il messaggio di welcome
    write(info->clifd, welcome, strlen(welcome));
    nRead = read(info->clifd, recvBuff, sizeof(recvBuff));
    
    //se non ho letto niente o se il client ha inviato !exit come username, vuol dire che e' terminato il suo processo, quindi faccio terminare anche il thread che se ne occupa e libero il posto per le informazioni di un altro client
    if (nRead <= 0 || strcmp(recvBuff, EXIT_CMD) == 0) { 
            info->in_use = 0;
            return 0;
        }

    recvBuff[strlen(recvBuff)-1] = '\0'; //sostituisco l'ultimo carattere ('\n') con \0
    sep = strchr(recvBuff, ':'); //trovo il separatore tra il timestamp e il messaggio
    *(sep+MAX_UNAME_LEN) = '\0'; //tronco l'username se e' troppo lungo

    //copio l'username nella variabile apposita
    strncpy(username, sep+1, MAX_UNAME_LEN);    

    //aggiungo 1 al contatore dei client collegati
    *(info->users) += 1;

    //annuncio l'entrata del client nel server:

    //copio il messaggio contenente timestamp, username, messaggio di annuncio e informazioni sugli utenti in sendBuff
    snprintf(sendBuff, sizeof(sendBuff), "%.50s%s (%i/%i)\n", recvBuff, announce, *(info->users), MAX_USERS); //ho scritto %.50s in modo da far copiare massimo 50 caratteri del recvBuff, l'ho fatto per non far lamentare il compilatore, avendo aggiunto il carattere \0 subito dopo la fine dell'username, che e' lungo massimo 25 caratteri, non corro mai il rischio di avere un messaggio piu' lungo di sendBuff

    //lavoro il messaggio e le sue informazioni e li salvo nella variabile messaggio
    gen_msg(&messaggio, sendBuff, username, 1);
    //passo tramite pipe il messaggio e le sue informazioni al thread che si occupa di inviarlo a tutti
    write(info->pipefd[1], &messaggio, sizeof(messaggio));

    //pulisco i buffer
    memset(sendBuff, 0, sizeof(sendBuff));
    memset(recvBuff, 0, sizeof(recvBuff));


    //comincio il ciclo in cui ogni volta che il client manda un messaggio, io lo leggo, ricavo le sue informazioni e le mando al thread che si occupa di spedirlo a tutti.
    while (1) {
        //se non riesco a leggere dal client interrompo il ciclo
        if (read(info->clifd, recvBuff, sizeof(recvBuff)) <= 0) {
            printf("ERROR reading from client %s\n", username);
            break;
        }

        //se ricevo il comando di uscita dal server come messaggio interrompo il ciclo
        if (strcmp(recvBuff, EXIT_CMD) == 0) {
            break;
        }

        //lavoro il messaggio ricevuto e le sue informazioni e li salvo nella variabile messaggio
        gen_msg(&messaggio, recvBuff, username, 0);

        //mando il messaggio al thread che se ne occupa tramite pipe
        write(info->pipefd[1], &messaggio, sizeof(messaggio));

        //pulisco i buffer per inviare e ricevere il messaggio
        memset(sendBuff, 0, sizeof(sendBuff));
        memset(recvBuff, 0, sizeof(recvBuff));
    }
    
    //una volta finito di parlare con il client chiudo la connessione
    close(info->clifd);

    *(info->users) -= 1; //diminuisco di uno il numero di utenti collegati
    info->in_use = 0; //contrassegno la variabile usata per le informazioni come libera per poter essere riutilizzata da altri client

    //preparo il messaggio di arrivederci
    snprintf(sendBuff, sizeof(sendBuff), "%ld:%s%s (%i/%i)\n", time(NULL), username, goodbye, *(info->users), MAX_USERS);
    //lavoro il messaggio e le sue informazioni e li salvo nella variabile messaggio
    gen_msg(&messaggio, sendBuff, username, 1);
    //mando il messaggio al thread che si occupa di inviarlo a tutti i client
    write(info->pipefd[1], &messaggio, sizeof(messaggio));
    return 0;
}

//thread che si occupa di inviare i messaggi a tutti i client nell'ordine di ricezione, riceve in info lo struct msgargs, che contiene le informazioni sui client e sulla pipe
void *gestisci_messaggi(void *info)
{
    message_s messaggio; //contiene il messaggio da inviare e le sue informazioni
    FILE *logfile; //file di log
    struct msgargs *args = info; //informazioni per il thread (informazioni sui client e file descriptors della pipe)

    //apro il file di log
    logfile = fopen("log.txt", "w+");
    if (logfile == NULL)
        error("ERROR opening file");

    //comincio un ciclo infinito in cui il thread riceve messaggi inviati dagli altri thread sulla pipe e li invia ai client
    while (1) {
        //leggi il prossimo messaggio sulla pipe
        read(args->pipefd[0], &messaggio, sizeof(messaggio));
        
        //invia il messaggio a tutti i client
        send_msg(&messaggio, args, logfile);
    }
}

//thread che si occupa di inviare i messaggi a tutti i client nell'ordine di invio da parte del client, riceve in info lo struct msgargs, che contiene le informazioni sui client e sulla pipe. Il thread rimane in ascolto per 2 secondi, o finche' non riceve 10 messaggi; una volta finito il lasso di tempo di ascolto, ordina i messaggi ricevuti in base al timestamp di invio e li invia a tutti i client
void *gestisci_messaggi_ordinati(void *info)
{
    message_s msgBuff[10]; //buffer che contiene 10 messaggi (e le loro informazioni)
    FILE *logfile; //file di log
    struct msgargs *args = info; //informazioni per il thread (informazioni sui client e file descriptors della pipe)
    int msgcounter = 0, select_ris; //contatore dei messaggi nel buffer, risultato della select
    fd_set fdset; //set di file descriptors da ascoltare, per la select
    struct timeval timeout; //variabile per il timeout della select
    time_t tempo; //variabile per prendere il timestamp dell'inizio del lasso di tempo in cui il thread e' in ascolto di nuovi messaggi
    double timediff; //variabile per contenere la differenza tra due timestamp 

    //pulisco i buffer dei messaggi da inviare e ricevere
    memset(msgBuff, 0, sizeof(msgBuff));

    //apro il file di log
    logfile = fopen("log.txt", "w+");
    if (logfile == NULL)
        error("ERROR opening file");

    //comincio un ciclo infinito in cui il thread riceve messaggi inviati dagli altri thread sulla pipe, li ordina e li invia ai client
    while (1) {

        //prendo il timestamp di adesso
        tempo = time(NULL);

        //rimane in ascolto per 2 secondi, o finche' non riceve 10 messaggi
        while (msgcounter < 10 && (timediff = difftime(time(NULL), tempo)) < 2) {
            timeout.tv_sec = 2-timediff;
            timeout.tv_usec = 0;

            //metto la pipe da ascoltare nell'argomento fdset della pipe
            FD_ZERO(&fdset);
            FD_SET(args->pipefd[0], &fdset);

            //aspetta fino a due secondi un messaggio
            select_ris = select((args->pipefd[0])+1, &fdset, NULL, NULL, &timeout);
            if (select_ris == -1)
                error("ERROR reading from pipe");

            //se la select restituisce 0 vuol dire che sono passati 2 secondi, quindi esci dal ciclo
            if (select_ris == 0) {
                break;
            }
            //altrimenti aggiungi il messaggio ricevuto al buffer dei messaggi e aumenta di uno il contatore dei messaggi nel buffer
            else {
                read(args->pipefd[0], msgBuff+msgcounter, sizeof(message_s));
                msgcounter++;
            }
        }

        //se non ci sono messaggi nel buffer ricomincia il ciclo
        if (msgcounter == 0)
            continue;

        //ordina i messaggi ricevuti secondo il loro timestamp di invio
        ordina(msgBuff, msgcounter);

        //invia ogni messaggio nel buffer a tutti i client
        for (int i = 0; i < msgcounter; i++) {
            send_msg(msgBuff+i, args, logfile);
        }
        
        //reimposto il counter dei messaggi nel buffer a 0
        msgcounter = 0;
    }
}

//====MAIN====

int main(int argc, char *argv[])
{
    int listenfd, tempfd, clino, users = 0; //file descriptor del socket "accettatore", file descriptor temporaneo per accettare le nuove connessioni, identificatore da dare al nuovo client, numero di utenti collegati
    struct sockaddr_in serv_addr; //informazioni per il socket "accettatore"
    clinfo client_info[MAX_USERS]; //array di struct clinfo che contengono le informazioni di ogni client
    char error_full[] = "Il server e' pieno, riprovare piu' tardi\n"; //messaggio da usare in caso il server sia pieno
    pthread_t clithread_id[MAX_USERS], msgthread_id; //Thread id per i client e per il tread 
    int pipefd[2]; //file descriptors della pipe
    struct msgargs msgthread_args; //argomenti per il thread che si occupa di distribuire i messaggi ai client
    
    struct sigaction act; //informazioni da dare al sigaction
    sigset_t set; //insieme di segnali da bloccare durante l'esecuzione dell'handler

    //imposto SIGTERM come segnale da bloccare durante l'esecuzione dell'handler
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);

    act.sa_flags = 0; //imposto i flags a 0
    act.sa_mask = set; //aggiungo set alle informazioni da dare al sigaction
    act.sa_handler = &chiudi; //imposto la funzione chiudi come handler

    //sigaction su SIGINT
    sigaction(SIGINT, &act, NULL);

    //se non ci sono abbastanza argomenti scrivi che argomenti dare alla chiamata del programma
    if(argc != 3) {
        printf("\n Usage: %s <port> <nmetodo>\n Dove: nmetodo = 1 per messaggi ordinati in base al tempo di ricezione del server\n       nmetodo = 2 per messaggi ordinati in base al tempo di invio\n",argv[0]);
        return 1;
    }

    //creo il socket accettatore e mi salvo il suo file descriptor
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) 
        error("ERROR opening socket");

    //"pulisco" la locazione di memoria
    memset(&serv_addr, 0, sizeof(serv_addr));

    //imposto la famiglia, la porta e l'indirizzo (ANY in modo da accettare messaggi da qualunque indirizzo) al serv_addr
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[1]));
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    //associo le informazioni di serv_addr al socket
    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    //metto in ascolto il socket, permettendo una coda di 10 richieste
    listen(listenfd, 10);

    //apro una pipe per far comunicare i thread che verranno creati tra di loro
    if (pipe(pipefd) == -1)
        error("ERROR creating pipe");

    //preparo le informazioni il thread che si occupa della gestione dei messaggi
    msgthread_args.clients = client_info; //gli do l'array degli struct che contengono le informazioni sui client
    msgthread_args.pipefd = pipefd; //gli do anche i file descriptor della pipe aperta prima

    //in base al numero che ha inserito l'utente al lancio del programma creo il thread che invia ai client messaggi ordinati in base al tempo di ricezione del server (gestisci_messaggi), o quello che li invia ordinati in base al tempo di invio
    if (atoi(argv[2]) == 1) {
        pthread_create(&msgthread_id, 0, &gestisci_messaggi, &msgthread_args);
    } else {
        pthread_create(&msgthread_id, 0, &gestisci_messaggi_ordinati, &msgthread_args);
    }

    //imposto ogni variabile per le informazioni dei clienti come libera
    for (int i=0; i < MAX_USERS; i++) {
        client_info[i].in_use = 0;
    }

    //il server comincia ad accettare messaggi
    while(1) {
        //accetto la connessione in arrivo e apro un socket con file descriptor tempfd per comunicare
        tempfd = accept(listenfd, (struct sockaddr*)NULL, NULL);

        //se il server e' pieno manda un messaggio al client che sta provando a connettersi, chiudi il suo file descriptor e torna all'inizio del ciclo, altrimenti trova il numero di uno struct clinfo libero da dare al nuovo client
        if ((clino = get_client_number(client_info)) == -1) {
            write(tempfd, error_full, strlen(error_full));
            close(tempfd);
            sleep(1);
            continue;
        }

        //preparo le informazioni da dare al nuovo thread
        client_info[clino].clifd = tempfd; //imposta il file descriptor del socket del client che il thred dovra' usare
        client_info[clino].users = &users; //imposta il puntatore alla variabile che contiene il numero di client attualmente collegati
        client_info[clino].pipefd = pipefd; //imposta i file descriptor della pipe per la comunicazione tra thread
        client_info[clino].in_use = 1; //imposta questo clinfo "in uso"

        //creo un thread per gestire il client appena accettato
        pthread_create(&clithread_id[clino], 0, &parla_con_client, &client_info[clino]);
        sleep(1);
    }
}