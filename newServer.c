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
#define COLS_DIR_CONTENTS 3
#define DEFAULT_FILE "index.html"

#define SIZE_BUFFER 512
#define SIZE_REQUEST 4000
#define SIZE_RESPONSE 2048
#define SIZE_RESPONSE_BODY 1024
#define SIZE_HEADER 64
#define SIZE_DATE_BUFFER 128
#define SIZE_HTML_TAGS 128
#define SIZE_DIR_ENTITY 500

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

/*********************************/
/***** Response Code Strings *****/
/*********************************/
#define CODE_OK_STRING "200 OK"
#define CODE_FOUND_STRING "302 Found"
#define CODE_BAD_STRING "400 Bad Request"
#define CODE_FORBIDDEN_STRING "403 Forbidden"
#define CODE_NOT_FOUND_STRING "404 Not Found"
#define CODE_INTERNAL_ERROR_STRING "500 Internal Server Error"
#define CODE_NOT_SUPPORTED_STRING "501 Not Supported"


/************************************/
/***** Response Message Strings *****/
/************************************/
#define RESPONSE_FOUND "Directories must end with a slash.\n"
#define RESPONSE_BAD_REQUEST "Bad Request.\n"
#define RESPONSE_FORBIDDEN "Access denied.\n"
#define RESPONSE_NOT_FOUND "File not found.\n"
#define RESPONSE_INTERNAL_ERROR "Some server side error.\n"
#define RESPONSE_NOT_SUPPORTED "Method is not supported.\n"
#define RESPONSE_BODY_TEMPLATE "<HTML>\n<HEAD>\n<TITLE>%s</TITLE>\n</HEAD>\n<BODY>\n<H4>%s</H4>\n%s\n</BODY>\n</HTML>\n"

#define DIR_CONTENTS_TITLE "Index of %s"

/****************************/
/***** Static Variables *****/
/****************************/
static int sPort = 0;
static int sPoolSize = 0;
static int sMaxRequests = 0;
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
int readRequest(char*, int*);
int parseRequest(char*, char*);
int parsePath(char*);
int hasPermissions(struct stat*);

//Response Handling
int sendResponse(int*, int, char*);
char* constructResponse(int, char*);
char* getResponseBody(int);
char* getDirContents(char*);
char* get_mime_type(char*);
int writeResponse(int*, char**);

void freeFileList();

int replaceSubstring(char*, char*, char*);

/******************************************************************************/
/******************************************************************************/
/***************************** Main Method ************************************/
/******************************************************************************/
/******************************************************************************/

