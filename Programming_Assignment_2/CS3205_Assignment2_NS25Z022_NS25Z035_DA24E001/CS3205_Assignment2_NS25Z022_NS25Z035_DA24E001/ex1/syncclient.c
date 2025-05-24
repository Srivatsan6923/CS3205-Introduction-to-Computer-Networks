/*
 * Networked Directory Synchronization Client
 * ------------------------------------------
 * This client application connects to a central server to receive updates about
 * directory synchronization events. The client ignores specified file types
 * and maintains a mirrored directory structure.
 *
 * Compile the client
 * gcc -o syncclient syncclient.c -pthread
 *
 * Usage:
 *   ./syncclient <server_ip> <server_port> <ignore_list_file>
 *
 * Example:
 *   ./syncclient 127.0.0.1 5000 ignore_list.txt
 *

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_PATH 2048
#define MAX_IGNORE 256
#define BUFFER_SIZE 4096

// Global variables
char local_dir[MAX_PATH];
int server_socket;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Read ignore list from file
void read_ignore_list(const char* filename, char* ignore_list) {
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        perror("Failed to open ignore list file");
        exit(EXIT_FAILURE);
    }
    
    if (fgets(ignore_list, MAX_IGNORE, fp) == NULL) {
        ignore_list[0] = '\0';
    } else {
        size_t len = strlen(ignore_list);
        if (len > 0 && ignore_list[len-1] == '\n') {
            ignore_list[len-1] = '\0';
        }
    }
    fclose(fp);
}

// Create directory if it doesn't exist
void ensure_directory(const char* path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0700) == -1 && errno != EEXIST) {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    }
}

// Handle receiving files from server
void receive_file(const char* filename) {
    size_t local_dir_len = strlen(local_dir);
    size_t filename_len = strlen(filename);
    size_t filepath_len = local_dir_len + filename_len + 2;
    
    char* filepath = malloc(filepath_len);
    if (!filepath) {
        perror("malloc");
        return;
    }
    
    snprintf(filepath, filepath_len, "%s/%s", local_dir, filename);
    
    char* dir = strdup(filepath);
    char* last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        ensure_directory(dir);
    }
    free(dir);
    
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open file");
        free(filepath);
        return;
    }
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes;
    while ((bytes = recv(server_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        if (write(fd, buffer, bytes) != bytes) {
            perror("write file");
            break;
        }
        // Check if this is the end of file data (simplified assumption)
        if (bytes < BUFFER_SIZE) break;
    }
    close(fd);
    printf("Received and wrote file: %s\n", filename);
    free(filepath);
}

// Thread to receive and process server updates with terminal output
void* receive_handler(void* arg) {
    char buffer[BUFFER_SIZE];
    while (1) {
        ssize_t bytes = recv(server_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            printf("Disconnected from server\n");
            close(server_socket);
            exit(0);
        }
        
        buffer[bytes] = '\0';
        printf("Raw message from server: '%s'\n", buffer);  // Debug output
        
        char command[16];
        char filename[MAX_PATH];
        if (sscanf(buffer, "%15s %s", command, filename) == 2) {
            if (strcmp(command, "CREATE") == 0) {
                printf("Server event: Created %s\n", filename);
                // File data follows in a separate FILE message
            } else if (strcmp(command, "DELETE") == 0) {
                size_t local_dir_len = strlen(local_dir);
                size_t filename_len = strlen(filename);
                size_t filepath_len = local_dir_len + filename_len + 2;
                
                char* filepath = malloc(filepath_len);
                if (!filepath) {
                    perror("malloc");
                } else {
                    snprintf(filepath, filepath_len, "%s/%s", local_dir, filename);
                    if (remove(filepath) == 0) {
                        printf("Server event: Deleted %s\n", filename);
                    } else {
                        printf("Server event: Failed to delete %s (%s)\n", filename, strerror(errno));
                    }
                    free(filepath);
                }
            } else if (strcmp(command, "MOVED_FROM") == 0) {
                printf("Server event: Moved from %s\n", filename);
                size_t local_dir_len = strlen(local_dir);
                size_t filename_len = strlen(filename);
                size_t filepath_len = local_dir_len + filename_len + 2;
                
                char* filepath = malloc(filepath_len);
                if (!filepath) {
                    perror("malloc");
                } else {
                    snprintf(filepath, filepath_len, "%s/%s", local_dir, filename);
                    remove(filepath);
                    free(filepath);
                }
            } else if (strcmp(command, "MOVED_TO") == 0) {
                printf("Server event: Moved to %s\n", filename);
                // Note: No file transfer here; relies on CREATE/FILE for new location
            } else if (strcmp(command, "FILE") == 0) {
                receive_file(filename);
            } else {
                printf("Received unknown command: %s\n", buffer);
            }
        } else {
            printf("Failed to parse message: %s\n", buffer);
        }
        
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s path_to_local_directory path_to_ignore_list_file\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    strncpy(local_dir, argv[1], MAX_PATH - 1);
    local_dir[MAX_PATH - 1] = '\0';
    char ignore_list[MAX_IGNORE] = {0};
    read_ignore_list(argv[2], ignore_list);
    
    ensure_directory(local_dir);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);  // Adjust port as needed
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");  // Adjust IP as needed

    if (connect(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    if (send(server_socket, ignore_list, strlen(ignore_list), 0) < 0) {
        perror("send ignore list");
        exit(EXIT_FAILURE);
    }

    pthread_t receive_thread;
    if (pthread_create(&receive_thread, NULL, receive_handler, NULL) != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server, synchronizing %s\n", local_dir);
    
    pthread_join(receive_thread, NULL);
    
    close(server_socket);
    return 0;
}
