#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#include "socket.h"
#include "gameplay.h"


#ifndef PORT
#define PORT 56613
#endif
#define MAX_QUEUE 5

void announce_turn(struct game_state *game);

void broadcast(struct game_state *game, char *outbuf);

void advance_turn(struct game_state *game);

void leave_msg(struct game_state *game, char *name);

int Partial_read(struct client **player);

void add_player(struct client **top, int fd, struct in_addr addr);

void remove_player(struct client **top, int fd);

void game_lost(struct game_state *game);

void announce_winner(struct game_state *game, struct client *winner);

void anounce_game_status(struct game_state *game);

void new_game_msg(struct game_state *game);

void disconnect(struct game_state *game, struct client **p);

int Write(struct game_state *game, struct client **p, char *msg, char *addr);

/* The set of socket descriptors for select to monitor.
 * This is a global variable because we need to remove socket descriptors
 * from allset when a write to a socket fails.
 */
fd_set allset;

int main(int argc, char **argv) {
    //handle signal
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    int clientfd, maxfd, nready;
    struct client *p;
    struct sockaddr_in q;
    fd_set rset;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <dictionary filename>\n", argv[0]);
        exit(1);
    }

    // Create and initialize the game state
    struct game_state game;

    srandom((unsigned int) time(NULL));
    // Set up the file pointer outside of init_game because we want to
    // just rewind the file when we need to pick a new word
    game.dict.fp = NULL;
    game.dict.size = get_file_length(argv[1]);

    init_game(&game, argv[1]);

    // head and has_next_turn also don't change when a subsequent game is
    // started so we initialize them here.
    game.head = NULL;
    game.has_next_turn = NULL;

    /* A list of client who have not yet entered their name.  This list is
     * kept separate from the list of active players in the game, because
     * until the new playrs have entered a name, they should not have a turn
     * or receive broadcast messages.  In other words, they can't play until
     * they have a name.
     */
    struct client *new_players = NULL;

    struct sockaddr_in *server = init_server_addr(PORT);
    int listenfd = set_up_server_socket(server, MAX_QUEUE);

    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;

    struct client *winner;
    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;
        nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready == -1) {
            perror("select");
            continue;
        }

        if (FD_ISSET(listenfd, &rset)) {
            printf("A new client is connecting\n");
            clientfd = accept_connection(listenfd);

            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            printf("Connection from %s\n", inet_ntoa(q.sin_addr));
            add_player(&new_players, clientfd, q.sin_addr);
            char *greeting = WELCOME_MSG;
            if (write(clientfd, greeting, strlen(greeting)) == -1) {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                //originally p->fd
                remove_player(&(game.head), clientfd);
            };
        }

        /* Check which other socket descriptors have something ready to read.
         * The reason we iterate over the rset descriptors at the top level and
         * search through the two lists of clients each time is that it is
         * possible that a client will be removed in the middle of one of the
         * operations. This is also why we call break after handling the input.
         * If a client has been removed the loop variables may not longer be
         * valid.
         */
        int cur_fd;
        for (cur_fd = 0; cur_fd <= maxfd; cur_fd++) {
            if (FD_ISSET(cur_fd, &rset)) {
                // Check if this socket descriptor is an active player

                for (p = game.head; p != NULL; p = p->next) {
                    if (cur_fd == p->fd) {
                        //TODO - handle input from an active client
                        //read the move, store in inbuf
                        int num_read = Partial_read(&p);
                        //check whether this player disconnect.
                        if (num_read == 0) {
                            disconnect(&game, &p);
                            break;
                        }
                        //check whether read is finished
                        if (strstr(p->inbuf, "\r\n") == NULL) {
                            break;
                        } else {
                            p->inbuf[strlen(p->inbuf) - 2] = '\0';
                            p->in_ptr = p->inbuf;
                        }
                        //check whether it is this player's turn
                        if (game.has_next_turn->fd != cur_fd) {
                            char *invalid_msg = "It is not your turn\r\n";
                            if (Write(&game, &p, invalid_msg, inet_ntoa(q.sin_addr)) == -1) {
                                break;
                            }
                        } else {//if player have the turn, process input
                            char guess = (game.has_next_turn)->inbuf[0];
                            int i = guess - 'a';
                            int index;
                            //check whether a valid guess from a to z
                            if (guess < 'a' || guess > 'z' || strlen((game.has_next_turn)->inbuf) != 1) {
                                char invalid_guess[MAX_MSG];
                                sprintf(invalid_guess, "your guess should between a to z\r\n");
                                Write(&game, &p, invalid_guess, inet_ntoa(q.sin_addr));
                                break;
                            }
                            //check whether it is guessed already.
                            if ((game.letters_guessed)[i] == 1) {
                                char *invalid = "This letter is already guessed\r\n";
                                if (Write(&game, &p, invalid, inet_ntoa(q.sin_addr)) == -1) {
                                    break;
                                }
                            } else {
                                //whether we find this guess in the word
                                int found = 0;
                                game.letters_guessed[i] = 1;
                                for (index = 0; index < strlen(game.word); index++) {
                                    if (game.word[index] == guess) {
                                        found = 1;
                                        game.guess[index] = guess;
                                    }
                                }
                                char guess_msg[MAX_MSG];
                                sprintf(guess_msg, "%s has guessed %c.\r\n", (game.has_next_turn)->name, guess);
                                broadcast(&game, guess_msg);
                                //check whether game is finished
                                if (strcmp(game.word, game.guess) == 0) {
                                    winner = game.has_next_turn;
                                    announce_winner(&game, winner);
                                    //reset game
                                    init_game(&game, argv[1]);
                                    new_game_msg(&game);
                                    anounce_game_status(&game);
                                    break;
                                } else {
                                    if (!found) {
                                        game.guesses_left--;
                                        advance_turn(&game);
                                    }
                                    if (game.guesses_left == 0) {
                                        //announce the word.
                                        char word_msg[MAX_MSG];
                                        sprintf(word_msg, "the word is %s. \r\n", game.word);
                                        broadcast(&game, word_msg);
                                        //announce game lost to all player.
                                        game_lost(&game);
                                        init_game(&game, argv[1]);
                                        new_game_msg(&game);
                                        anounce_game_status(&game);
                                        break;
                                    }
                                    anounce_game_status(&game);
                                }
                            }
                        }
                        break;
                    }
                }

                // Check if any new players are entering their names
                for (p = new_players; p != NULL; p = p->next) {
                    if (cur_fd == p->fd) {
                        // TODO - handle input from an new client who has
                        // not entered an acceptable name.
                        //read the name msg
                        int num_read = Partial_read(&p);
                        if (num_read == 0) {
                            remove_player(&new_players, cur_fd);
                        }
                        //check whether read is finished
                        if (strstr(p->inbuf, "\r\n") == NULL) {
                            break;
                        } else {
                            //get rid of \r\n
                            p->inbuf[strlen(p->inbuf) - 2] = '\0';
                            p->in_ptr = p->inbuf;
                        }
                        int duplicate = 0;
                        //handle duplicate name
                        struct client *active;
                        for (active = game.head; active != NULL; active = active->next) {
                            if (strcmp(active->name, p->inbuf) == 0) {
                                duplicate = 1;
                                printf("invalid name\n");
                                char *msg = "invalid name\r\n";
                                if (write(cur_fd, msg, strlen(msg)) == -1) {
                                    remove_player(&new_players, cur_fd);
                                }
                                break;
                            }
                        }
                        if (duplicate == 0) {
                            //no duplicate remove from pending list
                            struct client *pending = new_players;
                            struct client *next;
                            //get the name;
                            char name[MAX_MSG];
                            name[0] = '\0';
                            strcpy(name, p->inbuf);
                            //base case, only one in the list
                            if (pending->next == NULL) {
                                new_players = NULL;
                                free(pending);
                            } else if (pending->fd == cur_fd) {
                                new_players = pending->next;
                                pending->next = NULL;
                                free(pending);
                            } else {
                                while (pending != NULL) {
                                    next = pending->next;
                                    if (next->fd == cur_fd) {
                                        pending->next = next->next;
                                        next->next = NULL;
                                        free(next);
                                        break;
                                    }
                                    pending = pending->next;
                                    next = next->next;
                                }
                            }
                            //add to game list
                            add_player(&(game.head), cur_fd, q.sin_addr);
                            strcpy((game.head)->name, name);
                            //first player.
                            if (game.has_next_turn == NULL) {
                                game.has_next_turn = game.head;
                            }
                            //announce that a player is joined
                            char msg[MAX_MSG];
                            sprintf(msg, "%s has joined the game \r\n", (game.head)->name);
                            printf("%s", msg);
                            broadcast(&game, msg);
                            //announce game status for new player.
                            char start_msg[MAX_MSG];
                            status_message(start_msg, &game);
                            if (write(cur_fd, start_msg, strlen(start_msg)) == -1) {
                                fprintf(stderr, "player's turn: Write to client %s failed\n", inet_ntoa(q.sin_addr));
                                disconnect(&game, &p);
                                break;
                            }
                            //notify whose turn is it for new player.
                            announce_turn(&game);
                        }
                        break;
                    }
                }
            }
        }
    }
    return 0;
}

