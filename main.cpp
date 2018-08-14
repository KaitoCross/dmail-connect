#include <iostream>
#include <cstdio>
#include <exception>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdexcept>
#include<arpa/inet.h>
#include <pthread.h>
#include <thread>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>
#include <csignal>
#include <sys/stat.h>
#include <syslog.h>
#include <mutex>
#include <chrono>
#include <err.h>
#include <cerrno>
#include "SignalHandling.h"
#include <semaphore.h>

#define BUFLEN 10

using namespace std;

void udp_listens_locally(int *sockfd, int* timeshift, bool* endMyLife, bool* mailsent, mutex* mTimeshift, mutex* mutMailArr, sem_t *connSem)
{
    /*Listens to requests from dmail-filter locally)*/
    *sockfd = socket(AF_INET,SOCK_DGRAM,0);
    struct sockaddr_in serv, client;
    serv.sin_family = AF_INET;
    serv.sin_port = htons(1337);
    serv.sin_addr.s_addr = inet_addr("127.0.0.1");
    socklen_t l = sizeof(client);
    socklen_t m = sizeof(serv);
    char buffer[10] = "";
    bind(*sockfd, (struct sockaddr*)&serv, sizeof(serv));
    //sem_wait(connSem);
    while(!*endMyLife){
        recvfrom(*sockfd,buffer,10,0,(struct sockaddr *)&client,&l);
        /* Parsing incoming messages*/
        if (strncmp(buffer, "REQTO", 5) == 0) {
            //Time request received: Send out offset to filter program
            syslog(LOG_NOTICE,"%s","RECEIVED TIME REQUEST\n");
            mTimeshift->lock();
            sprintf(buffer, "RCVTO %04d", *timeshift);
            mTimeshift->unlock();
            sendto(*sockfd, buffer, 10, 0, (struct sockaddr *) &client, m);
        }
        if (strncmp(buffer, "MAILD", 5) == 0) {
            //Mail processed by filter, information is important for the SIP-Pi client! will be relayed later!
            syslog(LOG_NOTICE,"%s", "MAIL PROCESSED\n");
            mutMailArr->lock();
            *mailsent=true;
            mutMailArr->unlock();
            strcpy(buffer,"REKKT");
        }
    }
}

void tcp_writes_globally(int *new_socket, bool *endMyLife, bool* mailsent,bool* repliedToKeepalive, bool* conndead, mutex* mutMailArr, mutex* connDeadMutex, sem_t* disconnSem, sem_t* connSem)
{
    typedef chrono::high_resolution_clock timerexact; //used for keepalive check
    auto t2 = timerexact::now(); //used for keepalive check
    auto t1 = timerexact::now(); //used for keepalive check
    bool connDeadtemp=true; //connection dead?
    bool killProgram=false; //to end the loop when program needs to quit
    int errorcode=1337;
    int timeouts = 0;
    stringstream logmsg;

    do {
        sem_wait(connSem);
        connDeadMutex->lock();
        connDeadtemp=*conndead;
        killProgram=*endMyLife;
        connDeadMutex->unlock();
        while (!killProgram && !connDeadtemp) { //when everything is alright
            mutMailArr->lock();
            if (*mailsent) { //when filter was successful, tell the TCP client
                send(*new_socket, "MAILD", strlen("MAILD"), 0);
                *mailsent = false;
            }
            mutMailArr->unlock();
            //check for timeout - custom keepalive packet after 5 secs
            t1 = timerexact::now();
            long seconds_passed = chrono::duration_cast<std::chrono::seconds>(t1-t2).count();
            if (seconds_passed >= 5)
            {
                connDeadMutex->lock();
                if (!*repliedToKeepalive) //reply will be detected by tcp reader thread
                {
                    timeouts++;
                    if(timeouts>3) {
                        *conndead = true;
                        syslog(LOG_NOTICE,"%s", "CONNECTION LOST - NO REPLY\n");
                        timeouts = 0;
                    }
                } else {
                    *repliedToKeepalive = false;
                }
                connDeadMutex->unlock();
                int tempo=(int)send(*new_socket, "ELPSY", strlen("ELPSY"),0); //sending keepalive packet
                if (tempo<1)
                {
                    errorcode=errno;
                    perror("send");
                    logmsg << " BYTES SENT "<<tempo<<"\n TIMEOUT CHECK ERRNO "<< errorcode << "\n" << strerror(errorcode) <<" " << stderr <<"\n";
                    syslog(LOG_ERR,"%s",logmsg.str().c_str());
                    logmsg.clear();
                    logmsg.str("");
                    if (errorcode >= 100) //error handling
                    {
                        shutdown(*new_socket, SHUT_RDWR);
                        close(*new_socket);
                        connDeadMutex->lock();
                        *conndead = true;
                        connDeadMutex->unlock();
                        syslog(LOG_ERR,"%s","CONNECTION LOST\n");
                    }
                }
                t2=timerexact::now();
            }
            connDeadtemp=*conndead;
            killProgram=*endMyLife;
            connDeadMutex->unlock();
        }
        connDeadMutex->lock();
        killProgram=*endMyLife;
        connDeadMutex->unlock();
        sem_post(disconnSem);
    }while (!killProgram);
}

