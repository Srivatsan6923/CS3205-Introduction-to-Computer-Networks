#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ncurses.h>

#define WIDTH 80
#define HEIGHT 30
#define OFFSETX 10
#define OFFSETY 5
#define paddle_width 10

int server_socket;
int ball_x, ball_y; // Ball position
int paddle_x = 45;       // Paddle position
int penalty;        // Penalty count
int paddle2_x = 45;       // Paddle position
int penalty_2;        // Penalty count

pthread_mutex_t lock; // Mutex for thread-safe access to shared variables

// Function declarations
void *render_game(void *args);
void *receive_data(void *args);
void *send_data(void *args);
void draw(WINDOW *game_window);

int main(int argc, char *argv[]) {
    // Validate command-line arguments for client

    // Number of arguments should be 2
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return -1;
    }

    // Connect to server
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(12345); // Server port

    // Check if the IP address is valid
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        perror("Invalid address or address not supported");
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connection to server failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server!\n");

    // Initialize ncurses for rendering
    initscr();                  // Initialize ncurses
    start_color();
    init_pair(1, COLOR_BLUE, COLOR_WHITE);
    init_pair(2, COLOR_YELLOW, COLOR_YELLOW);

    // Create a new window for the game
    WINDOW *game_window = newwin(HEIGHT, WIDTH, 0, 0);
    box(game_window, 0, 0);  // Draw border initially

    curs_set(FALSE);            // Hide cursor
    keypad(stdscr, TRUE);       // Enable special keys like arrow keys
    timeout(10);                // Non-blocking input with a timeout of 10ms

    pthread_mutex_init(&lock, NULL); // Initialize mutex

    // Create threads for different tasks
    pthread_t render_thread, receive_thread, send_thread;

    // Thread for rendering the game
    pthread_create(&render_thread, NULL, render_game, NULL);

    // Thread for receiving data from the server
    pthread_create(&receive_thread, NULL, receive_data, NULL);

    // Thread for sending user input to the server
    pthread_create(&send_thread, NULL, send_data, NULL);

    // Wait for threads to finish (optional)
    pthread_join(render_thread, NULL);
    pthread_join(receive_thread, NULL);
    pthread_join(send_thread, NULL);

    // Clean up resources
    endwin();           // End ncurses mode
    clear();
    close(server_socket); // Close socket connection
    pthread_mutex_destroy(&lock);

    return 0;
}


// Thread function to render the game state
void *render_game(void *args) {
    while (1) {
        pthread_mutex_lock(&lock); // Lock shared variables

        WINDOW *game_window = newwin(HEIGHT, WIDTH, 0, 0);
        box(game_window, 0, 0);  // Draw border initially

        draw(game_window); // Render the game state

        pthread_mutex_unlock(&lock); // Unlock shared variables

        usleep(20000); // Control rendering speed
    }
}

// Thread function to receive data from the server
void *receive_data(void *args) {
    char buffer[256];
    int temp_ball_x, temp_ball_y, temp_penalty, temp_paddle2_x, temp_penalty_2; // Temporary variables

    while (1) {
        int bytes_received = recv(server_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            clear();
            refresh();
            endwin(); // End ncurses mode
            perror("Connection closed by server");
            exit(0);
            break;
        }

        buffer[bytes_received] = '\0'; // Null-terminate received string

        // Validate data before updating game state
        if (sscanf(buffer, "%d,%d,%d,%d,%d", &temp_ball_x, &temp_ball_y, &temp_penalty,&temp_paddle2_x,&temp_penalty_2) == 5) {
            pthread_mutex_lock(&lock); // Lock shared variables

            // Only update values if parsing was successful
            ball_x = temp_ball_x;
            ball_y = temp_ball_y;
            penalty = temp_penalty;
            paddle2_x = temp_paddle2_x;
            penalty_2 = temp_penalty_2;

            pthread_mutex_unlock(&lock);
        } else {
            printf("Warning: Received malformed data -> %s\n", buffer);
        }
    }

    return NULL;
}



// Thread function to send user input to the server
void *send_data(void *args) {

    while (1) {
        int key = getch(); // Capture key press
        pthread_mutex_lock(&lock);

        if (key == KEY_LEFT && paddle_x > 1) {
            paddle_x -= 1; // Move left
        } else if (key == KEY_RIGHT && paddle_x < WIDTH - 11) {
            
            paddle_x += 1; // Move right
        } else if (key == 'q') { 
            printf("Quitting game...\n");
            close(server_socket); // Close socket connection
            endwin();             // End ncurses mode
            exit(0);              // Exit the program
        } else if (key == 'c') {
            // Clear the screen
            clear();
            refresh();
        }
        pthread_mutex_unlock(&lock);

        // Send the updated paddle position to the server
        write(server_socket, &paddle_x, sizeof(int));
        usleep(30000); // Add a small delay to control input rate
    }

    return NULL; // Exit thread
}


void draw(WINDOW *game_window) {
    clear();  // Clear the screen
    attron(COLOR_PAIR(1));
    for (int i = OFFSETX; i <= OFFSETX + WIDTH; i++) {
        mvprintw(OFFSETY-1, i, " ");
    }
    mvprintw(OFFSETY-1, OFFSETX + 3, "CS3205 NetPong, Ball: %d, %d", ball_x, ball_y);
    mvprintw(OFFSETY-1, OFFSETX + WIDTH-25, "Server: %d, Client: %d", penalty, penalty_2);
    
    for (int i = OFFSETY; i < OFFSETY + HEIGHT; i++) {
        mvprintw(i, OFFSETX, "  ");
        mvprintw(i, OFFSETX + WIDTH - 1, "  ");
    }
    for (int i = OFFSETX; i < OFFSETX + WIDTH; i++) {
        mvprintw(OFFSETY, i, " ");
        mvprintw(OFFSETY + HEIGHT - 1, i, " ");
    }
    attroff(COLOR_PAIR(1));
    
    // Draw the ball
    mvprintw(OFFSETY + ball_y, OFFSETX + ball_x, "o");

    // Draw the paddle
    attron(COLOR_PAIR(2));
    for (int i = 0; i < paddle_width; i++) {
        mvprintw(OFFSETY + HEIGHT - 4, OFFSETX + paddle_x + i, " ");
    }
    for (int i = 0; i < paddle_width; i++) {
        mvprintw(OFFSETY + 3, OFFSETX + paddle2_x + i, " ");
    }
    attroff(COLOR_PAIR(2));
    
    refresh();

}

