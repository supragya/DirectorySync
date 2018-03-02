#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fstream>
#include <sys/types.h>
#include <sys/inotify.h>
#include <netinet/in.h>
#include <limits.h>
#include <sys/stat.h>
#include <mutex>
#include <arpa/inet.h>
#include <thread>

#define MAX_EVENTS 1024 /*Max. number of events to process at one go*/
#define LEN_NAME 16 /*Assuming that the length of the filename won't exceed 16 bytes*/
#define EVENT_SIZE  ( sizeof (struct inotify_event) ) /*size of one event*/
#define BUF_LEN     ( MAX_EVENTS * ( EVENT_SIZE + LEN_NAME )) /*buffer to store the data of events*/

// Data Structures

std::mutex mtx;

enum messagetype{
    TRY_SEND,
    READY_RECIEVE,
    SENDING,
    ACK_RECIEVE
};

enum filechangetype{
    MODIFY,
    DELETE,
    CREATE
};

enum filetype{
    FILE_,
    DIRECTORY
};

struct message{
    long long int filesize;
    char filename[1000];
    enum messagetype mtype;
    enum filechangetype fctype;
    enum filetype ftype;
};

void reader(int portno, char *dirname){
    int server_fd, new_socket, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char workCMD[10000];
    std::cout<<"-- Began thread [RECV] --\n";
    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        std::cout<<"[RECV] Socket Failed\n";
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                                                  &opt, sizeof(opt)))
    {
        std::cout<<"[RECV] Socket Failed (setsockopt)\n";
        exit(EXIT_FAILURE);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( portno );

    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr *)&address,
                                 sizeof(address))<0)
    {
        std::cout<<"[RECV] Socket Failed to bind\n";
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0)
    {
        std::cout<<"[RECV] Error Listen\n";
        exit(EXIT_FAILURE);
    }

    std::cout<<"[RECV] Ready to listen. Ping the other droid"<<std::endl;

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0)
    {
        std::cout<<"[RECV] Error accept\n";
        exit(EXIT_FAILURE);
    }

    std::cout<<"[RECV] Accepted communication at " <<portno<< std::endl;
    message *incoming = (message*)malloc(sizeof(message));
    message *outgoing = (message*)malloc(sizeof(message));
    char *filedata;
    while(1){
        valread = recv(new_socket , (void*)incoming, sizeof(message), 0);
        if(incoming->mtype == TRY_SEND){
            std::cout<<"[RECV] Recieved try send message from other droid... "<<valread<<" of "<<sizeof(message)<<std::endl;
            if(incoming->fctype == CREATE || incoming->fctype == MODIFY){
                std::cout<<"[RECV] FCtype = CREATE or MODIFY"<<std::endl;
                if(incoming->ftype == DIRECTORY){
                    std::cout<<"[RECV] FileType = DIRECTORY named "<<incoming->filename<<std::endl;
                    strcpy(workCMD, "mkdir -p ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->filename);
                    std::cout<<"[RECV] workCMD: "<<workCMD<<std::endl;
                    system(workCMD);
                    std::cout<<"[RECV] Created directory "<<incoming->filename<<std::endl;
                }
                else{
                    std::cout<<"[RECV] FileType = FILE  named "<<incoming->filename<<" sized "<<incoming->filesize<<std::endl;
                    filedata = (char*)malloc(sizeof(char)*incoming->filesize);
                    if(!filedata){
                        std::cout<<"[RECV] Error allocating space for transfer of "<<incoming->filename<<std::endl;
                        exit(1);
                    }
                    outgoing->mtype = READY_RECIEVE;
                    strcpy(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->filename);
                    std::ofstream filedump(workCMD, std::ios::binary);
                    if(incoming->fctype == MODIFY){
                        std::cout<<"[RECV] Begin reading data of file from other droid"<<std::endl;
                        read(new_socket, filedata, sizeof(filedata));
                        std::cout<<"[RECV] Done reading data of file from other droid"<<std::endl;
                        std::cout<<"[RECV] Begin writing data to file "<<incoming->filename<<std::endl;
                        if(!filedump){
                            std::cout<<"[RECV] Error opening file "<<incoming->filename<<std::endl;
                            exit(1);
                        }
                        filedump<<filedata;
                        std::cout<<"[RECV] Done writing data to file "<<incoming->filename<<std::endl;
                    }
                    filedump.close();
                    free(filedata);
                }
            }
            else if(incoming->fctype == DELETE){
                std::cout<<"[RECV] FCtype = DELETE"<<std::endl;
                if(incoming->ftype == DIRECTORY){
                    std::cout<<"[RECV] FileType = DIRECTORY named "<<incoming->filename<<std::endl;
                    strcpy(workCMD, "rm -r ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->filename);
                    std::cout<<"[RECV] workCMD: "<<workCMD<<std::endl;
                    system(workCMD);
                    std::cout<<"[RECV] Deleted directory "<<incoming->filename<<std::endl;
                }
                else{
                    std::cout<<"[RECV] FileType = FILE  named "<<incoming->filename<<" sized "<<incoming->filesize<<std::endl;
                    strcpy(workCMD, "rm ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->filename);
                    std::cout<<"[RECV] workCMD: "<<workCMD<<std::endl;
                    system(workCMD);
                    std::cout<<"[RECV] Deleted file "<<incoming->filename<<std::endl;
                }
            }
        }
    }
}
void filewatcher(char *dirname, int portno, char* ipaddr);

int main( int argc, char **argv)
{
    std::thread listener(reader, atoi(argv[4]), argv[1]);
    std::thread watcher(filewatcher, argv[1], atoi(argv[3]), argv[2]);
    listener.join();
    return 0;
}

void filewatcher(char *dirname, int portno, char* ipaddr){

    int length, i = 0, wd;
    int fd;
    char *buffer[BUF_LEN], workCMD[10000];
    std::cout<<"-- Began thread [FILEWATCHER] --\n";
    /* Initialize Inotify*/
    fd = inotify_init();
    if ( fd < 0 ) {
        std::cout<<"[FILEWATCHER] Could not initialise inotify, quitting\n";
        exit(1);
    }

    /* add watch to starting directory */
    wd = inotify_add_watch(fd, dirname, IN_CREATE | IN_MODIFY | IN_DELETE);

    if (wd == -1){
        std::cout<<"[FILEWATCHER] Could not add watch to "<<dirname<<", quitting\n";
        exit(1);
    }
    else
        std::cout<<"[FILEWATCHER] Started watching "<<dirname<<"\n";

    struct sockaddr_in address;
    int sock = 0, valread;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("[FILEWATCHER] Socket creation error \n");
        exit(1);
    }

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, ipaddr, &serv_addr.sin_addr)<=0)
    {
        printf("[FILEWATCHER] Invalid address/ Address not supported \n");
        exit(1);
    }
    std::cout<<"[FILEWATCHER] Other bot ready to accept communication? [y/n]?";
    char ch;
    std::cin>>ch;
    if(ch !='y' && ch !='Y'){
        std::cout<<"[FILEWATCHER] You did not want to communicate. Bye"<<std::endl;
        exit(1);
    }
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("[FILEWATCHER] Connection Failed \n");
        exit(1);
    }

    std::cout<<"[FILEWATCHER] Successfully setup connection with other droid\n";
    message *outgoing = (message*)malloc(sizeof(message));

    /* do it forever*/
    while(1){
        i = 0;
        length = read( fd, buffer, BUF_LEN );
        if ( length < 0 )
            std::cout<<"[FILEWATCHER] Read Error \n";

        while ( i < length ) {
            struct inotify_event *event = (struct inotify_event *) &buffer[ i ];
            if (event->len ) {
                if (event->mask & IN_CREATE) {
                    if (event->mask & IN_ISDIR){
                        std::cout<<"[FILEWATCHER]\tCreated\tDIR\t"<<event->name<<std::endl;
                        outgoing->mtype = TRY_SEND;
                        outgoing->fctype = CREATE;
                        outgoing->ftype = DIRECTORY;
                        strcpy(outgoing->filename, event->name);
                        std::cout<<"[FILEWATCHER]-- Sending request to create directory "<<outgoing->filename<<std::endl;
                        int sent = send(sock, outgoing, sizeof(message), 0 );
                        std::cout<<"[FILEWATCHER]-- Request sent, bytes = "<<sent<<" of "<<sizeof(message)<<std::endl;

                    }
                    else{
                        std::cout<<"[FILEWATCHER]\tCreated\tFILE\t"<<event->name<<std::endl;
                        outgoing->mtype = TRY_SEND;
                        outgoing->fctype = CREATE;
                        outgoing->ftype = FILE_;
                        strcpy(outgoing->filename, event->name);
                        strcpy(workCMD, dirname);
                        strcat(workCMD, "/");
                        strcat(workCMD, event->name);
                        std::cout<<"[FILEWATCHER] workCMD: "<<workCMD<<std::endl;
                        std::ifstream filestream(workCMD, std::ios::binary);
                        while(!filestream){
                            std::cout<<"[FILEWATCHER]-- Could not open file, retrying"<<std::endl;
                            usleep(100000);
                            filestream.open(workCMD, std::ios::binary);
                        }
                        filestream.seekg(0, filestream.end);
                        outgoing->filesize = filestream.tellg();
                        outgoing->filesize = outgoing->filesize > 0 ? outgoing->filesize : 0;
                        filestream.seekg(0, filestream.beg);
                        std::cout<<"[FILEWATCHER]-- Sending request to create file "<<outgoing->filename<<std::endl;
                        int sent = send(sock, outgoing, sizeof(message), 0 );
                        std::cout<<"[FILEWATCHER]-- Request sent, bytes = "<<sent<<" of "<<sizeof(message)<<std::endl;
                        std::cout<<"[FILEWATCHER] Loading file in memory"<<std::endl;
                        char *filedata = (char *)malloc(sizeof(char)*outgoing->filesize);
                        filestream.read(filedata, outgoing->filesize);
                        std::cout<<"[FILEWATCHER] Loading file in memory... DONE"<<std::endl;
                        std::cout<<"[FILEWATCHER] Sending data to droid"<<std::endl;
                        sent = 0;
                        sent = send(sock, filedata, outgoing->filesize, 0 );
                        std::cout<<"[FILEWATCHER] Filedata sent, bytes = "<<sent<<" of "<<outgoing->filesize<<std::endl;
                        free(filedata);
                        filestream.close();
                    }
                }

                if ( event->mask & IN_MODIFY) {
                    if (event->mask & IN_ISDIR){
                        std::cout<<"[FILEWATCHER]\tModified\tDIR\t"<<event->name<<std::endl;
                    }
                    else{
                        std::cout<<"[FILEWATCHER]\tModified\tFILE\t"<<event->name<<std::endl;
                        outgoing->mtype = TRY_SEND;
                        outgoing->fctype = MODIFY;
                        outgoing->ftype = FILE_;
                        strcpy(outgoing->filename, event->name);
                        strcpy(workCMD, dirname);
                        strcat(workCMD, "/");
                        strcat(workCMD, event->name);
                        std::ifstream filestream(workCMD, std::ios::binary);
                        while(!filestream){
                            std::cout<<"[FILEWATCHER]-- Could not open file, retrying"<<std::endl;
                            usleep(100000);
                            filestream.open(workCMD, std::ios::binary);
                        }
                        filestream.seekg(0, filestream.end);
                        outgoing->filesize = filestream.tellg();
                        outgoing->filesize = outgoing->filesize > 0 ? outgoing->filesize : 0;
                        filestream.seekg(0, filestream.beg);
                        std::cout<<"[FILEWATCHER]-- Sending request to create file "<<outgoing->filename<<std::endl;
                        int sent = send(sock, outgoing, sizeof(message), 0 );
                        std::cout<<"[FILEWATCHER]-- Request sent, bytes = "<<sent<<" of "<<sizeof(message)<<std::endl;
                        std::cout<<"[FILEWATCHER] Loading file in memory"<<std::endl;
                        char *filedata = (char *)malloc(sizeof(char)*outgoing->filesize);
                        filestream.read(filedata, outgoing->filesize);
                        std::cout<<"[FILEWATCHER] Loading file in memory... DONE"<<std::endl;
                        std::cout<<"[FILEWATCHER] Sending data to droid"<<std::endl;
                        sent = 0;
                        sent = send(sock, filedata, outgoing->filesize, 0 );
                        std::cout<<"[FILEWATCHER] Filedata sent, bytes = "<<sent<<" of "<<outgoing->filesize<<std::endl;
                        free(filedata);
                        filestream.close();
                    }
                }

                if ( event->mask & IN_DELETE) {
                    if (event->mask & IN_ISDIR){
                        std::cout<<"[FILEWATCHER]\tDeleted\tDIR\t"<<event->name<<std::endl;
                    }
                    else{
                        std::cout<<"[FILEWATCHER]\tDeleted\tFILE\t"<<event->name<<std::endl;
                    }
                }

                i += EVENT_SIZE + event->len;
            }
        }
    }

    /* Clean up*/
    inotify_rm_watch( fd, wd );
    close( fd );

}
