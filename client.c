#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <cjson/cJSON.h>
#include <dirent.h>
#include <cjson/cJSON.h>

#include <openssl/evp.h>

int sendStuff(char *buffer, int sd, struct sockaddr_in server_address, FILE *fptr);
void makeSocket(int *sd, char *argv[], struct sockaddr_in *server_address);
FILE *openFile(const char* fileName);
char *rtrim(char *s);
int parsePair(char **p, char *key,size_t keyCap, char *value, size_t valCap);
char *skipWhitespace(char *p);
char *readFileAndCreateJsonObjectandSerialize(char *buffer, FILE *fptr);

//new functions used for this lab
void openDirectory(const char* directory, char* chunkDir);
cJSON* hashFileAndSave(const char* filePath, const char* fileName, const char* chunkDir);
void binaryToHex(unsigned char* hash, unsigned int length, char* output);

//chunk size 
#define CHUNK_SIZE (500 * 1024)

int main(int argc, char *argv[]){
    int sd;
    struct sockaddr_in server_address;
    //char buffer[1024];
    //FILE *fptr;
    char* directory;

    if(argc < 4){
        printf("Usage: <IPaddr> <portNumber> <Directory Name>\n");
        exit(1);
    }
    
    //make the socket and send the data over to server
    makeSocket(&sd, argv, &server_address);
    //sendStuff(buffer, sd, server_address, fptr);

    directory = argv[3];

    //create directory for the chunks to be saved in
    char chunkDir[PATH_MAX];
    snprintf(chunkDir, sizeof(chunkDir), "%s/CHUNKS", directory);
    if(mkdir(chunkDir, 0755) != 0){
        perror("mkdir failed");
        exit(1);
    }

    openDirectory(directory, chunkDir);


    return 0;
}


//this opens the directory given in the CLI, iterates through each file until the end of the directory
void openDirectory(const char* directory, char* chunkDir){
    DIR* folder;
    struct dirent* entry;
    folder = opendir(directory);
    char* fileName;
    char filePath[PATH_MAX];


    if(folder == NULL){
        printf("Unable to read directory %s\n", directory);
        exit(1);
    }

    //this loops throguh every file in the directory until it reaches the end
    while((entry = readdir(folder)) != NULL){
        
        //skip current and parent directories and the CHUNKS folder where the hashes will be saved
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "CHUNKS") == 0) continue;

        //set the name of the current file and create the path to that file to be used by the hashing function
        fileName = entry -> d_name;
        snprintf(filePath, sizeof(filePath), "%s/%s", directory, fileName);

        //create the json object that will store the file info and hash info and print it
        cJSON* jsonObj = hashFileAndSave(filePath, fileName, chunkDir);
        char* jsonString = cJSON_Print(jsonObj);
        printf("%s\n", jsonString);
        cJSON_Delete(jsonObj);
        free(jsonString);
    }
    closedir(folder);
}


