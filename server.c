#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include "threadpool.h"


#define MAX_REQUEST_LENGHT 4000
#define MAX_ENTITY_LINE 500
#define MAX_PORT 65535
#define NUM_OF_COMMANDS 4
#define PRINT_WRONG_CMD_USAGE "Usage: server <port> <pool-size> <max-number-of-request>\n"

static int sPort = 0;
static int sPoolSize = 0;
static int sMaxRequests = 0;

int parseArguments(int, char**);
int verifyPort(char*);
int initServer();
void initServerSocket(int*);

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

int main(int argc, char* argv[]) {

        if(argc != NUM_OF_COMMANDS) {
                printf(PRINT_WRONG_CMD_USAGE);
                exit(-1);
        }

        if(parseArguments(argc, argv)) {
                printf(PRINT_WRONG_CMD_USAGE);
                exit(-1);
        }

        initServer(argc, argv);

}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

int parseArguments(int argc, char** argv) {

        if(verifyPort(argv[1]))
                return -1;

        int assigned;
        //verify pool size is only digits
        assigned = strspn(argv[2], "0123456789");
        if(assigned != strlen(argv[2]))
                return -1;
        sPoolSize = atoi(argv[2]);

        //verify max requests is only digits
        assigned = strspn(argv[3], "0123456789");
        if(assigned != strlen(argv[3]))
                return -1;
        sMaxRequests = atoi(argv[3]);

        return 0;
}

/*********************************/
/*********************************/
/*********************************/

int verifyPort(char* port_string) {

        //check port_string containts only digits
        int assigned = strspn(port_string, "0123456789");
        if(assigned != strlen(port_string))
                return -1;

        sPort = atoi(port_string);
        if(sPort > MAX_PORT)
                return -1;

        return 0;
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

int initServer() {

        int server_socket = 0;
        initServerSocket(&server_socket);

        threadpool* pool = create_threadpool(sPoolSize);

        struct sockaddr_in cli;
        int new_sockfd;
        int cli_length = sizeof(cli);


        if((new_sockfd = accept(server_socket, (struct sockaddr*) &cli, (socklen_t*) &cli_length)) < 0) {
        	perror("accept");
                exit(1);
        }

        //TODO
        // readRequest()
        //
        // writeResponse();

        return 0;
}

/*********************************/
/*********************************/
/*********************************/

void initServerSocket(int* sockfd) {

        if(((*sockfd) = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
                perror("socket");
                exit(-1);
        }

        struct sockaddr_in srv;
        srv.sin_family = AF_INET;
        srv.sin_port = htons(sPort);
        srv.sin_addr.s_addr = htonl(INADDR_ANY);

        if(bind((*sockfd), (struct sockaddr*) &srv, sizeof(srv)) < 0) {
	        perror("bind");
                exit(1);
        }

        //TODO change int backlog
        if(listen((*sockfd), 5) < 0) {
        	perror("listen");
        	exit(1);
        }


}
