#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/stat.h>
#include <dirent.h>
#include "threadpool.h"

#define DEBUG 1
#define debug_print(fmt, ...) \
           do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)


/***********************************/
/***** Input Validation Macros *****/
/***********************************/
#define MAX_REQUEST_LENGTH 4000
#define MAX_ENTITY_LINE 500
#define MAX_PORT 65535
#define NUM_OF_COMMANDS 4
#define PRINT_WRONG_CMD_USAGE "Usage: server <port> <pool-size> <max-number-of-request>\n"

/****************************************/
/***** Response Construction Macros *****/
/****************************************/
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
#define NUM_OF_EXPECTED_TOKENS 3
#define SIZE_BUFFER 2
#define SIZE_REQUEST 64
#define SIZE_RESPONSE 2048
#define SIZE_RESPONSE_BODY 1024
#define SIZE_DIR_ENTITY 500
#define COLS_DIR_CONTENTS 3
#define DEFAULT_FILE "index.html"

/**************************/
/***** Response Codes *****/
/**************************/
#define CODE_OK 200
#define CODE_FOUND 302
#define CODE_BAD 400
#define CODE_FORBIDDEN 403
#define CODE_NOT_FOUND 404
#define CODE_INTERNAL_ERROR 500
#define CODE_NOT_SUPPORTED 501

/****************************/
/***** Response Strings *****/
/****************************/
#define RESPONSE_OK "200 OK"
#define RESPONSE_FOUND "302 Found"
#define RESPONSE_BAD_REQUEST "400 Bad Request"
#define RESPONSE_FORBIDDEN "403 Forbidden"
#define RESPONSE_NOT_FOUND "404 Not Found"
#define RESPONSE_INTERNAL_ERROR "500 Internal Server Error"
#define RESPONSE_NOT_SUPPORTED "501 Not Supported"

/****************************/
/***** Static Variables *****/
/****************************/
static int sPort = 0;
static int sPoolSize = 0;
static int sMaxRequests = 0;
static char* sPath = NULL;
static int sIsPathDir = 0;
static int sFoundFile = 0;
static struct dirent** sFileList = NULL; //TODO free FileList



/*******************************/
/***** Method Declarations *****/
/*******************************/
//Server Initialization
int parseArguments(int, char**);
int verifyPort(char*);
int initServer();
void initServerSocket(int*);

//Request Handling
int handler(void*);
int readRequest(char**, int, int*);
int parseRequest(char**);
int parsePath();
int hasPermissions(struct stat*);

//Response Handling
int sendResponse(int*, int);
int constructResponse(int, char**);
int getResponseBody(int, char**);
char* get_mime_type(char*);
int writeResponse(int*, char**);

/******************************************************************************/
/******************************************************************************/
/***************************** Main Method ************************************/
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
/************************ Initialize Server Methods ***************************/
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

/*********************************/
/*********************************/
/*********************************/

int initServer() {
        debug_print("%s\n", "initServer");
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

                dispatch(pool, handler, (void*)&new_sockfd);

        }

        //TODO free memory
        close(server_socket);
        destroy_threadpool(pool);
        return 0;
}

/*********************************/
/*********************************/
/*********************************/