int main(int argc, char* argv[]) {

        if(argc != NUM_OF_COMMANDS) {
                printf(PRINT_WRONG_CMD_USAGE);
                exit(EXIT_FAILURE);
        }

        if(parseArguments(argc, argv)) {
                printf(PRINT_WRONG_CMD_USAGE);
                exit(EXIT_FAILURE);
        }

        initServer(argc, argv);

        return EXIT_SUCCESS;
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

        int i;
        for(i = 0; i < sMaxRequests; i++) {

                int* new_sockfd = (int*)calloc(1, sizeof(int));
                if(!new_sockfd) {
                        perror("calloc");
                        close(server_socket);
                        destroy_threadpool(pool);
                        exit(EXIT_FAILURE);
                }

                // NULL - dont care about client's IP & Port
                if((*new_sockfd = accept(server_socket, NULL, NULL)) < 0) {
                        perror("accept");
                        close(server_socket);
                        destroy_threadpool(pool);
                        exit(EXIT_FAILURE);
                }

                dispatch(pool, handler, (void*)new_sockfd);

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

        sIsPathDir = 0;
        sFoundFile = 0;
        sFileList = NULL;
        sNumOfFiles = 0;

        int sockfd = *(int*)(arg);
        if(!sockfd)
                return -1;

        char request[SIZE_REQUEST];
        char path[SIZE_REQUEST];
        memset(request, 0, sizeof(request));
        memset(path, 0, sizeof(path));

        if(readRequest(request, &sockfd)) {
                sendResponse(&sockfd, CODE_INTERNAL_ERROR, NULL);
                close(sockfd);
                free(arg);
                return -1;
        }
        debug_print("request = %s\n", request);

        int parser_code;
        if((parser_code = parseRequest(request, path)) || (parser_code =  parsePath(path))) {
                sendResponse(&sockfd, parser_code, NULL);
                close(sockfd);
                free(arg);
                return -1;
        }
        debug_print("path = %s\n", path);

        sendResponse(&sockfd, CODE_OK, path); //TODO rewrite
        close(sockfd);
        free(arg);
        // char* request = (char*)calloc(SIZE_REQUEST, sizeof(char));
        // if(!request) {
        //         sendResponse(sockfd, CODE_INTERNAL_ERROR);
        //         freeFileList();
        //         return -1;
        // }
        //
        // if(readRequest(&request, SIZE_REQUEST, sockfd, 1)) {
        //         sendResponse(sockfd, CODE_INTERNAL_ERROR);
        //         free(request);
        //         freeFileList();
        //         return -1;
        // }
        // debug_print("Request = \n%s\n", request);
        //
        // sPath = (char*)calloc(strlen(request), sizeof(char));
        // if(!sPath) {
        //         sendResponse(sockfd, CODE_INTERNAL_ERROR);
        //         free(request);
        //         freeFileList();
        //         return -1;
        // }
        //
        // int parserRetVal;
        // if((parserRetVal = parseRequest(&request)) || (parserRetVal = parsePath())) {
        //         debug_print("something failed, parserRetVal = %d\n", parserRetVal);
        //         sendResponse(sockfd, parserRetVal);
        //         if(request)
        //                 free(request);
        //         freeFileList();
        //         return -1;
        // }
        //
        // if(sendResponse(sockfd, CODE_OK)) {
        //         fprintf(stderr, "sending response failed\n");
        //         return -1;
        // }
        //
        // //Free Memory
        // free(request);
        // freeFileList();
        return 0;
}

/******************************************************************************/
/******************************************************************************/
/*************************** Request Methods **********************************/
/******************************************************************************/
/******************************************************************************/

//returns 0 on success, -1 on failure
//isSocket - to differentiate between reading server socket or file.
int readRequest(char* request, int* sockfd) {
        debug_print("%s\n", "readRequest");

        int nBytes;
        char buffer[SIZE_BUFFER + 1];
        memset(buffer, 0, sizeof(buffer));
        int bytes_read = 0;

        while((nBytes = read((*sockfd), buffer, SIZE_BUFFER)) > 0) {

                if(nBytes < 0) {
                        debug_print("\t%s\n", "reading request failed");
                        return -1;
                }

                bytes_read += nBytes;
                strncat(request, buffer, nBytes);

                //Server implementation reads only first line of the request.
                if(strchr(buffer, '\r'))
                        break;
        }
        debug_print("\tbytes read = %d\n", bytes_read);
        return 0;
}

/*********************************/
/*********************************/
/*********************************/

//returns 0 on success, error number on failure
int parseRequest(char* request, char* path) {
        debug_print("%s\n", "parseRequest");
        char method[4];
        char protocol[64];

        //cut request at the first '\r' (replace with '\0')
        char* cut;
        if((cut = strchr(request, '\r')))
                cut[0] = '\0';

        int assigned = sscanf(request, "%4s %s %8s", method, path, protocol);
        debug_print("\tassigned = %d\n", assigned);
        if(assigned != NUM_OF_EXPECTED_TOKENS)
                return CODE_BAD;

        if(strcmp(method, "GET"))
                return CODE_NOT_SUPPORTED;

        if(strcmp(protocol, "HTTP/1.0") && strcmp(protocol, "HTTP/1.1"))
                return CODE_BAD;

        //extract path from HTTP/1.0 requests
        //"host[:port]/path" - without http://
        if(!strncmp(path, "http", 4)) {

                debug_print("\t%s\n", "path containts http");
                char temp[strlen(path)];
                memset(temp, 0, sizeof(temp));
                strcat(temp, path + strlen("http://"));
                debug_print("temp = %s\n", temp);
                memset(path, 0, strlen(path));
                strncpy(path, temp, strlen(temp));
        }
        return 0;
}

/*********************************/
/*********************************/
/*********************************/

//returns 0 on success, error number on failure
int parsePath(char* path) {
        debug_print("%s\n", "parsePath");
        int i;

        replaceSubstring(path, "%20", " ");
        debug_print("path = %s\n", path);

        //make sPath hold absolute path
        char* rootPath = getcwd(NULL, 0);
        if(!rootPath)
                return -1;

        char absPath[strlen(path) + strlen(rootPath) + 1];
        memset(absPath, 0, sizeof(absPath));
        strcat(absPath, rootPath);
        strcat(absPath, path);

        free(rootPath);
        debug_print("absPath = %s\n", absPath);

        //Check path exists
        struct stat pathStats;
        if(stat(absPath, &pathStats)) {
                debug_print("\t%s\n", "stat return -1");
                return CODE_NOT_FOUND;
        }

        //Check if path is file or directory
        if(S_ISDIR(pathStats.st_mode)) {

                sIsPathDir = 1;
                debug_print("\t%s\n", "path is dir");

        }


        if(sIsPathDir) {

                if(absPath[strlen(absPath) - 1] != '/')
                        return CODE_FOUND;


                sNumOfFiles = scandir(absPath, &sFileList, NULL, alphasort);
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
                // if(sFoundFile) {
                //
                //         debug_print("\t%s\n", "adding to path");
                //         char* tempPath = (char*)calloc(strlen(sPath) + strlen(DEFAULT_FILE) + 1, sizeof(char));
                //         if(!tempPath)
                //                 return CODE_INTERNAL_ERROR;
                //         strcat(tempPath, sPath);
                //         strcat(tempPath, DEFAULT_FILE);
                //         free(sPath);
                //         sPath = tempPath;
                //
                //         debug_print("\tpath is now: %s\n", sPath);
                // }

        } else { //path is file

                if(!S_ISREG(pathStats.st_mode) || access(absPath, R_OK)) {

                        return CODE_FORBIDDEN;
                }

                // return 0;
        }
        debug_print("absPath = %s\n", absPath);
        debug_print("%s\n", "parsePath END");
        return 0;
}

/******************************************************************************/
/******************************************************************************/
/***************************** Response Methods *******************************/
/******************************************************************************/
/******************************************************************************/

//returns 0 on success, -1 on failure
int sendResponse(int* sockfd, int type, char* path) {
        debug_print("sendResponse - %d\n", type);

        // char* response = (char*)calloc(SIZE_RESPONSE, sizeof(char));
        // if(!response)
        //         return -1;

        //sendHeader
        //sendBody
        //sendEndTags
        // if(constructResponse(type, path) || writeResponse(sockfd, &response)) {
        //         free(response);
        //         return -1;
        // }
        char* response = constructResponse(type, path);
        if(!response)
                return -1;

        debug_print("%s\n", "sendResponse END");
        // free(response);
        // close(*sockfd);
        return 0;
}

/*********************************/
/*********************************/
/*********************************/

//returns 0 on success, -1 on failure
char* constructResponse(int type, char* path) {
        debug_print("%s\n", "constructResponse");
        char server_header[SIZE_HEADER] = "Server: webserver/1.0\r\n";
        char connection[SIZE_HEADER] = "Connection: close\r\n\r\n";

        char location_header[SIZE_HEADER + strlen(path)];
        char type_string[SIZE_HEADER];
        char response_type[SIZE_HEADER];
        char date_string[SIZE_HEADER + SIZE_DATE_BUFFER];
        char timebuf[SIZE_DATE_BUFFER];
        char last_modified[SIZE_HEADER + SIZE_DATE_BUFFER];
        char content_length[SIZE_HEADER];
        char content_type[SIZE_HEADER];

        memset(type_string, 0, sizeof(type_string));
        memset(response_type, 0, sizeof(response_type));
        memset(location_header, 0, sizeof(location_header));
        memset(date_string, 0, sizeof(date_string));
        memset(timebuf, 0, sizeof(timebuf));
        memset(last_modified, 0, sizeof(last_modified));
        memset(content_length, 0, sizeof(content_length));
        memset(content_type, 0, sizeof(content_type));


        switch (type) {

        case CODE_OK:

                strcat(type_string, CODE_OK_STRING);
                break;

        case CODE_FOUND:
                strcat(type_string, CODE_FOUND_STRING);
                sprintf(location_header, "Location: %s/\r\n", path);
                break;

        case CODE_BAD:
                strcat(type_string, CODE_BAD_STRING);
                break;

        case CODE_FORBIDDEN:
                strcat(type_string, CODE_FORBIDDEN_STRING);
                break;

        case CODE_NOT_FOUND:
                strcat(type_string, CODE_NOT_FOUND_STRING);
                break;

        case CODE_INTERNAL_ERROR:
                strcat(type_string, CODE_INTERNAL_ERROR_STRING);
                break;

        case CODE_NOT_SUPPORTED:
                strcat(type_string, CODE_NOT_SUPPORTED_STRING);
                break;

        }


        sprintf(response_type, "HTTP/1.0 %s\r\n", type_string);


        //Get Date
        time_t now;
        now = time(NULL);
        strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));
        sprintf(date_string, "Date: %s\r\n", timebuf);

        debug_print("sIsPathDir = %d\n", sIsPathDir);
        char* mime = sIsPathDir || !(type == CODE_OK) ?
                     get_mime_type(DEFAULT_FILE) : get_mime_type(strrchr(path, '/'));
        if(mime)
                sprintf(content_type, "Content-Type: %s\r\n", mime);


        char* responseBody = NULL;

        if(type == CODE_OK) {

                struct stat statBuff;
                if(stat(path, &statBuff))
                        return NULL;

                if(!sIsPathDir || sFoundFile) {
                        sprintf(content_length, "Content-Length: %ld\r\n", statBuff.st_size);
                } else {
                        responseBody = getDirContents(path);
                        if(!responseBody)
                                return NULL;
                        sprintf(content_length, "Content-Length: %ld\r\n", strlen(responseBody));
                }


                strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&statBuff.st_mtime));
                sprintf(last_modified, "Last-Modified: %s\r\n", timebuf);

        } else {
                responseBody = getResponseBody(type);
                if(!responseBody)
                        return NULL;
                sprintf(content_length, "Content-Length: %ld\r\n", strlen(responseBody));
        }

        sprintf(content_length, "Content-Length: %ld\r\n", strlen(responseBody));

        int length = strlen(response_type)
                     + strlen(server_header)
                     + strlen(date_string)
                     + strlen(location_header)
                     + strlen(content_type)
                     + strlen(content_length)
                     + strlen(last_modified)
                     + strlen(connection)
                     + strlen(responseBody);


        char* response = (char*)calloc(length + 1, sizeof(char));
        if(!response) {
                free(responseBody);
                return NULL;
        }

        sprintf(response, "%s%s%s%s%s%s%s%s%s",
                response_type,
                server_header,
                date_string,
                location_header,
                content_type,
                content_length,
                last_modified,
                connection,
                !sIsPathDir || sFoundFile ? "" : responseBody); //attach body only if not file

        free(responseBody);
        return response;
}


