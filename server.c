#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include "threadpool.h"

#define DEBUG 1 //TODO disable debug prints
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
#define SIZE_HEADER 64
#define SIZE_DATE_BUFFER 128
#define SIZE_HTML_TAGS 64
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
static char* sLocationPath = NULL;
static int sIsPathDir = 0;
static int sFoundFile = 0;
static struct dirent** sFileList = NULL;
static int sNumOfFiles = 0;



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
int readRequest(char**, int, int*, int);
int parseRequest(char**);
int parsePath();
int hasPermissions(struct stat*);

//Response Handling
int sendResponse(int*, int);
int constructResponse(int, char**);
int getResponseBody(int, char**);
int getPathBody(char**, int, char**, int);
char* get_mime_type(char*);
int writeResponse(int*, char**);

void freeGlobalVars();

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

        //TODO should backlog be sMaxRequests?
        if(listen((*sockfd), sMaxRequests) < 0) {
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
        if(!sockfd)
                return -1;

        char* request = (char*)calloc(SIZE_REQUEST, sizeof(char));
        if(!request) {
                sendResponse(sockfd, CODE_INTERNAL_ERROR);
                freeGlobalVars();
                return -1;
        }

        if(readRequest(&request, SIZE_REQUEST, sockfd, 1)) {
                sendResponse(sockfd, CODE_INTERNAL_ERROR);
                free(request);
                freeGlobalVars();
                return -1;
        }
        debug_print("Request = \n%s\n", request);

        sPath = (char*)calloc(strlen(request), sizeof(char));
        if(!sPath) {
                sendResponse(sockfd, CODE_INTERNAL_ERROR);
                free(request);
                freeGlobalVars();
                return -1;
        }

        int parserRetVal;
        if((parserRetVal = parseRequest(&request)) || (parserRetVal = parsePath())) {
                debug_print("something failed, parserRetVal = %d\n", parserRetVal);
                sendResponse(sockfd, parserRetVal);
                free(request);
                freeGlobalVars();
                return -1;
        }

        sendResponse(sockfd, CODE_OK);
        //TODO what to do if sending response fails?

        //Free Memory
        free(request);
        freeGlobalVars();
        return 0;
}

/******************************************************************************/
/******************************************************************************/
/*************************** Request Methods **********************************/
/******************************************************************************/
/******************************************************************************/