/* Wrapper function with error checking.
*/
int Write(struct game_state *game, struct client **p, char *msg, char *addr) {
    int num;
    if ((num = write((*p)->fd, msg, strlen(msg))) == -1) {
        fprintf(stderr, "Write to client %s failed\n", addr);
        disconnect(game, p);
    } else if (num != strlen(msg)) {
        fprintf(stderr, "Write to client %s with incorrect number of bytes\n", addr);
    }
    return num;
}

/* Disconnect the closed player.
*/
void disconnect(struct game_state *game, struct client **p) {
    char name[MAX_MSG];
    name[0] = '\0';
    strcpy(name, (*p)->name);
    if (game->has_next_turn->fd == (*p)->fd) {
        advance_turn(game);
    }
    if ((game->has_next_turn)->fd == (*p)->fd) {
        game->has_next_turn = NULL;
    }
    remove_player(&(game->head), (*p)->fd);
    leave_msg(game, name);
    announce_turn(game);
}

/* Partial read from current player's file descriptor.
*/
int Partial_read(struct client **player) {
    int num_chars = read((*player)->fd, (*player)->in_ptr, MAX_BUF);
    (*player)->in_ptr = (*player)->in_ptr + num_chars;
    (*player)->in_ptr[0] = '\0';
    if (num_chars == 0) {
        return 0;
    } else {
        printf("[%d] read %d bytes\n", (*player)->fd, num_chars);
    }
    return num_chars;
}