/*********************************/
/*********************************/
/*********************************/
char* getResponseBody(int type) {

        char title[SIZE_HTML_TAGS];
        char body[SIZE_HTML_TAGS];

        memset(title, 0, sizeof(title));
        memset(body, 0, sizeof(body));

        switch (type) {

        case CODE_FOUND:
                strcat(title, CODE_FOUND_STRING);
                strcat(body, RESPONSE_FOUND);
                break;

        case CODE_BAD:
                strcat(title, CODE_BAD_STRING);
                strcat(body, RESPONSE_BAD_REQUEST);
                break;

        case CODE_FORBIDDEN:
                strcat(title, CODE_FORBIDDEN_STRING);
                strcat(body, RESPONSE_FORBIDDEN);
                break;

        case CODE_NOT_FOUND:
                strcat(title, CODE_NOT_FOUND_STRING);
                strcat(body, RESPONSE_NOT_FOUND);
                break;

        case CODE_INTERNAL_ERROR:
                strcat(title, CODE_INTERNAL_ERROR_STRING);
                strcat(body, RESPONSE_INTERNAL_ERROR);
                break;

        case CODE_NOT_SUPPORTED:
                strcat(title, CODE_NOT_SUPPORTED_STRING);
                strcat(body, RESPONSE_NOT_SUPPORTED);
                break;

        }

        int length = strlen(RESPONSE_BODY_TEMPLATE) + 2*strlen(title) + strlen(body);

        char* responseBody = (char*)calloc(length + 1, sizeof(char));
        if(!responseBody)
                return NULL;

        sprintf(responseBody, RESPONSE_BODY_TEMPLATE, title, title, body);

        return responseBody;
}

