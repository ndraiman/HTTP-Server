#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include "threadpool.h"

#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
#define MAX_REQUEST_LENGHT 4000
#define MAX_ENTITY_LINE 500
#define MAX_PORT 65535
#define NUM_OF_COMMANDS 4
#define PRINT_WRONG_CMD_USAGE "Usage: server <port> <pool-size> <max-number-of-request>\n"

/***** Response Codes *****/
const int code_ok = 200;
const int code_found = 302;
const int code_bad = 400;
const int code_forbidden = 403;
const int code_not_found = 404;
const int code_server_error = 500;
const int code_not_supported = 501;

#define RESPONSE_OK "200 OK\r\n"
#define RESPONSE_FOUND "302 Found\r\n"
#define RESPONSE_BAD_REQUEST "400 Bad Request\r\n"
#define RESPONSE_FORBIDDEN "403 Forbidden\r\n"
#define RESPONSE_NOT_FOUND "404 Not Found\r\n"
#define RESPONSE_SERVER_ERROR "500 Internal Server Error\r\n"
#define RESPONSE_NOT_SUPPORTED "501 Not Supported\r\n"

static int sPort = 0;
static int sPoolSize = 0;
static int sMaxRequests = 0;

int parseArguments(int, char**);
int verifyPort(char*);
int initServer();
void initServerSocket(int*);

char* readRequest(int*);

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

char* constructResponse(int type) {

        char response_type[128] = "HTTP/1.0 ";
        switch (type) {

                case code_ok:
                strcat(response_type, RESPONSE_OK);
                break;

                case code_found:
                strcat(response_type, RESPONSE_FOUND);
                break;

                case code_bad:
                strcat(response_type, RESPONSE_BAD_REQUEST);
                break;

                case code_forbidden:
                strcat(response_type, RESPONSE_FORBIDDEN);
                break;

                case code_not_found:
                strcat(response_type, RESPONSE_NOT_FOUND);
                break;

                case code_server_error:
                strcat(response_type, RESPONSE_SERVER_ERROR);
                break;

                case code_not_supported:
                strcat(response_type, RESPONSE_NOT_SUPPORTED);
                break;

        }

        char server_header[128] = "Server: webserver/1.0\r\n";

        //Get Date
        char date_string[128];
        time_t now;
        now = time(NULL);
        strftime(date_string, sizeof(date_string), RFC1123FMT, gmtime(&now));
        //date_string holds the correct format of the current time.
        strcat(date_string, "\r\n");

        //TODO continue implementation

}

int initServer() {

        int server_socket = 0;
        initServerSocket(&server_socket);

        threadpool* pool = create_threadpool(sPoolSize);

        struct sockaddr_in cli;
        int new_sockfd;
        int cli_length = sizeof(cli);

        int i;
        for(i = 0; i < sMaxRequests; i++) {

                if((new_sockfd = accept(server_socket, (struct sockaddr*) &cli, (socklen_t*) &cli_length)) < 0) {
                	perror("accept");
                        exit(1);
                }

                //TODO
                char* request = readRequest(&new_sockfd);
                //
                // writeResponse();

                // close(new_sockfd);
        }
        return 0;
}

char* readRequest(int* sockfd) {

        return NULL;
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

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
