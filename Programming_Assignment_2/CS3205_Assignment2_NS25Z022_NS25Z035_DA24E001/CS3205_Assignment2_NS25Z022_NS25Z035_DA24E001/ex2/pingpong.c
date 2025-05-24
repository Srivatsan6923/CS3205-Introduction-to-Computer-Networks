#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    // Validate command-line arguments

    // Number of arguments should be 3
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server|client> <port|server_ip>\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "server") == 0) {
        // If the first argument is "server"
        char *port = argv[2];
        execl("./p_server", "./p_server", port, (char *)NULL);
        perror("Error executing server");
    } else if (strcmp(argv[1], "client") == 0) {
        // If the first argument is "client"
        char *server_ip = argv[2];
        execl("./p_client", "./p_client", server_ip, (char *)NULL);
        perror("Error executing client");
    } else {
        fprintf(stderr, "Invalid first argument. Use 'server' or 'client'.\n");
        return 1;
    }

    return 0;
}