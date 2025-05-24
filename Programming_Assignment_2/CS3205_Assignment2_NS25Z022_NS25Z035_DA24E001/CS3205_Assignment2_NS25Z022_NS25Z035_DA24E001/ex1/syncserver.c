// Compile the server
// gcc -o syncserver syncserver.c -pthread

/*
Example Usage:

1. Running the Server

Ensure the `server_sync_dir` exists. Then, start the server:

    ./syncserver server_sync_dir 5000 5

    server_sync_dir → Path to the directory the server monitors.
    5000 → Port number for communication.
    5 → Maximum number of clients allowed.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <libgen.h>  
#include <fcntl.h>

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + 16))
#define MAX_PATH    2048
#define MAX_IGNORE  256
#define MAX_CLIENTS 10

// Structure to hold client information
typedef struct {
    int socket;
    struct sockaddr_in address;
    char ignore_list[MAX_IGNORE];
    int active;
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

int inotify_fd;
char sync_dir[MAX_PATH];

// Utility function to check if a file is in the ignore list
int is_ignored(Client *client, const char *filename) {
    char *token = strtok(strdup(client->ignore_list), ",");
    while (token) {
        if (strstr(filename, token) != NULL) {
            free(token);
            return 1;
        }
        token = strtok(NULL, ",");
    }
    return 0;
}

// Send file to client
void send_file_to_client(Client* client, const char* filepath) {
    if (is_ignored(client, filepath)) return;
    
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) return;

    // Copy filepath because basename() may modify the string
    char* filepath_copy = strdup(filepath);
    if (!filepath_copy) {
        close(fd);
        return;
    }

    char* filename = basename(filepath_copy);  // Extract just the filename

    char cmd[MAX_PATH];
    snprintf(cmd, sizeof(cmd), "FILE %s\n", filename);
    send(client->socket, cmd, strlen(cmd), 0);

    off_t offset = 0;
    struct stat stat_buf;
    fstat(fd, &stat_buf);
    sendfile(client->socket, fd, &offset, stat_buf.st_size);

    free(filepath_copy);  // Free the duplicated string
    close(fd);
}

// Send message to all clients except ignored files
void send_to_clients(const char *message, const char *filename) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].active && (filename == NULL || !is_ignored(&clients[i], filename))) {
            send(clients[i].socket, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&client_mutex);
}

// Recursively add directory watches
void add_recursive_watches(int fd, const char* path) {
    int wd = inotify_add_watch(fd, path, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    if (wd < 0) return;

    DIR* dir = opendir(path);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && 
            strcmp(entry->d_name, "..") != 0) {
            char subpath[MAX_PATH];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
            add_recursive_watches(fd, subpath);
        }
    }
    closedir(dir);
}

// Directory monitoring thread
void *watch_directory(void *arg) {
    char *sync_dir = (char *)arg;
    inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        perror("inotify_init");
        return NULL;
    }

    add_recursive_watches(inotify_fd, sync_dir);

    char buffer[BUF_LEN];
    while (1) {
        int length = read(inotify_fd, buffer, BUF_LEN);
        if (length < 0) {
            perror("read");
            break;
        }
        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            if (event->len) {
                char message[MAX_PATH];
                char filepath[MAX_PATH];
                snprintf(filepath, sizeof(filepath), "%s/%s", sync_dir, event->name);

                if (event->mask & IN_CREATE) {
                    snprintf(message, sizeof(message), "CREATE %s", event->name);
                    send_to_clients(message, event->name);
                    if (!(event->mask & IN_ISDIR)) {
                        pthread_mutex_lock(&client_mutex);
                        for (int j = 0; j < client_count; j++) {
                            if (clients[j].active) {
                                send_file_to_client(&clients[j], filepath);
                            }
                        }
                        pthread_mutex_unlock(&client_mutex);
                    } else {
                        add_recursive_watches(inotify_fd, filepath);
                    }
                } else if (event->mask & IN_DELETE) {
                    snprintf(message, sizeof(message), "DELETE %s", event->name);
                    send_to_clients(message, event->name);
                } else if (event->mask & IN_MOVED_FROM) {
                    snprintf(message, sizeof(message), "MOVED_FROM %s", event->name);
                    send_to_clients(message, event->name);
                } else if (event->mask & IN_MOVED_TO) {
                    snprintf(message, sizeof(message), "MOVED_TO %s", event->name);
                    send_to_clients(message, event->name);
                }
            }
            i += EVENT_SIZE + event->len;
        }
    }
    close(inotify_fd);
    return NULL;
}

// Send initial directory state to client
void send_initial_state(Client *client, const char *sync_dir) {
    DIR *dir = opendir(sync_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char filepath[MAX_PATH];
        snprintf(filepath, sizeof(filepath), "%s/%s", sync_dir, entry->d_name);
        
        char message[MAX_PATH];
        snprintf(message, sizeof(message), "CREATE %s", entry->d_name);
        send(client->socket, message, strlen(message), 0);
        
        if (entry->d_type != DT_DIR) {
            send_file_to_client(client, filepath);
        }
    }
    closedir(dir);
}

// Client handling thread
void *handle_client(void *arg) {
    Client *client = (Client *)arg;
    
    // Receive ignore list
    int bytes = recv(client->socket, client->ignore_list, sizeof(client->ignore_list) - 1, 0);
    if (bytes > 0) {
        client->ignore_list[bytes] = '\0';
    }
    
    // Send initial directory state
    send_initial_state(client, sync_dir);
    
    char buffer[256];
    while (recv(client->socket, buffer, sizeof(buffer), 0) > 0) {
        // Handle client messages if needed
    }
    
    // Cleanup
    pthread_mutex_lock(&client_mutex);
    client->socket = -1;
    client->active = 0;
    client_count--;
    pthread_mutex_unlock(&client_mutex);
    close(client->socket);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <sync_dir> <port> <max_clients>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    strncpy(sync_dir, argv[1], MAX_PATH - 1);
    int port = atoi(argv[2]);
    int max_clients = atoi(argv[3]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, max_clients) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    pthread_t watcher_thread;
    if (pthread_create(&watcher_thread, NULL, watch_directory, sync_dir) != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", port);
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (new_socket < 0) {
            perror("accept");
            continue;
        }

        pthread_mutex_lock(&client_mutex);
        if (client_count >= max_clients) {
            close(new_socket);
            pthread_mutex_unlock(&client_mutex);
            continue;
        }

        int i;
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                clients[i].socket = new_socket;
                clients[i].address = client_addr;
                clients[i].active = 1;
                client_count++;
                break;
            }
        }
        pthread_mutex_unlock(&client_mutex);

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, handle_client, &clients[i]) != 0) {
            perror("pthread_create client");
            pthread_mutex_lock(&client_mutex);
            clients[i].active = 0;
            client_count--;
            close(new_socket);
            pthread_mutex_unlock(&client_mutex);
        }
    }
    close(server_fd);
    return 0;
}