void initServerSocket(int* sockfd) {
        debug_print("\t%s\n", "initServerSocket");
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
/*********************** Handler Method - Thread ******************************/
/******************************************************************************/
/******************************************************************************/

int handler(void* arg) {
        debug_print("handler - tid = %d\n", (int)pthread_self());
        int* sockfd = (int*)(arg);

        char* request = (char*)calloc(SIZE_REQUEST, sizeof(char));

        if(readRequest(&request, SIZE_REQUEST, sockfd)) {
                sendResponse(sockfd, CODE_INTERNAL_ERROR);
                return -1;
        }
        debug_print("Request = \n%s\n", request);

        sPath = (char*)calloc(strlen(request), sizeof(char));
        if(!sPath) {
                sendResponse(sockfd, CODE_INTERNAL_ERROR);
                return -1;
        }

        int parserRetVal;
        if((parserRetVal = parseRequest(&request)) || (parserRetVal = parsePath())) {
                sendResponse(sockfd, parserRetVal);
                return -1;
        }

        sendResponse(sockfd, CODE_OK);
        //TODO what to do if sending response fails?
        free(sFileList);
        return 0;
}

/******************************************************************************/
/******************************************************************************/
/*************************** Request Methods **********************************/
/******************************************************************************/
/******************************************************************************/

//returns 0 on success, -1 on failure
int readRequest(char** request, int request_length, int* sockfd) {
        debug_print("%s\n", "readRequest");
        if((*request) == NULL)
                return -1;

        int nBytes;
        char buffer[SIZE_BUFFER];
        memset(&buffer, 0, sizeof(buffer));
        int bytes_read = 0;

        char* temp;

        while((nBytes = read((*sockfd), buffer, sizeof(buffer))) > 0) {

                if(nBytes < 0)
                        return -1;

                bytes_read += nBytes;

                if(nBytes >= (request_length - bytes_read)) {

                        temp = (char*)realloc((*request), (request_length *= 2));
                        if(temp == NULL)
                                return -1;

                        (*request) = temp;
                }
                strncat((*request), buffer, nBytes);

                //Server implementation reads only first line of the request.
                if(strchr(buffer, '\r'))
                        return 0;
        }
        return 0;
}

/*********************************/
/*********************************/
/*********************************/

//returns 0 on success, error number on failure
int parseRequest(char** request) {
        debug_print("%s\n", "parseRequest");
        char method[4];
        char protocol[64];

        int assigned = sscanf((*request), "%4s %s %8s", method, sPath, protocol);
        debug_print("\tassigned = %d\n", assigned);
        if(assigned != NUM_OF_EXPECTED_TOKENS)
                return CODE_BAD;

        if(strcmp(method, "GET"))
                return CODE_NOT_SUPPORTED;

        //extract path from HTTP/1.0 requests
        if(!strncmp(sPath, "http", 4)) {

                debug_print("\t%s\n", "path containts http");
                char* temp = (char*)calloc(strlen(sPath), sizeof(char));
                if(!temp)
                        return CODE_INTERNAL_ERROR;
                strcat(temp, strchr(&sPath[7], '/'));
                free(sPath);
                sPath = temp;
                debug_print("\tcorrected sPath = %s\n", temp);
        }
        return 0;
}

/*********************************/
/*********************************/
/*********************************/

//returns 0 on success, error number on failure
int parsePath() {
        debug_print("%s\n", "parsePath");
        int i;
        sFoundFile = 0;

        //make sPath hold absolute path
        char* rootPath = getcwd(NULL, 0);
        debug_print("\trootPath = %s\n\tsPath = %s\n", rootPath, sPath);

        char* tempPath = (char*)calloc(strlen(rootPath) + strlen(sPath) + 1, sizeof(char));
        if(!tempPath)
                return CODE_INTERNAL_ERROR;

        strcat(tempPath, rootPath);
        strcat(tempPath, sPath);
        debug_print("\ttempPath = %s\n", tempPath);
        free(sPath); //free previously allocated path
        free(rootPath); //free memory allocated by getcwd
        sPath = tempPath;
        debug_print("\tsPath = %s\n", sPath);

        //Check path exists
        struct stat pathStats;
        if(stat(sPath, &pathStats)) {
                debug_print("\t%s\n", "stat return -1");
                return CODE_NOT_FOUND;
        }

        //Check if path is file or directory
        if(S_ISDIR(pathStats.st_mode)) {

                sIsPathDir = 1;
                debug_print("\t%s\n", "path is dir");

        } else {
                sIsPathDir = 0;
        }


        if(sIsPathDir) {

                if(sPath[strlen(sPath) - 1] != '/')
                        return CODE_FOUND;


                int numOfFiles = scandir(sPath, &sFileList, NULL, alphasort);
                perror("scandir error"); //TODO DEBUG
                if(numOfFiles < 0)
                        return CODE_INTERNAL_ERROR;

                debug_print("\tPrinting scandir retval, numOfFiles = %d\n", numOfFiles);
                for(i = 0; i < numOfFiles; i++) {
                        debug_print("\t%s [%d]\n", sFileList[i] -> d_name, i);
                        if(!strcmp(sFileList[i]->d_name, DEFAULT_FILE)) {

                                sFoundFile = 1;
                                break;
                        }
                }

                debug_print("\tsFoundFile = %d\n", sFoundFile);

                // if(!sFoundFile) {
                //
                //
                //         numOfFiles -= 2; //ignores "." & ".." directories
                //         char*** dir_contents = (char***)calloc(numOfFiles + 1, sizeof(char**));
                //
                //         if(!dir_contents)
                //                 return CODE_INTERNAL_ERROR;
                //
                //         int j;
                //         for(i = 0; i < numOfFiles; i++) {
                //
                //                 j = i + 2;
                //
                //                 char tempPath[strlen(sPath) + strlen(sFileList[j]->d_name) + 1];
                //                 strcat(tempPath, sPath);
                //                 strcat(tempPath, sFileList[j]->d_name);
                //                 debug_print("tempPath = %s\n", tempPath);
                //                 struct stat statBuff;
                //                 //if d_type is DIR, no need for size column
                //                 char** entity = (char**)calloc(
                //                         COLS_DIR_CONTENTS + 1,
                //                         sizeof(char*));
                //
                //                 if(!entity || stat(tempPath, &statBuff))
                //                         return CODE_INTERNAL_ERROR;
                //
                //
                //                 char* name = (char*)calloc(strlen(sFileList[j]->d_name), sizeof(char));
                //                 char* last_mod = (char*)calloc(128, sizeof(char));
                //                 char* size = NULL;
                //
                //                 if(S_ISDIR(statBuff.st_mode)) {
                //                         size = (char*)calloc(16, sizeof(char));
                //                         if(!size)
                //                                 return CODE_INTERNAL_ERROR;
                //                 }
                //
                //                 if(!name || !last_mod)
                //                         return CODE_INTERNAL_ERROR;
                //
                //                 strftime(last_mod, sizeof(last_mod), RFC1123FMT, gmtime(&statBuff.st_mtime));
                //                 strcat(name, sFileList[j]->d_name);
                //                 printf("im here\n");
                //                 if(!size) {
                //                         sprintf(size, "%d", 2);
                //                 }
                //
                //                 entity[0] = name;
                //                 entity[1] = last_mod;
                //                 entity[2] = size;
                //                 entity[3] = NULL;
                //
                //                 dir_contents[i] = entity;
                //         }
                //         debug_print("i = %d\n", i);
                //         dir_contents[i] = NULL;
                //         debug_print("%s\n", "done building dir_contents");
                //         //TODO DEBUG
                //         int k;
                //         for(i = 0; dir_contents[i] != NULL; i++) {
                //                 for(k = 0; dir_contents[i][k] != NULL; k++) {
                //                         printf("%s", dir_contents[i][k]);
                //                 }
                //                 printf("\n");
                //         }
                // }


                // if(sFoundFile) {
                //
                //         //TODO send index.html
                //
                // } else {
                //
                //         //TODO send dir_contents
                // }

                //TODO might not need above condition
                //return 0 & use IsDir & FoundFile to determine action

                // return 0;

        } else { //path is file

                if(!S_ISREG(pathStats.st_mode) || !hasPermissions(&pathStats)) {

                        return CODE_FORBIDDEN;
                }

                // return 0;
        }

        debug_print("%s\n", "parsePath END");
        return 0;
}

/*********************************/
/*********************************/
/*********************************/

//returns if file has read permissions for everyone (owner, grp, others)
int hasPermissions(struct stat* fileStats) {

        return (fileStats->st_mode & S_IRUSR)
                && (fileStats->st_mode & S_IRGRP)
                && (fileStats->st_mode & S_IROTH);
}

/******************************************************************************/
/******************************************************************************/
/***************************** Response Methods *******************************/
/******************************************************************************/
/******************************************************************************/

//returns 0 on success, -1 on failure
int sendResponse(int* sockfd, int type) {
        debug_print("%s\n", "sendResponse");
        //FIXME if sending the response fails?

        char* response = (char*)calloc(SIZE_RESPONSE, sizeof(char));
        if(!response)
                return -1;

        if(constructResponse(type, &response) || writeResponse(sockfd, &response))
                return -1;

        debug_print("%s\n", "sendResponse END");
        free(response);
        close(*sockfd);
        return 0;
}

/*********************************/
/*********************************/
/*********************************/

//returns 0 on success, -1 on failure
int constructResponse(int type, char** response) {
        debug_print("%s\n", "constructResponse");
        char server_header[64] = "Server: webserver/1.0\r\n";
        char connection[64] = "Connection: close\r\n\r\n";

        char type_string[32] = "";
        char* location = (char*)calloc(64, sizeof(char));
        if(!location)
                return -1;

        switch (type) {

        case CODE_OK:
                strcat(type_string, RESPONSE_OK);
                break;

        case CODE_FOUND:
                strcat(type_string, RESPONSE_FOUND);
                if(strlen(sPath) <= 64) {
                        char* temp = (char*)realloc(location, strlen(sPath) + 16);
                        if(!temp)
                                return -1;
                        location = temp;
                }
                sprintf(location, "Location: %s\r\n", sPath);
                break;

        case CODE_BAD:
                strcat(type_string, RESPONSE_BAD_REQUEST);
                break;

        case CODE_FORBIDDEN:
                strcat(type_string, RESPONSE_FORBIDDEN);
                break;

        case CODE_NOT_FOUND:
                strcat(type_string, RESPONSE_NOT_FOUND);
                break;

        case CODE_INTERNAL_ERROR:
                strcat(type_string, RESPONSE_INTERNAL_ERROR);
                break;

        case CODE_NOT_SUPPORTED:
                strcat(type_string, RESPONSE_NOT_SUPPORTED);
                break;

        }

        char response_type[64];
        sprintf(response_type, "HTTP/1.0 %s\r\n", type_string);


        //Get Date
        char date_string[256];
        char timebuf[128];
        time_t now;
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        //date_string holds the correct format of the current time.
        sprintf(date_string, "Date: %s\r\n", timebuf);


        char content_type[128];
        sprintf(content_type,
                "Content-Type: %s\r\n",
                sIsPathDir ? get_mime_type(DEFAULT_FILE) : get_mime_type(strrchr(sPath, '/')));


        char* responseBody = (char*)calloc(SIZE_RESPONSE_BODY, sizeof(char));
        if(!responseBody || getResponseBody(type, &responseBody))
                return -1;

        char content_length[128];
        sprintf(content_length, "Content-Length: %d\r\n", (int)strlen(responseBody));

        //FIXME if type is OK
        char last_modified[128] = "";
        // sprintf(last_modified, "Last Modified: %s\r\n", "last modification date"); //FIXME replace with modification date


        int length = strlen(response_type)
                + strlen(server_header)
                + strlen(date_string)
                + strlen(location)
                + strlen(content_type)
                + strlen(content_length)
                + strlen(last_modified)
                + strlen(connection)
                + strlen(responseBody);

        if(strlen(*response) <= length) {
                char* temp = (char*)realloc((*response), length + 1);
                if(!temp)
                        return -1;
                (*response) = temp;
        }
        sprintf(*response, "%s%s%s%s%s%s%s%s%s",
                response_type,
                server_header,
                date_string,
                location,
                content_type,
                content_length,
                last_modified,
                connection,
                responseBody);

        free(location);
        free(responseBody);
        debug_print("response = \n\n%s\n", *response);
        return 0;
}


/*********************************/
/*********************************/
/*********************************/
//return 0 on success, -1 on failure
int getResponseBody(int type, char** responseBody) {
        debug_print("\t%s\n", "getResponseBody");


        char title[128] = "";
        char body[128] = "";

        switch (type) {

                case CODE_OK:
                        //TODO depends on dir or file
                        strcat(title, "OK placeholder");
                        strcat(body, "OK placeholder");
                        break;

                case CODE_FOUND:
                        strcat(title, RESPONSE_FOUND);
                        strcat(body, "Directories must end with a slash.");
                        break;

                case CODE_BAD:
                        strcat(title, RESPONSE_BAD_REQUEST);
                        strcat(body, "Bad Request.");
                        break;

                case CODE_FORBIDDEN:
                        strcat(title, RESPONSE_FORBIDDEN);
                        strcat(body, "Access denied.");
                        break;

                case CODE_NOT_FOUND:
                        strcat(title, RESPONSE_NOT_FOUND);
                        strcat(body, "File not found.");
                        break;

                case CODE_INTERNAL_ERROR:
                        strcat(title, RESPONSE_INTERNAL_ERROR);
                        strcat(body, "Some server side error.");
                        break;

                case CODE_NOT_SUPPORTED:
                        strcat(title, RESPONSE_NOT_SUPPORTED);
                        strcat(body, "Method is not supported.");
                        break;
        }


        char* temp;
        int length = 2*strlen(title) + strlen(body) + 64; //64 is approx size of all the html tags
        if(SIZE_RESPONSE_BODY < length) {
                debug_print("\t\treallocing responseBody from %d to %d\n", (int)strlen(*responseBody), length);
                temp = (char*)realloc(responseBody, length);
                if(!temp)
                        return -1;
                (*responseBody) = temp;
        }


        sprintf((*responseBody),
                "<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n<BODY>\n<H4>%s</H4>\n%s\n</BODY>\n</HTML>\n",
                title, title, body);

        return 0;
}

/*********************************/
/*********************************/
/*********************************/

char* get_mime_type(char* name) {

        debug_print("\t%s\n", "get_mime_type");
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

/*********************************/
/*********************************/
/*********************************/

int writeResponse(int* sockfd, char** response) {
        debug_print("%s\n", "writeResponse");
        int response_length = strlen(*response);
        int bytes_written = 0;
        int nBytes;

        debug_print("response length = %d\nresponse: \n%s\n", response_length, *response);


        while(bytes_written < response_length) {

                if((nBytes = write((*sockfd), *response, strlen(*response))) < 0) {
                        //FIXME what response to send?
                        debug_print("%s\n", "writing response failed");
                        return -1;
                }

                bytes_written += nBytes;
        }

        return 0;
}
