#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define FIRST_TURN 'X'
#define SECOND_TURN 'O'

int connect_server(int port) {
    int fd;
    struct sockaddr_in server_address;

    fd = socket(AF_INET, SOCK_STREAM, 0);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) { // checking for errors
        printf("Error in connecting to server\n");
    }

    return fd;
}

char board[3][3] = {
        {'1', '2', '3'},
        {'4', '5', '6'},
        {'7', '8', '9'}
};

struct Game {
    int port;
    char *msg;
};

struct Game games[30];

char turn = FIRST_TURN;
char current_player = FIRST_TURN;
int port_game;
struct sockaddr_in bc_addr;
int server_fd;

void xo(int x, int y, char c) {
    board[x - 1][y - 1] = c;
}

int is_game_done() {
    int i, j;
    for (i = 0; i < 3; i++) {
        if (board[i][0] == board[i][1] && board[i][1] == board[i][2]) {
            return 1;
        }
    }
    for (j = 0; j < 3; j++) {
        if (board[0][j] == board[1][j] && board[1][j] == board[2][j]) {
            return 1;
        }
    }
    if (board[0][0] == board[1][1] && board[1][1] == board[2][2]) {
        return 1;
    }
    if (board[0][2] == board[1][1] && board[1][1] == board[2][0]) {
        return 1;
    }

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (board[i][j] == '1' || board[i][j] == '2' || board[i][j] == '3' || board[i][j] == '4' ||
                board[i][j] == '5' || board[i][j] == '6' || board[i][j] == '7' || board[i][j] == '8' ||
                board[i][j] == '9') {
                return 0;
            }
        }
    }

    return 1;
}

char get_the_winner() {
    int i, j;
    for (i = 0; i < 3; i++) {
        if (board[i][0] == board[i][1] && board[i][1] == board[i][2]) {
            return board[i][0];
        }
    }
    for (j = 0; j < 3; j++) {
        if (board[0][j] == board[1][j] && board[1][j] == board[2][j]) {
            return board[0][j];
        }
    }
    if (board[0][0] == board[1][1] && board[1][1] == board[2][2]) {
        return board[0][0];
    }
    if (board[0][2] == board[1][1] && board[1][1] == board[2][0]) {
        return board[0][2];
    }
    return ' ';
}

int is_game_started(char *msg) {
    char *copy = malloc(strlen(msg) + 1);
    strcpy(copy, msg);
    char *token;
    token = strtok(copy, ":");
    if (token == NULL) {
        free(copy);
        return 0;
    }
    if (strcmp(token, "291") == 0) {
        free(copy);
        return 1;
    }
    free(copy);
    return 0;
}

int is_game_watchlist(char *msg) {
    char *copy = malloc(strlen(msg) + 1);
    strcpy(copy, msg);
    char *token;
    token = strtok(copy, ":");
    if (token == NULL) {
        free(copy);
        return 0;
    }
    if (strcmp(token, "295") == 0) {
        free(copy);
        return 1;
    }
    free(copy);
    return 0;
}

void extract_game_watchlist(char *msg) {
    char *copy = malloc(strlen(msg) + 1);
    strcpy(copy, msg);
    char *token;
    strtok(copy, ":");
    token = strtok(NULL, "\n");
    if (token == NULL) {
        free(copy);
        return;
    }
    memset(games, 0, sizeof(games));
    int i = 0;
    while (token != NULL) {
        int port;
        char *player1 = malloc(strlen(token) + 1);
        char *player2 = malloc(strlen(token) + 1);
        sscanf(token, "\t%d: %s vs %s", &port, player1, player2);
        struct Game game;
        game.port = port;
        game.msg = malloc(strlen(player1) + strlen(player2) + 6);
        game.msg = strcat(game.msg, player1);
        game.msg = strcat(game.msg, " vs ");
        game.msg = strcat(game.msg, player2);
        games[i] = game;
        i++;
        token = strtok(NULL, "\n");
    }
    free(copy);
}

void print_game_watchlist() {
    char msg[1024];
    for (int i = 0; i < 30; ++i) {
        if (games[i].msg != NULL) {
            sprintf(msg, "%d: %s\n", i + 1, games[i].msg);
            write(STDOUT_FILENO, msg, strlen(msg));
            memset(msg, 0, sizeof(msg));
        }
    }
    write(STDOUT_FILENO, "Select game to join: ", strlen("Select game to join: "));
}

int connect_game_broadcast() {
    int sock, broadcast = 1, opt = 1;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bc_addr.sin_family = AF_INET;
    bc_addr.sin_port = htons(port_game);
    bc_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    if (bind(sock, (struct sockaddr *) &bc_addr, sizeof(bc_addr)) < 0) {
        printf("Error in binding\n");
        exit(1);
    }
    return sock;
}

void print_board() {
    char board_str[1024];
    sprintf(board_str, "     |     |     \n");
    int i;
    for (i = 0; i < 3; i++) {
        char row[1024];
        sprintf(row, "  %c  |  %c  |  %c  \n", board[i][0], board[i][1], board[i][2]);
        strncat(row, "_____|_____|_____\n", 18);
        strncat(row, "     |     |     \n", 18);
        strncat(board_str, row, strlen(row));
    }
    strncat(board_str, "     |     |     \n", 18);
    write(1, board_str, strlen(board_str));
}

void send_game_result_to_server() {
    char *msg = malloc(1024);
    sprintf(msg, "292:%c\n", get_the_winner());
    send(server_fd, msg, strlen(msg), 0);
    free(msg);
}

void send_watching_game_to_server() {
    char *msg = malloc(1024);
    sprintf(msg, "293:%d\n", port_game);
    send(server_fd, msg, strlen(msg), 0);
    free(msg);
}