/* These are some of the function prototypes that we used in our solution
 * You are not required to write functions that match these prototypes, but
 * you may find the helpful when thinking about operations in your program.
 */
/* Send the message in outbuf to all clients */
void broadcast(struct game_state *game, char *outbuf) {
    struct client *cur_player = game->head;
    while (cur_player != NULL) {
        if (write(cur_player->fd, outbuf, strlen(outbuf)) == -1) {
            disconnect(game, &cur_player);
        }
        cur_player = cur_player->next;
    }
}

/* Anounce game lost message.
*/
void game_lost(struct game_state *game) {
    char lose_msg[MAX_MSG];
    lose_msg[0] = '\0';
    strcpy(lose_msg, "game loses, no one is winner!!!\n\n\r\n");
    broadcast(game, lose_msg);
}

/* Announce the current turn.
*/
void announce_turn(struct game_state *game) {
    //printf("%s is here\n", game->has_next_turn->name);
    if (game->has_next_turn == NULL) {
        printf("no one is playing right now!\n");
    } else {
        printf("It is %s's turn.\n", game->has_next_turn->name);
        char msg[MAX_MSG];
        struct client *cur_player = game->head;
        while (cur_player != NULL) {
            if (cur_player->fd != (game->has_next_turn)->fd) {
                sprintf(msg, "It is %s's turn.\r\n", (game->has_next_turn)->name);
                if (write(cur_player->fd, msg, strlen(msg)) == -1) {
                    disconnect(game, &cur_player);
                }
            } else {//handle the case that player disconnect during it's turn
                sprintf(msg, "you guess?\r\n");
                if (write(cur_player->fd, msg, strlen(msg)) == -1) {
                    disconnect(game, &cur_player);
                }
            }
            cur_player = cur_player->next;
        }
    }
}

/* Anounce who is the winner.
*/
void announce_winner(struct game_state *game, struct client *winner) {
    char winner_msg[MAX_MSG];
    winner_msg[0] = '\0';
    strcpy(winner_msg, winner->name);
    strcat(winner_msg, " is the winner!\n\n\r\n");
    printf("%s\n", winner_msg);
    broadcast(game, winner_msg);
    char word_msg[MAX_MSG];
    sprintf(word_msg, "the word is %s. \r\n", game->word);
    broadcast(game, word_msg);
}

/* Move the has_next_turn pointer to the next active client */
void advance_turn(struct game_state *game) {
    if ((game->has_next_turn)->next == NULL) {
        game->has_next_turn = game->head;
    } else {
        game->has_next_turn = (game->has_next_turn)->next;
    }
}

/* Anounce game status
*/
void anounce_game_status(struct game_state *game) {
    char status_msg[MAX_MSG];
    status_msg[0] = '\0';
    char *broadcast_msg = status_message(status_msg, game);
    broadcast(game, broadcast_msg);
    announce_turn(game);
}

/* Add a client to the head of the linked list
 */
void add_player(struct client **top, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));

    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));

    p->fd = fd;
    p->ipaddr = addr;
    p->name[0] = '\0';
    p->in_ptr = p->inbuf;
    p->inbuf[0] = '\0';
    p->next = *top;
    *top = p;
}

/* Removes client from the linked list and closes its socket.
 * Also removes socket descriptor from allset
 */
void remove_player(struct client **top, int fd) {
    struct client **p;

    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next);
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        FD_CLR((*p)->fd, &allset);
        close((*p)->fd);
        free(*p);
        *p = t;
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                fd);
    }
}

/* Send leaving message
*/
void leave_msg(struct game_state *game, char *name) {
    char leave_msg[MAX_MSG];
    sprintf(leave_msg, "%s leaves the game! say goodbye to him\r\n", name);
    broadcast(game, leave_msg);
}

/* Send new game message
*/
void new_game_msg(struct game_state *game) {
    char new_game_msg[MAX_MSG];
    sprintf(new_game_msg, "Let's begin with a new game!\r\n");
    broadcast(game, new_game_msg);
}