/*********************************/
/*********************************/
/*********************************/

char* getDirContents(char* path) {
        debug_print("getDirContents\n\tpath = %s\n", path);

        debug_print("\t%s\n", "reading dir");
        int i;


        char title[strlen(DIR_CONTENTS_TITLE) + strlen(path) + 1];
        char body[SIZE_DIR_ENTITY * sNumOfFiles];

        memset(title, 0, sizeof(title));
        memset(body, 0, sizeof(body));


        sprintf(title, "Index of %s", path);

        strcat(body, "<table CELLSPACING=8>\n<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\n");

        for(i = 0; i < sNumOfFiles; i++) {

                if(!strcmp(sFileList[i]->d_name, ".") || !strcmp(sFileList[i]->d_name, ".."))
                        continue;

                char tempPath[strlen(path) + strlen(sFileList[i]->d_name) + 1];
                memset(tempPath, 0, sizeof(tempPath));
                strcat(tempPath, path);
                strcat(tempPath, sFileList[i]->d_name);
                debug_print("tempPath = %s\n", tempPath);

                struct stat statBuff;
                if(stat(tempPath, &statBuff))
                        return NULL;
                char timebuf[SIZE_DATE_BUFFER];
                strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&statBuff.st_mtime));


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
                strcat(body, entity);
        }

        strcat(body, "</table>\n<HR>\n<ADDRESS>webserver/1.0</ADDRESS>\n");

        int length = strlen(RESPONSE_BODY_TEMPLATE) + 2*strlen(title) + strlen(body);
        char* responseBody = (char*)calloc(length + 1, sizeof(char));
        if(!responseBody)
                return NULL;

        sprintf(responseBody, RESPONSE_BODY_TEMPLATE, title, title, body);
        debug_print("%s\n", "getDirContents END");
        return responseBody;
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

        // debug_print("response length = %d\nresponse: \n%s\n", response_length, *response); //FIXME


        while(bytes_written < response_length) {

                if((nBytes = write((*sockfd), *response, strlen(*response))) < 0) {
                        debug_print("%s\n", "writing response failed");
                        return -1;
                }

                bytes_written += nBytes;
        }

        return 0;
}

/******************************************************************************/
/*************************** Misc Methods *************************************/
/******************************************************************************/

void freeFileList() {
        debug_print("%s\n", "freeFileList");

        if(sFileList) {
                debug_print("\t%s\n", "freeing sFileList");
                int i;
                for(i = 0; i < sNumOfFiles; i++)
                        free(sFileList[i]);
                free(sFileList);
        }
        debug_print("%s\n", "freeFileList END");
}

/*********************************/
/*********************************/
/*********************************/

int replaceSubstring(char* str, char* orig, char* replace) {
        debug_print("%s\n", "replaceSubstring");

        int orig_length = strlen(orig);
        if(strlen(replace) > orig_length)
                return -1;

        char* origPtr;

        while((origPtr = strstr(str, orig))) {

                //create char* with replace
                char temp[strlen(origPtr) + orig_length + 1];
                memset(temp, 0, sizeof(temp));
                strcat(temp, replace);
                strcat(temp, origPtr + orig_length);

                //replace rest of str with temp
                memset(origPtr, 0, strlen(origPtr));
                strcat(origPtr, temp);
                debug_print("origPtr = %s, temp = %s, str = %s\n", origPtr, temp, str);
        }


        return 0;
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/