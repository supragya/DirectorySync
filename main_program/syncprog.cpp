#include <iostream>
#include <string.h>
#include <thread>
#include <mutex>
#include <fstream>
#include <sys/inotify.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>

#define WORKCMDLEN 10000
#define MAXTRANSACTIONS 100
#define MAXFILENAMELEN  1000
#define INOTIFYBUFLEN   1000
#define EVENT_SIZE  ( sizeof (struct inotify_event) ) /*size of one event*/

enum FileChangeType_t{
    FCT_NONE,
    MODIFY,
    DELETE,
    CREATE
};
enum FileType_t{
    FT_NONE,
    _FILE,
    DIRECTORY
};

struct message{
    char                    FileName[MAXFILENAMELEN];
    enum FileChangeType_t   FCType;
    enum FileType_t         FType;
    unsigned long           FileSize;
};

class TransactionHistory_t{
    bool    ValidTransactionHistory[MAXTRANSACTIONS];
    message TransactionHistory[MAXTRANSACTIONS];
    std::mutex mtx;
public:
    TransactionHistory_t(){
        for(int i=0; i<MAXTRANSACTIONS; i++){
            ValidTransactionHistory[i] = false;
            strcpy(TransactionHistory[i].FileName, "");
            TransactionHistory[i].FCType = FCT_NONE;
            TransactionHistory[i].FType = FT_NONE;
            TransactionHistory[i].FileSize = 0;
        }
    }
    bool is_recorded(message m){
        mtx.lock();
        bool FoundRecord = false;
        for(int i=0; i<MAXTRANSACTIONS; i++){
            if(!ValidTransactionHistory[i])
                continue;
            if( strcmp(m.FileName, TransactionHistory[i].FileName) == 0 &&
                m.FType == TransactionHistory[i].FType &&
                m.FileSize == TransactionHistory[i].FileSize &&
                m.FCType == TransactionHistory[i].FCType){
                    FoundRecord = true;
                    ValidTransactionHistory[i] = false;
                    break;
            }
        }
        mtx.unlock();
        return FoundRecord;
    }
    bool record_message(message m){
        mtx.lock();
        bool RecordingSuccess = false;
        int i = 0;
        while(!RecordingSuccess && i<MAXTRANSACTIONS){
            if(ValidTransactionHistory[i]){
                i++;
                continue;
            }
            else{
                strcpy(TransactionHistory[i].FileName, m.FileName);
                TransactionHistory[i].FCType = m.FCType;
                TransactionHistory[i].FileSize = m.FileSize;
                TransactionHistory[i].FType = m.FType;
                ValidTransactionHistory[i] = true;
                RecordingSuccess = true;
                break;
            }
        }
        mtx.unlock();
        return RecordingSuccess;
    }
};

/* Globals */
TransactionHistory_t    Transactions;
std::mutex              FileAccess;
/* Function declarations */

void reader(int portno, char *dirname);
void filewatcher(char *dirname, int portno, char* ipaddr);
message* FormMessage(char *filename, FileChangeType_t fctype, FileType_t ftype, unsigned long filesize);


/* Main driver function
 * Parameters:
 *      1.  Folder to watch
 *      2.  IP address of other droid
 *      3.  Port no for filewatcher
 *      4.  Port no for reciever
 */



int main( int argc, char **argv)
{
    if(argc != 5){
        std::cout<<"[MAIN] Usage: <programexec> <FolderName> <droid_IP> <SendPORT> <RecvPORT>"<<std::endl;
        exit(-1);
    }
    std::thread listener(reader, atoi(argv[4]), argv[1]);
    std::thread watcher(filewatcher, argv[1], atoi(argv[3]), argv[2]);
    listener.join();
    watcher.join();
    return 0;
}