//this function takes the filepath for every file in the directory, get the info to be stored in a json object 
//and also hashes the full file and the data, and it then stores each chunk as its own file in the CHUNKS folder
cJSON* hashFileAndSave(const char* filePath, const char* fileName, const char* chunkDir){

    FILE* fptr = openFile(filePath);
    if(!fptr){
        perror("openFile error");
        exit(1);
    }
    //get the size of the file and store it
    fseek(fptr, 0, SEEK_END);
    long fileSize = ftell(fptr);
    rewind(fptr);

    unsigned char buffer[CHUNK_SIZE];
    size_t bytesRead; 

    //json objects used to store the info for the file and hashed chunks
    cJSON *jsonObject = cJSON_CreateObject();
    cJSON *chunkArray = cJSON_CreateArray();
    int numOfChunks = 0;

    //initialize the context for the full file hash **used what dave used (posted in canvas)
    EVP_MD_CTX *fileContext = EVP_MD_CTX_new();
    if(EVP_DigestInit_ex(fileContext, EVP_sha256(), NULL) != 1){
            EVP_MD_CTX_free(fileContext);
    }

    //iterate through the file's data 500kb at a time and hash each chunk, store the hashed chunk as a file
    //also hashes full file, updated incrementally 
    while((bytesRead = fread(buffer, 1, CHUNK_SIZE, fptr)) > 0){
        EVP_MD_CTX *chunkContext = EVP_MD_CTX_new();

        //initialize chunk context
        if(EVP_DigestInit_ex(chunkContext, EVP_sha256(), NULL) != 1){
            EVP_MD_CTX_free(chunkContext);
        }

        if(EVP_DigestUpdate(chunkContext, buffer, bytesRead) != 1){
            EVP_MD_CTX_free(chunkContext);
        }
        
        //this is to avoid rereading file, incrementally update full file hash
        if(EVP_DigestUpdate(fileContext, buffer, bytesRead) != 1){
            EVP_MD_CTX_free(fileContext);
        }

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen;

        if(EVP_DigestFinal_ex(chunkContext, hash, &hashLen) != 1){
            EVP_MD_CTX_free(chunkContext);
        }

        EVP_MD_CTX_free(chunkContext);

        

        char hashHex[65];
        binaryToHex(hash, hashLen, hashHex);
        
        //add chunk info to json object
        cJSON_AddItemToArray(chunkArray, cJSON_CreateString(hashHex));

        //write the hashed chunk to a new file in the subdirectory after making the subDir
        char chunkPath[PATH_MAX];
        snprintf(chunkPath, sizeof(chunkPath), "%s/%s", chunkDir, hashHex);

        FILE* chunkFptr = fopen(chunkPath, "wb");
        if (!chunkFptr) {
            perror("fopen chunk failed");
            exit(1);
        }

        fwrite(hash, 1, hashLen, chunkFptr);
        fclose(chunkFptr);
        numOfChunks++;

    }


    unsigned char fileHash[EVP_MAX_MD_SIZE];
    unsigned int fileHashLen;
    if(EVP_DigestFinal_ex(fileContext, fileHash, &fileHashLen) != 1){
            EVP_MD_CTX_free(fileContext);
    }

    EVP_MD_CTX_free(fileContext);

    char fileHashHex[65];
    binaryToHex(fileHash, fileHashLen, fileHashHex);

    //add hashed full file and chunk info into obj and return that obj
    cJSON_AddStringToObject(jsonObject, "filename", fileName);
    cJSON_AddNumberToObject(jsonObject, "fileSize", fileSize);
    cJSON_AddNumberToObject(jsonObject, "numberOfChunks", numOfChunks);
    cJSON_AddItemToObject(jsonObject, "chunk_hashes", chunkArray);
    cJSON_AddStringToObject(jsonObject, "fullFileHash", fileHashHex);

    fclose(fptr);
    return jsonObject;

}