void stopdaemon(int sockfd[], int sockfdamount, int *connfd, bool* endMyLife, thread* firstThread, thread* secondThread, thread* thirdThread)
{
    shutdown(*connfd,SHUT_RDWR);
    for (int i = 0; i < sockfdamount; i++) {
        shutdown(sockfd[i],SHUT_RDWR);
    }
    sleep(2);
    close(*connfd);
    for (int i = 0; i < sockfdamount; i++) {
        close(sockfd[i]);
    }
    *endMyLife = true;
    firstThread->join();
    secondThread->join();
    thirdThread->join();
    syslog(LOG_NOTICE,"%s","SHUTDOWN DONE");
}

void tcp_reads_global(int *new_socket, int* timeshift, bool *endMyLife, bool* repliedToKeepalive, bool* conndead, mutex* mTimeshift, mutex* connDeadMutex, sem_t* disconnSem, sem_t* connSem)
{
    bool connDeadTemp = false;
    bool killProgram=false;
    char buffer[10]="";
    char buffer2[10]="";
    int whaterror=1337;
    stringstream msg;
    do {
        sem_wait(connSem);
        connDeadMutex->lock();
        connDeadTemp=*conndead;
        killProgram=*endMyLife;
        connDeadMutex->unlock();
        while (!connDeadTemp && !killProgram) {
            int valread = (int) read(*new_socket, buffer, 10);
            if (valread == 0) {
                connDeadMutex->lock();
                *conndead = true;
                connDeadMutex->unlock();
                syslog(LOG_NOTICE,"%s","CONNECTION LOST 0 PACKET\n");
                shutdown(*new_socket,SHUT_RDWR);
                close(*new_socket);
            }
            if (strncmp(buffer, "RCVTO", 5) == 0) { //New offset received by SIP-Pi client!
                char *endOfBuffer = &buffer[10];    //Write new offset into memory later
                mTimeshift->lock();
                *timeshift = (int) strtol(buffer + 6, &endOfBuffer, 10);
                syslog(LOG_NOTICE,"%s","RECEIVED NEW TIME\n");
                mTimeshift->unlock();
                sprintf(buffer2,"RCVOK");
                int rcvok = (int)send(*new_socket, buffer2, strlen(buffer2), 0); //send RCVOK (signalling everything is ok)
                if (rcvok==-1)
                {
                    connDeadMutex->lock();
                    *conndead = true;
                    connDeadMutex->unlock();
                    whaterror = errno;
                    //cout << stderr << "STDERR\n";
                    msg << "RECEIVE NOT OK ERRNO " << whaterror << "\n";
                    syslog(LOG_ERR,"%s",msg.str().c_str());
                    msg.clear();
                    msg.str("");
                }
            }
            if (strncmp(buffer, "SHTDW", 5) == 0) { //secret shutdown command for this program
                send(*new_socket, "SDOWN", strlen("SDOWN"), 0);
                connDeadMutex->lock();
                *endMyLife = true;
                connDeadMutex->unlock();
            }
            if (strncmp(buffer, "ELPSY", strlen("ELPSY")) == 0) {
                send(*new_socket, "KONGROO", strlen("KONGROO"), 0); //Reply to keepalive packet
            }
            if (strncmp(buffer, "KONGROO", strlen("KONGROO")) == 0) {
                connDeadMutex->lock();
                *repliedToKeepalive=true; //Keepalive packet received, telling other thread
                connDeadMutex->unlock();
            }
            connDeadMutex->lock();
            connDeadTemp=*conndead;
            killProgram=*endMyLife;
            connDeadMutex->unlock();
        }
        connDeadMutex->lock();
        killProgram=*endMyLife;
        connDeadMutex->unlock();
        sem_post(disconnSem);
    } while (!killProgram);
}