void watch_game(int game_index) {
    port_game = games[game_index].port;
    send_watching_game_to_server();
    int game_fd = connect_game_broadcast();
    char buffer[1024];
    while (is_game_done() == 0) {
        print_board();
        char c;
        int x, y;
        memset(buffer, 0, 1024);
        recvfrom(game_fd, buffer, 1024, 0, (struct sockaddr *) &bc_addr, sizeof(bc_addr));
        sscanf(buffer, "%d:%d:%c", &x, &y, &c);
        xo(x, y, c);
        memset(buffer, 0, 1024);
    }
    print_board();
    char winner = get_the_winner();
    if (winner == ' ') {
        write(1, "Draw!\n", 6);
    } else {
        write(1, "Winner is ", 10);
        write(1, &winner, 1);
        write(1, "\n", 1);
    }
}

void start_game() {
    int game_fd = connect_game_broadcast();
    char buffer[1024];
    while (is_game_done() == 0) {
        print_board();
        if (turn == current_player) {
            write(1, "Your turn: ", 10);
            read(0, buffer, 1024);
            int x, y;
            sscanf(buffer, "%d %d", &x, &y);
            if (x < 1 || x > 3 || y < 1 || y > 3) {
                write(1, "Invalid move\n", 13);
                continue;
            }
            if (board[x - 1][y - 1] == FIRST_TURN || board[x - 1][y - 1] == SECOND_TURN) {
                write(1, "Invalid move\n", 13);
                continue;
            }
            xo(x, y, current_player);
            current_player = (current_player == FIRST_TURN) ? SECOND_TURN : FIRST_TURN;
            memset(buffer, 0, 1024);
            sprintf(buffer, "%d:%d:%c", x, y, turn);
            int a = sendto(game_fd, buffer, strlen(buffer), 0,
                           (struct sockaddr *) &bc_addr,
                           sizeof(bc_addr));
            printf("%d bytes sent!\n", a);
        } else {
            char c;
            int x, y;
            memset(buffer, 0, 1024);
            printf("Waiting for %c\n", current_player);
            recvfrom(game_fd, buffer, 1024, 0, (struct sockaddr *) &bc_addr, sizeof(bc_addr));
            printf("INPUT: %s\n", buffer);
            sscanf(buffer, "%d:%d:%c", &x, &y, &c);
            if (c == turn)
                continue;
            xo(x, y, c);
            current_player = turn;
            memset(buffer, 0, 1024);
        }
    }
    print_board();
    char winner = get_the_winner();
    if (winner == ' ') {
        write(1, "Draw!\n", 6);
    } else {
        write(1, "Winner is ", 10);
        write(1, &winner, 1);
        write(1, "\n", 1);
    }
    if (turn == winner) {
        write(1, "You won!\n", 9);
    } else {
        write(1, "You lost!\n", 10);
    }

    if (turn == current_player) {
        send_game_result_to_server();
    }
}

void extract_game_turn_and_port(char *msg) {
    char *copy = malloc(strlen(msg) + 1);
    strcpy(copy, msg);
    copy[strlen(msg) - 1] = '\0';
    char *token;
    strtok(copy, ":");
    token = strtok(NULL, ":");
    token = strtok(token, " ");
    if (token == NULL) {
        free(copy);
        return;
    }
    if (strcmp(token, "1") == 0) {
        turn = FIRST_TURN;
    } else {
        turn = SECOND_TURN;
    }
    token = strtok(NULL, " ");
    port_game = atoi(token);
    free(copy);
}


int main(int argc, char const *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_port>\n", argv[0]);
        exit(1);
    }
    int server_port = atoi(argv[1]);
    char buff[1024] = {0};

    server_fd = connect_server(server_port);
    int max_sd;
    fd_set master_set, working_set;
    FD_ZERO(&master_set);
    max_sd = server_fd;
    FD_SET(server_fd, &master_set);
    FD_SET(STDIN_FILENO, &master_set);

    while (1) {
        working_set = master_set;
        if (select(max_sd + 1, &working_set, NULL, NULL, NULL) < 0) {
            printf("Error in select\n");
            exit(1);
        }
        for (int i = max_sd; i >= 0; --i) {
            if (FD_ISSET(i, &working_set)) {
                if (i == 0) {
                    read(0, buff, 1024);
                    if (strcmp(buff, "exit\n") == 0) {
                        close(server_fd);
                        exit(0);
                    }
                    write(server_fd, buff, strlen(buff));
                    memset(buff, 0, 1024);
                } else if (i == server_fd) {
                    read(server_fd, buff, 1024);
                    if (is_game_started(buff) == 1) {
                        extract_game_turn_and_port(buff);
                        char msg[1024] = {0};
                        sprintf(msg, "Game started, your turn: %c\n", turn);
                        write(1, msg, strlen(msg));
                        memset(buff, 0, 1024);
                        start_game();
                        continue;
                    } else if (is_game_watchlist(buff) == 1) {
                        extract_game_watchlist(buff);
                        print_game_watchlist();
                        memset(buff, 0, 1024);
                        read(STDIN_FILENO, buff, 1024);
                        int index;
                        sscanf(buff, "%d", &index);
                        while (games[index - 1].msg == NULL) {
                            write(STDOUT_FILENO, "Invalid game number\n", 24);
                            write(STDOUT_FILENO, "Select game to join: ", strlen("Select game to join: "));
                            memset(buff, 0, 1024);
                            read(STDIN_FILENO, buff, 1024);
                            sscanf(buff, "%d", &index);
                        }
                        watch_game(index - 1);
                        continue;
                    }
                    write(1, buff, strlen(buff));
                    memset(buff, 0, 1024);
                }
            }
        }
    }

    return 0;
}