//returns 0 on success, -1 on failure
//isSocket - to differentiate between reading server socket or file.
int readRequest(char** request, int request_length, int* sockfd, int isSocket) {
        debug_print("%s\n", "readRequest");

        int nBytes;
        char buffer[SIZE_BUFFER + 1];
        memset(buffer, 0, sizeof(buffer));
        int bytes_read = 0;

        char* temp;

        while((nBytes = read((*sockfd), buffer, SIZE_BUFFER)) > 0) {

                if(nBytes < 0) {
                        debug_print("\t%s\n", "reading request failed");
                        return -1;
                }

                bytes_read += nBytes;


                if(nBytes >= (request_length - bytes_read)) {

                        temp = (char*)realloc((*request), (request_length *= 2));
                        if(temp == NULL)
                                return -1;

                        (*request) = temp;
                }
                strncat((*request), buffer, nBytes);

                //Server implementation reads only first line of the request.
                if(isSocket && strchr(buffer, '\r'))
                        break;
        }
        debug_print("\tbytes read = %d\n", bytes_read);
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
        //"host[:port]/path" - without http://
        if(!strncmp(sPath, "http", 4)) {

                debug_print("\t%s\n", "path containts http");
                char* temp = (char*)calloc(strlen(sPath) + 1, sizeof(char));
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
        // free(sPath); //free previously allocated path

        free(rootPath); //free memory allocated by getcwd
        sLocationPath = sPath;
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


                sNumOfFiles = scandir(sPath, &sFileList, NULL, alphasort);
                if(sNumOfFiles < 0)
                        return CODE_INTERNAL_ERROR;

                debug_print("\tPrinting scandir retval, numOfFiles = %d\n", sNumOfFiles);
                for(i = 0; i < sNumOfFiles; i++) {
                        debug_print("\t%s [%d]\n", sFileList[i]->d_name, i);
                        if(!strcmp(sFileList[i]->d_name, DEFAULT_FILE)) {

                                sFoundFile = 1;
                                break;
                        }
                }

                debug_print("\tsFoundFile = %d\n", sFoundFile);

                //concat DEFAULT_FILE to path
                if(sFoundFile) {

                        debug_print("\t%s\n", "adding to path");
                        char* tempPath = (char*)calloc(strlen(sPath) + strlen(DEFAULT_FILE) + 1, sizeof(char));
                        if(!tempPath)
                                return CODE_INTERNAL_ERROR;
                        strcat(tempPath, sPath);
                        strcat(tempPath, DEFAULT_FILE);
                        free(sPath);
                        sPath = tempPath;

                        debug_print("\tpath is now: %s\n", sPath);
                }

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
        debug_print("sendResponse - %d\n", type);
        //FIXME if sending the response fails?

        char* response = (char*)calloc(SIZE_RESPONSE, sizeof(char));
        if(!response)
                return -1;

        if(constructResponse(type, &response) || writeResponse(sockfd, &response)) {
                free(response);
                return -1;
        }

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
        char server_header[SIZE_HEADER] = "Server: webserver/1.0\r\n";
        char connection[SIZE_HEADER] = "Connection: close\r\n\r\n";
        char type_string[SIZE_HEADER/2] = "";

        char* location = (char*)calloc(SIZE_HEADER, sizeof(char));
        char* responseBody = (char*)calloc(SIZE_RESPONSE_BODY, sizeof(char));
        if(!location || !responseBody) {
                free(location);
                free(responseBody);
                return -1;
        }


        switch (type) {

        case CODE_OK:
                strcat(type_string, RESPONSE_OK);
                break;

        case CODE_FOUND:
                strcat(type_string, RESPONSE_FOUND);
                int loc_header_len = (int)strlen("Location: s\r\n");
                int path_length = strlen(sLocationPath);

                if(path_length >= SIZE_HEADER - loc_header_len) {

                        char* temp = (char*)realloc(location, path_length + loc_header_len + 1);
                        if(!temp) {
                                free(location);
                                free(responseBody);
                                return -1;
                        }
                        location = temp;

                }
                sprintf(location, "Location: %s/\r\n", sLocationPath);
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

        char response_type[SIZE_HEADER];
        sprintf(response_type, "HTTP/1.0 %s\r\n", type_string);


        //Get Date
        char date_string[SIZE_HEADER + SIZE_DATE_BUFFER];
        char timebuf[SIZE_DATE_BUFFER];
        time_t now;
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        //date_string holds the correct format of the current time.
        sprintf(date_string, "Date: %s\r\n", timebuf);

        debug_print("sIsPathDir = %d\n", sIsPathDir);
        char content_type[SIZE_HEADER];
        sprintf(content_type,
                "Content-Type: %s\r\n",
                sIsPathDir || !(type == CODE_OK) ? get_mime_type(DEFAULT_FILE) : get_mime_type(strrchr(sPath, '/')));



        if(getResponseBody(type, &responseBody)) {
                free(location);
                free(responseBody);
                return -1;
        }


        char content_length[SIZE_HEADER + strlen(responseBody) + 1];
        sprintf(content_length, "Content-Length: %d\r\n", (int)strlen(responseBody));


        char last_modified[SIZE_HEADER + SIZE_DATE_BUFFER] = "";
        if(type == CODE_OK) {

                struct stat statBuff;
                if(stat(sPath, &statBuff))
                        return -1;
                char timebuf[SIZE_DATE_BUFFER];
                strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&statBuff.st_mtime));
                sprintf(last_modified, "Last-Modified: %s\r\n", timebuf);
        }


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
                if(!temp) {
                        free(location);
                        free(responseBody);
                        return -1;
                }
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
        return 0;
}