void do_heartbeat() {
    int setbackHours = 0;
    bool endMyLife = false;
    bool mailed = false;
    bool conndead = true;
    bool ClientAliveConfirmed = false;
    int new_socket = 0;
    int socketfd[2];
    mutex mArrive;
    mutex mutSetbackHours;
    mutex mutConnDead;
    sem_t disconnectSem;
    sem_init(&disconnectSem,0,2);
    sem_t connectSem;
    sem_init(&connectSem,0,0);
    thread udplocal(udp_listens_locally, &socketfd[0], &setbackHours, &endMyLife, &mailed, &mutSetbackHours, &mArrive, &connectSem);
    thread tcpglobal(tcp_writes_globally, &new_socket, &endMyLife, &mailed, &ClientAliveConfirmed,
                     &conndead, &mArrive, &mutConnDead, &disconnectSem, &connectSem);
    thread tcpreadglobal(tcp_reads_global, &new_socket, &setbackHours, &endMyLife, &ClientAliveConfirmed,
                         &conndead, &mutSetbackHours, &mutConnDead,&disconnectSem, &connectSem);
    stringstream logmsg;
    struct sockaddr_in6 server;
    socketfd[1] = socket(AF_INET6, SOCK_STREAM, 0);
    int trueFlag = 1;
    setsockopt(socketfd[1], SOL_SOCKET, SO_REUSEADDR, &trueFlag, sizeof(int));
    setsockopt(socketfd[1], SOL_SOCKET, SO_REUSEPORT, &trueFlag, sizeof(int));
    setsockopt(socketfd[1], IPPROTO_IPV6, IPV6_V6ONLY, 0, sizeof(int));

    if (socketfd[1] < 0) {
        syslog(LOG_ERR,"%s","CANT CREATE SOCKET!\n");
        return;
    }
    unsigned int addrlen = sizeof(server);
    server.sin6_family = AF_INET6;
    server.sin6_addr = in6addr_any;
    server.sin6_port = htons(4242);
    syslog(LOG_NOTICE,"%s","ABOUT TO BIND TCP\n");
    int bindsuc = (int) bind(socketfd[1], (struct sockaddr *) &server, addrlen);
    if (bindsuc < 0) {
        logmsg << "ERROR BINDING " << bindsuc << "\n";
        syslog(LOG_ERR,"%s",logmsg.str().c_str());
        logmsg.clear();
        logmsg.str("");
        int errcode = errno;
        mutConnDead.lock();
        conndead = false;
        endMyLife = true;
        mutConnDead.unlock();
        logmsg << errcode << "\n";
        syslog(LOG_ERR,"%s", logmsg.str().c_str());
        logmsg.clear();
        logmsg.str("");
        return;
    } else {
        syslog(LOG_NOTICE,"%s", "BOUND TCP\n");
    }
    int listensuc = listen(socketfd[1], 1);
    if (listensuc) {
        logmsg << "ERROR LISTENING " << listensuc << "\n";
        syslog(LOG_ERR,"%s", logmsg.str().c_str());
        logmsg.str("");
        logmsg.clear();
        return;
    } else {
        syslog(LOG_NOTICE,"%s","LISTENING TCP\n");
    }
    try {
        SignalHandling theHandler(socketfd, 2, &new_socket, &endMyLife, &udplocal, &tcpglobal, &tcpreadglobal, &connectSem);
        theHandler.setupSignalHandlers();
        do {
            sem_wait(&disconnectSem);
            sem_wait(&disconnectSem);
            while (conndead && !endMyLife) {
                //when connection to client died, establish connection again with next client
                new_socket = accept(socketfd[1], (struct sockaddr *) &server, (socklen_t *) &addrlen);
                syslog(LOG_NOTICE,"%s","CONNECTION ESTABLISHED!\n");
                mutConnDead.lock();
                conndead = false;
                ClientAliveConfirmed = true;
                mutConnDead.unlock();
                if (new_socket > 0)
                {
                    sem_post(&connectSem);
                    sem_post(&connectSem);
                    sem_post(&connectSem);
                }
            }
        } while (!endMyLife);
        endMyLife = true;
        if (!theHandler.gotExitSignal()) {
            sem_post(&connectSem);
            sem_post(&connectSem);
            stopdaemon(socketfd, 2, &new_socket, &endMyLife, &udplocal, &tcpglobal, &tcpreadglobal);
        }
        sem_destroy(&disconnectSem);
        sem_destroy(&connectSem);
    }
    catch (SignalException& e) {
        logmsg << "SignalException: " << e.what();
        syslog(LOG_ERR,"%s",logmsg.str().c_str());
        logmsg.str("");
        logmsg.clear();
    }
}
// For security purposes, we don't allow any arguments to be passed into the daemon
int main(void)
{
    // Define variables
    pid_t pid, sid;

    // Fork the current process
    pid = fork();
    // The parent process continues with a process ID greater than 0
    if(pid > 0)
    {
        exit(EXIT_SUCCESS);
    }
        // A process ID lower than 0 indicates a failure in either process
    else if(pid < 0)
    {
        exit(EXIT_FAILURE);
    }

    // The parent process has now terminated, and the forked child process will continue
    // (the pid of the child process was 0)

    // Since the child process is a daemon, the umask needs to be set so files and logs can be written
    umask(0);

    // Open system logs for the child process
    openlog("dmail-connect", LOG_NOWAIT | LOG_PID, LOG_USER);
    syslog(LOG_NOTICE,"%s", "Successfully started dmail-connect");

    // Generate a session ID for the child process
    sid = setsid();
    // Ensure a valid SID for the child process
    if(sid < 0)
    {
        // Log failure and exit
        syslog(LOG_ERR,"%s", "Could not generate session ID for child process");

        // If a new session ID could not be generated, we must terminate the child process
        // or it will be orphaned
        exit(EXIT_FAILURE);
    }

    // Change the current working directory to a directory guaranteed to exist
    if((chdir("/")) < 0)
    {
        // Log failure and exit
        syslog(LOG_ERR,"%s", "Could not change working directory to /");

        // If our guaranteed directory does not exist, terminate the child process to ensure
        // the daemon has not been hijacked
        exit(EXIT_FAILURE);
    }

    // A daemon cannot use the terminal, so close standard file descriptors for security reasons
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    //execute daemon heartbeat - infinite loop in function
    do_heartbeat();

    // Close system logs for the child process
    syslog(LOG_NOTICE,"%s", "Stopping dmail-connect");
    closelog();

    // Terminate the child process when the daemon completes
    exit(EXIT_SUCCESS);
}