void makeSocket (int *sd, char* argv[], struct sockaddr_in *server_address){
    
    struct sockaddr_in inaddr; //temp value for validation checking 
    int portNumber; //from cmd line
    char serverIP[50]; //ip from cmd line


    //checks for valid ip address
    if(!inet_pton(AF_INET, argv[1], &inaddr)){
        printf("error, bad ip address\n");
        exit(1);
    }

    //creat a socket
    *sd = socket(AF_INET, SOCK_DGRAM, 0);
    int reuse = 1;
    setsockopt(*sd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    //error checking on socket creation
    if(*sd == -1){
        perror("socket");
        exit(1);
    }

    

    portNumber = strtol(argv[2], NULL, 10);

    //validate port number
    if((portNumber > 65535) || (portNumber < 0)){
        printf("You entered an invalid socker number/n");
        exit(1);
    }

    //copy the ip address from cmd line 
    strcpy(serverIP, argv[1]); 
    //fill in the address data structure
    server_address->sin_family = AF_INET; //use AF INET addrs
    server_address->sin_port = htons(portNumber); //convert port number
    server_address->sin_addr.s_addr = inet_addr(serverIP); //convert IP addr
}

//gets the name of file from user, opens the file, and returns the file descriptor
FILE * openFile(const char* fileName){
    FILE * fptr = NULL;
    
    // char *ptr = fgets(fileName, sizeof(fileName), stdin);
    // if(ptr == NULL){
    //     perror("fgets");
    //     exit(1);
    // }

    fptr = fopen(fileName, "rb");
    if(fptr == NULL){
        printf("Error opening file, try again\n");
    }
    return fptr;
}

//this function converts the binary hash into hex to be used for json output and filename for each hash
void binaryToHex(unsigned char* hash, unsigned int length, char* output){
    for(unsigned int i = 0; i < length; i++){
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[length * 2] = '\0';
}

//parse key value pairs in a line, handle whitespaces within quotes and quotes themselves
int parsePair(char **p, char *key, size_t keyCap, char *value, size_t valCap){
    //we set the first non white space character to s
    char *s = skipWhitespace(*p);

    //once we hit the end of the line, there are no more key value pairs so we exit function
    if(!s || *s == '\0'){
        *p = s;
        return 0;
    }

    //look for colon splitting the key value pair, if none it is malformed
    char *colon = strchr(s, ':');
    if(!colon){
        printf("colon error\n");
        return -1;
    }

    //get length of key before colon and trim any spaces before it
    size_t keyLength = (size_t)(colon - s);
    while(keyLength > 0 && isspace((unsigned char)s[keyLength - 1])) keyLength--;

    //prevent overflow, and copy raw bytes with memcpy, manually null terminate
    if(keyLength >= keyCap) keyLength = keyCap - 1;
    memcpy(key, s , keyLength);
    key[keyLength] = '\0';

    //move to value on the other side of colon and error handle malformed pair
    s = colon + 1;
    s = skipWhitespace(s);

    if(!s || *s == '\0'){
        printf("s error\n");
        return -1;
    }

    //if value has quotes, handle white spaces accordingly, treaat as quoted string
    if(*s == '"'){
        s++; //skip open quote
        char *endQuote = strchr(s, '"'); //find end quote
        if(!endQuote){
            printf("quote error\n");
            return -1;
        }

        //copy everything between the quotes into the value
        size_t valueLength = (size_t)(endQuote - s);
        if(valueLength >= valCap) valueLength = valCap - 1;
        memcpy(value, s, valueLength);
        value[valueLength] = '\0';
        s = endQuote + 1; //cursor is now at the space after closing quote

    } else { //for unquoted tokens, value is treated as a single token, copies into value until whitespace
        char *end = s;
        while(*end && !isspace((unsigned char)*end)) end++;

        size_t valueLength = (size_t)(end - s);
        if(valueLength >= valCap) valueLength = valCap - 1;
        memcpy(value, s, valueLength);
        value[valueLength] = '\0';

        s = end;
    }

    //update the caller's cursor and return 1 for no failure
    *p = s;
    return 1;


}   

//make JSON object with data from file and serialize
char *readFileAndCreateJsonObjectandSerialize(char *buffer, FILE *fptr){
    char *lineFromFile = NULL;
    int rc;
    size_t lengthRead = 0;
    char key[200];
    char value[200];


    //get line from the opened file
    rc = getline(&lineFromFile, &lengthRead, fptr);
    if(rc <= 0 ){
        fclose(fptr); 
        return NULL;
    }

    //make json object for each line
    cJSON *jsonObject = cJSON_CreateObject();

    //caller's cursor to iterate through pairs
    char *lineCursor = lineFromFile;
    for(;;){
        //parse on key value pair at a time from the line
        int prc = parsePair(&lineCursor, key, sizeof(key), value, sizeof(value));
        if(prc == 0) break;
        if(prc < 0){
            cJSON_Delete(jsonObject);
            return NULL;
        }
        //add the key values to json object
        cJSON_AddStringToObject(jsonObject, key, value);

        printf("%-20s %s\n", key, value);
    }
    
    //store serialized object
    char *serializedObject = cJSON_PrintUnformatted(jsonObject);
    cJSON_Delete(jsonObject); //free the jsonObject

    //copy serialized json into buffer and free the searilzedObject
    strcpy(buffer, serializedObject);
    free(serializedObject);

    //return buffer with jason string
    return buffer;
}


int sendStuff(char *buffer, int sd, struct sockaddr_in server_address, FILE *fptr){

    //loop through file and serialize each line and send each line until all lines have been sent
    for(;;){
        char *jsonLine = readFileAndCreateJsonObjectandSerialize(buffer, fptr);
        if(!jsonLine){
            printf("done\n");
            break;
        }
        int sent = 0;
        printf("***********************************************\n");
        sent = sendto(sd, jsonLine, strlen(jsonLine), 0, (struct sockaddr *) &server_address, sizeof(server_address));
        if(sent < 0){
            printf("sent less than 0");
            return -1;
        }   
    }
    return (0);
}

//skip white space and return that character
char *skipWhitespace(char *p){
    while(p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

//trim characters from a string
char *rtrim(char *s){
    char *back = s + strlen(s);
    while(isspace(*--back));
    *(back+1) = '\0';
    return s;
}
