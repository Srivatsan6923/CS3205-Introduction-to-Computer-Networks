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

int paddle2_x = 45;       // Paddle position

// Structures for game state
typedef struct {
    int x, y;       // Ball position
    int dx, dy;     // Ball velocity
} Ball;

typedef struct {
    int x;          // Paddle position
    int width;      // Paddle width
} Paddle;

// Structure for unified game state
typedef struct {
    Ball ball;
    Paddle paddle;
    int penalty;
    Paddle paddle2;
    pthread_mutex_t lock; // Mutex for thread-safe access to game state
    int penalty_2;
} GameState;

// Structure for thread arguments
typedef struct {
    GameState *state;
    int client_socket;
    int server_socket;
} ThreadArgs;

// Function declarations
void *move_ball(void *args);
void *send_game_state(void *args);
void *receive_client_input(void *args);
void reset_ball(GameState *state);
void draw(WINDOW *game_window, GameState *state);
void *render_game(void *args);
void *move_paddle(void *args);

int main(int argc, char *argv[]) {
    // Validate command-line arguments for server

    // Number of arguments should be 2
    if (argc != 2) {
        printf("Usage: %s server <port>\n", argv[0]);
        return 1;
    }
    // Convert port number to integer
    int port = atoi(argv[1]);

    // Port number should be between 1 and 65535
    if (port < 1 || port > 65535) {
        printf("Invalid port number. Port should be between 1 and 65535.\n");
        return 1;
    }

    
    // Create a new window for the game
    WINDOW *game_window = newwin(HEIGHT, WIDTH, 0, 0);
    box(game_window, 0, 0);  // Draw border initially
    
    // Initialize game state
    GameState state;
    state.ball = (Ball){WIDTH / 2, HEIGHT / 2, 1, 1};
    state.paddle = (Paddle){WIDTH / 2 - 5, 10};
    state.penalty = 0;

    // Initialize mutex for thread-safe access to game state
    pthread_mutex_init(&state.lock, NULL);
    
    // Set up server socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Set up server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    // Bind server socket to address
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for incoming connections
    if (listen(server_socket, 1) == -1) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Waiting for client to connect...\n");
    
    // Accept client connection
    int client_socket = accept(server_socket, NULL, NULL);
    if (client_socket == -1) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }
    printf("Client connected!\n");
    
    initscr(); // Initialize ncurses
    noecho();
    curs_set(0); // Hide cursor

    // Initialize ncurses for rendering
    start_color();
    init_pair(1, COLOR_BLUE, COLOR_WHITE);
    init_pair(2, COLOR_YELLOW, COLOR_YELLOW);

    // Create threads for different tasks
    pthread_t ball_thread, send_thread, receive_thread, render_thread, move_paddle_thread;
    
    // Create argument structure for threads
    ThreadArgs args = {&state, client_socket, server_socket};
    
    // Thread for moving paddle
    pthread_create(&move_paddle_thread, NULL, move_paddle, &state);
    
    // Thread for rendering the game
    pthread_create(&render_thread, NULL, render_game, &state);

    // Thread for moving ball and updating game logic
    pthread_create(&ball_thread, NULL, move_ball, &state);

    // Thread for sending game state to client
    pthread_create(&send_thread, NULL, send_game_state, &args);

    // Thread for receiving input from client
    pthread_create(&receive_thread, NULL, receive_client_input, &args);

    // Wait for threads to finish (optional)
    pthread_join(ball_thread, NULL);
    pthread_join(send_thread, NULL);
    pthread_join(receive_thread, NULL);
    pthread_join(render_thread, NULL);
    pthread_join(move_paddle_thread, NULL);

    endwin();  // End ncurses mode
    // Clean up resources
    close(client_socket);
    close(server_socket);
    pthread_mutex_destroy(&state.lock);
    
    return 0;
}

// Thread function to move the paddle2
void *move_paddle(void *args) {
    WINDOW *win = newwin(HEIGHT, WIDTH, 0, 0);

    while (1) {
        int key = getch(); // Capture key press

        if (key == 'a' && paddle2_x > 1) {
            paddle2_x -= 1; // Move left
        } else if (key == 'd' && paddle2_x < WIDTH - 11) {
            
            paddle2_x += 1; // Move right
        } else if (key == 'q') { 
            printf("Quitting game...\n");
            endwin();             // End ncurses mode
            exit(EXIT_FAILURE);
        } else if (key == 'c') {
            // Clear the screen
            clear();
            refresh();
        }
    }
    usleep(30000);
}

// Thread function to render the game state
void *render_game(void *args) {
    GameState *state = (GameState *)args;
    while (1) {
        pthread_mutex_lock(&state->lock); // Lock shared variables

        WINDOW *game_window = newwin(HEIGHT, WIDTH, 0, 0);
        box(game_window, 0, 0);  // Draw border initially

        draw(game_window, state); // Render the game state

        pthread_mutex_unlock(&state->lock); // Unlock shared variables

        usleep(20000); // Control rendering speed
    }
}