void reader(int portno, char *dirname) {
    std::cout << "#### Began thread [RECV] on port ["<<portno<<"]"<<std::endl;


    // Network
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1, valread;
    int addrlen = sizeof(address);

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        std::cout<<"[RECV] Socket Failed"<<std::endl;
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        std::cout<<"[RECV] Socket Failed (setsockopt)"<<std::endl;
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( portno );

    // Forcefully attaching socket to the port
    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address))<0)
    {
        std::cout<<"[RECV] Socket Failed to bind"<<std::endl;
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0)
    {
        std::cout<<"[RECV] Error Listen"<<std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout<<"[RECV] Ready to listen. Ping the other droid!"<<std::endl;

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0)
    {
        std::cout<<"[RECV] Error accepting connection"<<std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout<<"[RECV] Accepted communication at [" <<portno<<"]"<<std::endl;



    // Start receiving
    message *incoming = (message*)malloc(sizeof(message));
    char workCMD[WORKCMDLEN];
    while(1){
        valread = recv(new_socket ,(void*)incoming, sizeof(message), 0);

        switch(incoming->FCType){
            case CREATE:
            case MODIFY:
                if(incoming->FType == DIRECTORY){
                    if(!Transactions.record_message(*incoming)){
                        std::cout<<"[RECV] Cannot add transaction"<<std::endl;
                        exit(-1);
                    }
                    std::cout<<"[RECV] Creating DIR ["<<incoming->FileName<<"]"<<std::endl;

                    strcpy(workCMD, "mkdir -p ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->FileName);

                    system(workCMD);
                }
                else if(incoming->FType == _FILE){
                    if(!Transactions.record_message(*incoming)){
                        std::cout<<"[RECV] Cannot add transaction"<<std::endl;
                        exit(-1);
                    }

                    std::cout<<"[RECV] Creating FIL ["<<incoming->FileName<<"]"<<std::endl;

                    FileAccess.lock();
                    strcpy(workCMD, "touch ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->FileName);

                    system(workCMD);
                    if(incoming->FCType == MODIFY){
                        strcat(workCMD, dirname);
                        strcat(workCMD, "/");
                        strcat(workCMD, incoming->FileName);
                        std::ofstream filedump(workCMD, std::ios::binary);
                        char *filedata = (char *)malloc(sizeof(char)*incoming->FileSize);
                        std::cout<<"[RECV] Begin reading data of file from other droid"<<std::endl;
                        read(new_socket, filedata, sizeof(filedata));
                        std::cout<<"[RECV] Done reading data of file from other droid"<<std::endl;
                        std::cout<<"[RECV] Begin writing data to file "<<incoming->FileName<<std::endl;
                        if(!filedump){
                            std::cout<<"[RECV] Error opening file "<<incoming->FileName<<std::endl;
                            exit(1);
                        }
                        filedump<<filedata;
                        std::cout<<"[RECV] Done writing data to file "<<incoming->FileName<<std::endl;
                        free(filedata);
                        filedump.close();
                    }
                    FileAccess.unlock();

                }
                break;

            case DELETE:
                if(incoming->FType == DIRECTORY){
                    if(!Transactions.record_message(*incoming)){
                        std::cout<<"[RECV] Cannot add transaction"<<std::endl;
                        exit(-1);
                    }
                    std::cout<<"[RECV] Deleting DIR ["<<incoming->FileName<<"]"<<std::endl;

                    strcpy(workCMD, "rmdir ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->FileName);

                    system(workCMD);
                }
                else if(incoming->FType == _FILE){
                    if(!Transactions.record_message(*incoming)){
                        std::cout<<"[RECV] Cannot add transaction"<<std::endl;
                        exit(-1);
                    }

                    std::cout<<"[RECV] Deleting FIL ["<<incoming->FileName<<"]"<<std::endl;

                    strcpy(workCMD, "rm ");
                    strcat(workCMD, dirname);
                    strcat(workCMD, "/");
                    strcat(workCMD, incoming->FileName);

                    system(workCMD);

                }
                break;
            default:
                std::cout<<"[RECV] Unknown message received, QUIT"<<std::endl;
                exit(0);
        }

    }
}

void filewatcher(char *dirname, int portno, char* ipaddr) {

    std::cout << "#### Began thread [FILEWATCHER] for directory ["<<dirname<<"]"<<std::endl;




    // Initialise FileWatcher
    int fd, wd;
    fd = inotify_init();
    if ( fd < 0 ) {
        std::cout<<"[FW: inotify] Could not initialise inotify, quitting"<<std::endl;
        exit(1);
    }
    wd = inotify_add_watch(fd, dirname, IN_CREATE | IN_MODIFY | IN_DELETE);
    if (wd == -1){
        std::cout<<"[FW: inotify] Could not add watch to ["<<dirname<<"], quitting\n";
        exit(1);
    }
    else
        std::cout<<"[FW: inotify] Started watching ["<<dirname<<"]"<<std::endl;





    // Network setup with other Droid
    struct sockaddr_in address;
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        std::cout<<"[FW: connect] Socket creation error"<<std::endl;
        exit(1);
    }

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, ipaddr, &serv_addr.sin_addr)<=0)
    {
        std::cout<<"[FW: connect] Invalid address/ Address not supported "<<std::endl;
        exit(1);
    }
    std::cout<<"[FW: connect] Other bot ready to accept communication?";
    char ch;
    std::cin>>ch;
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        std::cout<<"[FW: connect] Connection Failed"<<std::endl;
        exit(1);
    }
    else
        std::cout<<"[FW: connect] Successfully setup connection with other droid\n";




    // Actual FileWatch system
    char *FWINBuffer[INOTIFYBUFLEN], workCMD[10000];
    int length;
    while(1) {
        int i = 0;
        length = read(fd, FWINBuffer, INOTIFYBUFLEN);
        if (length < 0)
            std::cout << "[FILEWATCHER] Read Error \n";
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &FWINBuffer[i];
            if (event->len) {
                if (event->mask & IN_CREATE) {
                    if (event->mask & IN_ISDIR) {
                        std::cout << "[FW: inotify]\tCreated\tDIR\t" << event->name << std::endl;
                        message *message_packet = FormMessage(event->name, CREATE, DIRECTORY, 0);
                        if (!Transactions.is_recorded(*message_packet)) {
                            int sent = send(sock, message_packet, sizeof(message), 0);
                            std::cout << "[FW: inotify]\tSent request to create [" << event->name << "] Directory"
                                      << std::endl;
                        } else {
                            std::cout << "[FW: inotify]\tDirectory [" << event->name
                                      << "]  created due to incoming request, skip request send" << std::endl;
                        }

                        free(message_packet);
                    }
                    else {
                        std::cout << "[FW: inotify]\tCreated\tFIL\t" << event->name << std::endl;

                        FileAccess.lock();
                        strcpy(workCMD, dirname);
                        strcat(workCMD, "/");
                        strcat(workCMD, event->name);
                        std::ifstream filestream(workCMD, std::ios::binary);
                        while(!filestream){
                            std::cout<<"[FILEWATCHER]-- Could not open file, retrying"<<std::endl;
                            usleep(100);
                            filestream.open(workCMD, std::ios::binary);
                        }
                        filestream.seekg(0, filestream.end);
                        message *message_packet = FormMessage(event->name, CREATE, _FILE, filestream.tellg());
                        filestream.close();
                        FileAccess.unlock();

                        if (!Transactions.is_recorded(*message_packet)) {
                            int sent = send(sock, message_packet, sizeof(message), 0);
                            std::cout << "[FW: inotify]\tSent request to create [" << event->name << "] file"
                                      << std::endl;
                        } else {
                            std::cout << "[FW: inotify]\tFile [" << event->name
                                      << "]  created due to incoming request, skip request send" << std::endl;
                        }

                        free(message_packet);
                    }
                }

                else if (event->mask & IN_DELETE) {
                    if (event->mask & IN_ISDIR) {
                        std::cout << "[FW: inotify]\tDeleted\tDIR\t" << event->name << std::endl;
                        message *message_packet = FormMessage(event->name, DELETE, DIRECTORY, 0);
                        if (!Transactions.is_recorded(*message_packet)) {
                            int sent = send(sock, message_packet, sizeof(message), 0);
                            std::cout << "[FW: inotify]\tSent request to delete [" << event->name << "] Directory"
                                      << std::endl;
                        } else {
                            std::cout << "[FW: inotify]\tDirectory [" << event->name
                                      << "]  deleted due to incoming request, skip request send" << std::endl;
                        }

                        free(message_packet);
                    }
                    else {
                        std::cout << "[FW: inotify]\tDeleted\tFIL\t" << event->name << std::endl;

                        message *message_packet = FormMessage(event->name, DELETE, _FILE, 0);

                        if (!Transactions.is_recorded(*message_packet)) {
                            int sent = send(sock, message_packet, sizeof(message), 0);
                            std::cout << "[FW: inotify]\tSent request to delete [" << event->name << "] file"
                                      << std::endl;
                        } else {
                            std::cout << "[FW: inotify]\tFile [" << event->name
                                      << "]  delete due to incoming request, skip request send" << std::endl;
                        }

                        free(message_packet);
                    }
                }
            }
            i += EVENT_SIZE + event->len;
        }
    }

}


message* FormMessage(char *filename, FileChangeType_t fctype, FileType_t ftype, unsigned long filesize){
    message* newmsg = (message *)malloc(sizeof(message));
    strcpy(newmsg->FileName, filename);
    newmsg->FType = ftype;
    newmsg->FCType = fctype;
    newmsg->FileSize = filesize;
    return newmsg;
}