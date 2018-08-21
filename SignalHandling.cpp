//
// Created by chris on 16.05.2018.
//

#include "SignalHandling.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdexcept>
#include<arpa/inet.h>
#include <pthread.h>
#include <thread>
#include <netinet/in.h>
#include <unistd.h>
#include <csignal>
#include <sys/stat.h>
#include <mutex>
#include <semaphore.h>

using namespace std;

bool SignalHandling::mbGotExitSignal = false;
bool *SignalHandling::endMyLife = NULL;
bool *SignalHandling::conndead = NULL;
int *SignalHandling::new_socket = NULL;
int *SignalHandling::socketfd = NULL;
sem_t *SignalHandling::connSem = NULL;
sem_t *SignalHandling::sendsmthSem = NULL;
int SignalHandling::sockfds_open = 0;
thread *SignalHandling::openThreads[4];

/**
* Default Contructor.
*/
SignalHandling::SignalHandling()
{
}

/**
* Destructor.
*/
SignalHandling::~SignalHandling()
{
}

/**
* Returns the bool flag indicating whether we received an exit signal
* @return Flag indicating shutdown of program
*/
bool SignalHandling::gotExitSignal()
{
    return mbGotExitSignal;
}

/**
* Sets the bool flag indicating whether we received an exit signal
*/
void SignalHandling::setExitSignal(bool _bExitSignal)
{
    mbGotExitSignal = _bExitSignal;
}

/**
* Sets exit signal to true.
* @param[in] _ignored Not used but required by function prototype
*                     to match required handler.
*/
void SignalHandling::exitSignalHandler(int _ignored)
{
    mbGotExitSignal = true;
    shutdown(*new_socket,SHUT_RDWR);
    for (int i = 0; i < sockfds_open; i++) {
        shutdown(socketfd[i],SHUT_RDWR);
    }
    close(*new_socket);
    for (int i = 0; i < sockfds_open; i++) {
        close(socketfd[i]);
    }
    *endMyLife = true;
    sem_post(sendsmthSem);
    sem_post(connSem);
    sem_post(connSem);
    sem_post(connSem);
    openThreads[0]->join();
    openThreads[1]->join();
    openThreads[2]->join();
    openThreads[3]->join();
}

void SignalHandling::setupSignalHandlers()
{
    if (signal(SIGQUIT, SignalHandling::exitSignalHandler) == SIG_ERR)
    {
        throw SignalException("!!!!! Error setting up signal handlers !!!!!");
    }
    if (signal(SIGHUP, SignalHandling::exitSignalHandler) == SIG_ERR)
    {
        throw SignalException("!!!!! Error setting up signal handlers !!!!!");
    }
    if (signal(SIGTERM, SignalHandling::exitSignalHandler) == SIG_ERR)
    {
        throw SignalException("!!!!! Error setting up signal handlers !!!!!");
    }
    if (signal(SIGUSR1, SignalHandling::exitSignalHandler) == SIG_ERR)
    {
        throw SignalException("!!!!! Error setting up signal handlers !!!!!");
    }
}

SignalHandling::SignalHandling(int sockfd[], int sockfdamount, int *connfd, bool* endMyLife, std::thread* firstThread, std::thread* secondThread, std::thread* thirdThread, std::thread* fourthThread, sem_t *connSema, sem_t* sendSigSem)
{
    sockfds_open=sockfdamount;
    socketfd = sockfd;
    new_socket=connfd;
    this->endMyLife = endMyLife;
    openThreads[0] = firstThread;
    openThreads[1] = secondThread;
    openThreads[2] = thirdThread;
    openThreads[3] = fourthThread;
    connSem = connSema;
    sendsmthSem = sendSigSem;
}