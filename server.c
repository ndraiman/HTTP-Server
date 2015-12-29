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
char *get_mime_type(char*);

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

        char type_string[128];
        char location[128] = "";
        switch (type) {

        case code_ok:
                strcat(type_string, RESPONSE_OK);
                break;

        case code_found:
                strcat(type_string, RESPONSE_FOUND);
                sprintf(location, "Location: path + /"); //TODO replace with file path
                break;

        case code_bad:
                strcat(type_string, RESPONSE_BAD_REQUEST);
                break;

        case code_forbidden:
                strcat(type_string, RESPONSE_FORBIDDEN);
                break;

        case code_not_found:
                strcat(type_string, RESPONSE_NOT_FOUND);
                break;

        case code_server_error:
                strcat(type_string, RESPONSE_SERVER_ERROR);
                break;

        case code_not_supported:
                strcat(type_string, RESPONSE_NOT_SUPPORTED);
                break;

        }

        char response_type[128];
        sprintf(response_type, "HTTP/1.0 %s\r\n", type_string);

        char server_header[64] = "Server: webserver/1.0\r\n";

        //Get Date
        char date_string[256];
        char timebuf[128];
        time_t now;
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        //date_string holds the correct format of the current time.
        sprintf(date_string, "Date: %s\r\n", timebuf);


        char content_type[128];
        sprintf(content_type, "Content-Type: %s\r\n", get_mime_type("filename.ext")); //TODO replace with filename

        char content_length[128];
        sprintf(content_length, "Content-Length: %d\r\n", (int)strlen("response body")); //TODO replace with response body variable

        char last_modified[128];
        sprintf(last_modified, "Last Modified: %s\r\n", "last modification date"); //TODO replace with modification date

        char connection[64] = "Connection: close\r\n\r\n";
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


char *get_mime_type(char *name)
{
        char *ext = strrchr(name, '.');
        if (!ext)
                return NULL;

        if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
                return "text/html";
        if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
                return "image/jpeg";
        if (strcmp(ext, ".gif") == 0)
                return "image/gif";
        if (strcmp(ext, ".png") == 0)
                return "image/png";
        if (strcmp(ext, ".css") == 0)
                return "text/css";
        if (strcmp(ext, ".au") == 0)
                return "audio/basic";
        if (strcmp(ext, ".wav") == 0)
                return "audio/wav";
        if (strcmp(ext, ".avi") == 0)
                return "video/x-msvideo";
        if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0)
                return "video/mpeg";
        if (strcmp(ext, ".mp3") == 0)
                return "audio/mpeg";

        return NULL;
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
