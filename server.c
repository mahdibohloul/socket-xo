#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/time.h>
#include <fcntl.h>

#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024
#define WAITING_FOR_NAME 1
#define NAME_MAX_LENGTH 20
#define GAME_FINISHED 1
#define GAME_STARTED 0
#define GAME_NOT_STARTED -1
#define BASE_PORT_NUMBER 5000
#define GAME_STARTED_CODE_MESSAGE 291
#define GAME_WATCH_CODE_MESSAGE 295


enum UserRole {
    PLAYER,
    OBSERVER,
    NONE
};

struct Game;

struct User {
    char name[NAME_MAX_LENGTH];
    int fd;
    int waiting_for_game;
    enum UserRole role;
    struct Game *game;
};

struct UserNode {
    struct User *user;
    struct UserNode *next;
};

struct Game {
    int is_done;
    int port;
    struct User *players[2];
    struct UserNode *observers;
};

struct GameNode {
    struct Game *game;
    struct GameNode *next;
};

struct Game *pending_games = NULL;
struct GameNode *active_games = NULL;

struct UserNode *users = NULL;

struct Game *get_pending_game(struct User *player);

void add_game(struct Game *game);

void add_user(struct User *user);

int setup_server(int port);

int accept_client(int server_fd);

int handle_client(struct User *user, char *input, int input_length);

char *get_user_selective_msg();

int get_active_games_number();

void send_msg(int fd, char *msg);

char *get_ready_to_watch_games();

void game_over(struct Game *game, char winner);

void remove_game(struct Game *game);

void track_game_result(struct Game *game, char winner);

struct User *get_user_by_fd(int fd);

struct Game *get_game_by_port(int port);

void add_observer(struct Game *game, struct User *observer);


int main(int argc, const char *argv[]) {
    int server_port, server_fd, new_socket, max_sd;
    printf("Setting up server\n");
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    server_port = atoi(argv[1]);
    server_fd = setup_server(server_port);
    char buffer[BUFFER_SIZE] = {0};

    fd_set master_set, working_set;
    FD_ZERO(&master_set);
    max_sd = server_fd;
    FD_SET(server_fd, &master_set);
    printf("Server ready\n");
    while (1) {
        working_set = master_set;
        if (select(max_sd + 1, &working_set, NULL, NULL, NULL) < 0) {
            printf("Error in select\n");
            exit(1);
        }
        for (int i = 0; i <= max_sd; ++i) {
            if (FD_ISSET(i, &working_set)) {
                if (i == server_fd) {
                    new_socket = accept_client(server_fd);
                    struct User *user = (struct User *) malloc(sizeof(struct User));
                    user->fd = new_socket;
                    user->waiting_for_game = WAITING_FOR_NAME;
                    user->role = NONE;
                    sprintf(user->name, "User%d", new_socket);
                    add_user(user);
                    FD_SET(new_socket, &master_set);
                    if (new_socket > max_sd) {
                        max_sd = new_socket;
                    }
                    char *msg = "Welcome to the server. Please enter your name: ";
                    write(new_socket, msg, strlen(msg));
                } else {
                    struct User *current_user = get_user_by_fd(i);
                    int bytes_read = read(i, buffer, BUFFER_SIZE);
                    if (bytes_read <= 0) {
                        printf("Client %s disconnected\n", current_user->name);
                        close(i);
                        FD_CLR(i, &master_set);
                        continue;
                    }
                    buffer[bytes_read - 1] = '\0';
                    handle_client(current_user, buffer, bytes_read);
                }
            }
        }
    }
    return 0;
}


int setup_server(int port) {
    printf("Establishing connection on port %d\n", port);
    struct sockaddr_in address;
    int server_fd;
    int opt = 1;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        printf("Could not create socket\n");
        exit(1);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("Could not set socket options\n");
        exit(1);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        printf("Could not bind socket\n");
        exit(1);
    }
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        printf("Could not listen on socket\n");
        exit(1);
    }
    printf("Connection established\n");
    return server_fd;
}


int accept_client(int server_fd) {
    int client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    client_fd = accept(server_fd, (struct sockaddr *) &address, (socklen_t *) &addrlen);
    if (client_fd < 0) {
        printf("Could not accept client\n");
        exit(1);
    }
    printf("Client connected fd: %d\n", client_fd);
    return client_fd;
}