// Thread function to move the ball and handle game logic
void *move_ball(void *args) {
    GameState *state = (GameState *)args;

    while (1) {

        // Lock the game state for thread-safe access
        pthread_mutex_lock(&state->lock);

        // Move ball
        state->ball.x += state->ball.dx;
        state->ball.y += state->ball.dy;

        // Bounce off walls
        // Prevent ball from overlapping the walls before bouncing
        if (state->ball.x <= 2 || state->ball.x >= WIDTH - 2)  
            state->ball.dx = -state->ball.dx;

        // Bounce off the Bottom and Top Paddle
        if (state->ball.y == HEIGHT - 5 && 
            state->ball.x >= state->paddle.x - 1 && 
            state->ball.x < state->paddle.x + state->paddle.width + 1) {
            state->ball.dy = -state->ball.dy;
        }else if (state->ball.y == 3 &&
            state->ball.x >= paddle2_x &&
            state->ball.x < paddle2_x + state->paddle.width) {
            state->ball.dy = -state->ball.dy; // Bounce off paddle
        } else if (state->ball.y >= HEIGHT - 1) { // Missed paddle
            reset_ball(state);               // Reset ball position
            state->penalty++;                // Increment penalty count
            // reset paddle position

        } else if (state->ball.y <= 2 ) {
            reset_ball(state);               // Reset ball position
            state->penalty_2++;                // Increment penalty count
            // reset paddle position

        }

        pthread_mutex_unlock(&state->lock);
        usleep(80000); // Control ball speed
    }
}

// Reset the ball to its initial position
void reset_ball(GameState *state) {
    state->ball.x = WIDTH / 3;
    state->ball.y = HEIGHT / 3;
    state->ball.dx = 1;
    state->ball.dy = 1;
}

// Thread function to send game state to client
void *send_game_state(void *args) {
    ThreadArgs *thread_args = (ThreadArgs *)args;
    GameState *state = thread_args->state;
    int client_socket = thread_args->client_socket;

    char buffer[256];
    
	while (1) {
        
        pthread_mutex_lock(&state->lock);

        // write game state to buffer
        snprintf(buffer, sizeof(buffer), "%d,%d,%d,%d,%d\n",
                 state->ball.x, state->ball.y, state->penalty,paddle2_x,state->penalty_2);

        pthread_mutex_unlock(&state->lock);

        // Send game state to client
        if (send(client_socket, buffer, strlen(buffer), 0) == -1) {
            perror("Error sending data to client");
            break;
        }

        usleep(20000); // Send data at a controlled rate
	}

	return NULL; 
}

void *receive_client_input(void *args) {
    ThreadArgs *thread_args = (ThreadArgs *)args; // Cast argument to ThreadArgs structure
    GameState *state = thread_args->state;       // Extract GameState pointer
    int client_socket = thread_args->client_socket; // Extract client socket
    int server_socket = thread_args->server_socket; // Extract server socket

    int new_paddle_x; // Variable to store the received paddle position

    while (1) {
        // Receive the updated paddle position from the client
        int bytes_received = recv(client_socket, &new_paddle_x, sizeof(int), 0);
        
        if (bytes_received > 0) {
            pthread_mutex_lock(&state->lock); // Lock the game state for thread-safe access
            
            state->paddle.x = new_paddle_x; // Directly update the paddle position

            pthread_mutex_unlock(&state->lock); // Unlock the game state
        } else if (bytes_received == 0) {
            clear(); // Clear the screen
            refresh(); // Refresh the screen
            endwin(); // End ncurses mode
            close(client_socket); // Close the client socket
            close(server_socket);
            printf("Client disconnected.\n");
            exit(0);  // Exit the program

            break; // Exit the loop if the client disconnects
        } else {
            perror("Error receiving data from client");
            break; // Exit the loop on error
        }
    }

    return NULL; // Exit the thread
}


void draw(WINDOW *game_window, GameState *state) {
    clear();  // Clear the screen
    attron(COLOR_PAIR(1));
    for (int i = OFFSETX; i <= OFFSETX + WIDTH; i++) {
        mvprintw(OFFSETY-1, i, " ");
    }
    mvprintw(OFFSETY-1, OFFSETX + 3, "CS3205 NetPong, Ball: %d, %d", state->ball.x, state->ball.y);
    mvprintw(OFFSETY-1, OFFSETX + WIDTH-25, "Server: %d, Client: %d", state->penalty, state->penalty_2);
    
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
    mvprintw(OFFSETY + state->ball.y, OFFSETX + state->ball.x, "o");

    // Draw the paddle
    attron(COLOR_PAIR(2));
    for (int i = 0; i < state->paddle.width; i++) {
        mvprintw(OFFSETY + HEIGHT - 4, OFFSETX + state->paddle.x + i, " ");
    }
    for (int i = 0; i < state->paddle.width; i++) {
        mvprintw(OFFSETY + 3, OFFSETX + paddle2_x + i, " ");
    }
    attroff(COLOR_PAIR(2));
    
    refresh();
}