/*********************************/
/*********************************/
/*********************************/
//return 0 on success, -1 on failure
int getResponseBody(int type, char** responseBody) {
        debug_print("\t%s\n", "getResponseBody");


        char* title = (char*)calloc(128, sizeof(char));
        char* body = (char*)calloc(128, sizeof(char));
        if(!title || !body) {
                free(title);
                free(body);
                return -1;
        }

        switch (type) {

        case CODE_OK:
                if(getPathBody(&title, 128, &body, 128)) {
                        free(title);
                        free(body);
                        return -1;
                }
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
        int length = 2*strlen(title) + strlen(body) + SIZE_HTML_TAGS;
        if(SIZE_RESPONSE_BODY < length) {
                debug_print("\t\treallocing responseBody from %d to %d\n", (int)strlen(*responseBody), length);
                temp = (char*)realloc(responseBody, length + 1);
                if(!temp) {
                        free(title);
                        free(body);
                        return -1;
                }
                (*responseBody) = temp;
        }


        sprintf((*responseBody),
                "<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n<BODY>\n<H4>%s</H4>\n%s\n</BODY>\n</HTML>\n",
                title, title, body);

        free(title);
        free(body);
        return 0;
}

/*********************************/
/*********************************/
/*********************************/

int getPathBody(char** title, int title_len, char** body, int body_len) {
        debug_print("getPathBody\n\tpath = %s\n", sPath);


        if(sIsPathDir && !sFoundFile) { //get dir contents

                debug_print("\t%s\n", "reading dir");
                int i;
                char* temp;

                if((title_len - (int)strlen(sPath)) < 0) {
                        temp = (char*)realloc(*title, (title_len += (strlen(sPath) + 1)));
                        if(!temp)
                                return -1;
                        *title = temp;
                }
                sprintf(*title, "Index of %s", sPath);

                if(body_len <= SIZE_DIR_ENTITY * sNumOfFiles) {
                        temp = (char*)realloc(*body, (body_len += (SIZE_DIR_ENTITY * sNumOfFiles)));
                        if(!temp)
                                return -1;
                        *body = temp;

                }
                strcat(*body, "<table CELLSPACING=8>\n<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\n");

                for(i = 0; i < sNumOfFiles; i++) {

                        if(!strcmp(sFileList[i]->d_name, ".") || !strcmp(sFileList[i]->d_name, ".."))
                                continue;

                        char tempPath[strlen(sPath) + strlen(sFileList[i]->d_name) + 1];
                        memset(tempPath, 0, sizeof(tempPath));
                        strcat(tempPath, sPath);
                        strcat(tempPath, sFileList[i]->d_name);
                        debug_print("tempPath = %s\n", tempPath);

                        struct stat statBuff;
                        if(stat(tempPath, &statBuff))
                                return -1;
                        char timebuf[SIZE_DATE_BUFFER];
                        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&statBuff.st_mtime));


                        //FIXME uncomment if entity 500 bytes limit does not include html tags and mod date
                        // char entity[SIZE_DIR_ENTITY + 2*strlen(sFileList[i]->d_name) + SIZE_DATE_BUFFER];
                        char entity[SIZE_DIR_ENTITY];
                        sprintf(entity, "<tr><td><A HREF=\"%s\">%s</A></td><td>%s</td>",
                                sFileList[i]->d_name,
                                sFileList[i]->d_name,
                                timebuf);

                        if(S_ISDIR(statBuff.st_mode)) {
                                debug_print("%s\n", "dir - not file size");
                                strcat(entity, "<td></td></tr>\n");

                        } else {
                                debug_print("%s\n", "getting file size");
                                char fileSize[64];
                                sprintf(fileSize, "<td>%ld</td></tr>\n", statBuff.st_size);
                                strcat(entity, fileSize);

                        }
                        strcat(*body, entity);
                }

                strcat(*body, "</table>\n<HR>\n<ADDRESS>webserver/1.0</ADDRESS>\n");



        } else { //get file content

                debug_print("\t%s\n", "reading file");
                (*title)[0] = 0;
                int fd = open(sPath, O_RDONLY);
                if(fd < 0 || readRequest(body, body_len, &fd, 0))
                        return -1;

                debug_print("\ttitle = %s\n\tbody = \n%s\n**********\n", *title, *body);
                close(fd);
        }

        debug_print("%s\n", "getPathBody END");
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

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

void freeGlobalVars() {

        if(sPath)
                free(sPath);

        if(sLocationPath)
                free(sLocationPath);

        if(sFileList) {
                int i;
                for(i = 0; i < sNumOfFiles; i++)
                        free(sFileList[i]);
                free(sFileList);
        }
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