int handle_client(struct User *user, char *input, int input_length) {
    if (user->waiting_for_game == WAITING_FOR_NAME) {
        strncpy(user->name, input, NAME_MAX_LENGTH);
        user->waiting_for_game = 0;
        char msg[BUFFER_SIZE];
        sprintf(msg, "Welcome %s!\n", user->name);
        strncat(msg, get_user_selective_msg(), BUFFER_SIZE - strlen(msg));
        write(user->fd, msg, strlen(msg));
    } else if (user->role == NONE) {
        if (strcmp(input, "1") == 0) {
            user->role = PLAYER;
            char msg[BUFFER_SIZE];
            struct Game *new_game = get_pending_game(user);
            if (new_game->is_done == GAME_NOT_STARTED) {
                sprintf(msg, "Waiting for another player to join\n");
                send_msg(user->fd, msg);
            } else if (new_game->is_done == GAME_STARTED) {
                sprintf(msg, "%d:1 %d\n", GAME_STARTED_CODE_MESSAGE, new_game->port);
                send_msg(new_game->players[0]->fd, msg);
                memset(msg, 0, BUFFER_SIZE);
                sprintf(msg, "%d:2 %d\n", GAME_STARTED_CODE_MESSAGE, new_game->port);
                send_msg(new_game->players[1]->fd, msg);
            }
        } else if (strcmp(input, "2") == 0) {
            user->role = OBSERVER;
            char *msg = get_ready_to_watch_games();
            write(user->fd, msg, strlen(msg));
        } else {
            char msg[BUFFER_SIZE];
            sprintf(msg, "Invalid input\n");
            write(user->fd, msg, strlen(msg));
        }
    } else if (user->role == PLAYER) {
        char *copy = malloc(input_length + 1);
        strncpy(copy, input, input_length);
        char *token = strtok(copy, ":");
        if (token == NULL) {
            send_msg(user->fd, "Keep playing\n");
            free(copy);
            return 0;
        }
        if (strcmp(token, "292") == 0) {
            token = strtok(NULL, ":");
            char winner = token[0];
            struct Game *game = user->game;
            game_over(game, winner);
            send_msg(game->players[0]->fd, "Game over\n");
            send_msg(game->players[1]->fd, "Game over\n");
        }
    } else if (user->role == OBSERVER) {
        char *copy = malloc(input_length + 1);
        strncpy(copy, input, input_length);
        char *token = strtok(copy, ":");
        if (token == NULL) {
            send_msg(user->fd, "Keep watching\n");
            free(copy);
            return 0;
        }
        if (strcmp(token, "293") == 0) {
            token = strtok(NULL, ":");
            int game_port = atoi(token);
            struct Game *game = get_game_by_port(game_port);
            if (game == NULL) {
                send_msg(user->fd, "Game not found\n");
                free(copy);
                return 0;
            }
            user->game = game;
            add_observer(game, user);
        }
    }
    memset(input, 0, BUFFER_SIZE);
    return 0;
}

char *get_user_selective_msg() {
    char *msg = malloc(BUFFER_SIZE);
    sprintf(msg, "Please select your role:\n1. Player\n2. Observer\n");
    return msg;
}

struct Game *get_pending_game(struct User *player) {
    if (pending_games == NULL) {
        struct Game *game = malloc(sizeof(struct Game));
        game->is_done = GAME_NOT_STARTED;
        game->port = BASE_PORT_NUMBER + get_active_games_number();
        game->players[0] = player;
        player->game = game;
        pending_games = game;
        return game;
    }
    struct Game *game = pending_games;
    if (game->is_done == GAME_NOT_STARTED) {
        game->players[1] = player;
        game->is_done = GAME_STARTED;
        player->game = game;
        add_game(game);
        pending_games = NULL;
        return game;
    }
}

void add_game(struct Game *game) {
    struct GameNode *node = malloc(sizeof(struct GameNode));
    node->game = game;
    node->next = active_games;
    active_games = node;
}

int get_active_games_number() {
    int count = 0;
    struct GameNode *current = active_games;
    while (current != NULL) {
        count++;
        current = current->next;
    }
    return count;
}

void send_msg(int fd, char *msg) {
    write(fd, msg, strlen(msg));
}

char *get_ready_to_watch_games() {
    char *msg = malloc(BUFFER_SIZE);
    sprintf(msg, "%d:\n", GAME_WATCH_CODE_MESSAGE);
    struct GameNode *current = active_games;
    while (current != NULL) {
        char *temp = malloc(50);
        sprintf(temp, "\t%d: %s vs %s\n", current->game->port, current->game->players[0]->name,
                current->game->players[1]->name);
        strncat(msg, temp, BUFFER_SIZE - strlen(msg));
        current = current->next;
    }
    return msg;
}

void game_over(struct Game *game, char winner) {
    game->is_done = GAME_FINISHED;
    game->players[0]->game = NULL;
    game->players[1]->game = NULL;
    game->players[0]->role = NONE;
    game->players[1]->role = NONE;
    struct UserNode *current = game->observers;
    while (current != NULL) {
        send_msg(current->user->fd, "Game over\n");
        current->user->role = NONE;
        current = current->next;
    }
    remove_game(game);
    track_game_result(game, winner);
}

void remove_game(struct Game *game) {
    struct GameNode *current = active_games;
    struct GameNode *previous = NULL;
    while (current != NULL) {
        if (current->game == game) {
            if (previous == NULL) {
                active_games = current->next;
            } else {
                previous->next = current->next;
            }
            free(current);
            return;
        }
        previous = current;
        current = current->next;
    }
}

void track_game_result(struct Game *game, char winner) {
    char *file_path = "./result.txt";
    int fd = open(file_path, O_RDWR | O_APPEND | O_CREAT, 0666);
    char *msg = malloc(BUFFER_SIZE);
    sprintf(msg, "On port %d: %s vs %s\twinner: %c\n",
            game->port,
            game->players[0]->name,
            game->players[1]->name,
            winner);
    write(fd, msg, strlen(msg));
    close(fd);
    free(msg);
}

struct Game *get_game_by_port(int port) {
    struct GameNode *current = active_games;
    while (current != NULL) {
        if (current->game->port == port) {
            return current->game;
        }
        current = current->next;
    }
    return NULL;
}

void add_user(struct User *user) {
    struct UserNode *node = malloc(sizeof(struct UserNode));
    node->user = user;
    node->next = users;
    users = node;
}

struct User *get_user_by_fd(int fd) {
    struct UserNode *current = users;
    while (current != NULL) {
        if (current->user->fd == fd) {
            return current->user;
        }
        current = current->next;
    }
    return NULL;
}

void add_observer(struct Game *game, struct User *observer) {
    struct UserNode *node = malloc(sizeof(struct UserNode));
    node->user = observer;
    node->next = game->observers;
    game->observers = node;
}
