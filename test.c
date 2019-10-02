char lose_msg[MAX_MSG];
lose_msg[0] = '\0';
strcpy(lose_msg, "game loses, no one is winner!!!\r\n");
broadcast(&game, lose_msg);

void game_lost(struct game_state *game){
	char lose_msg[MAX_MSG];
	lose_msg[0] = '\0';
	strcpy(lose_msg, "game loses, no one is winner!!!\r\n");
	broadcast(game, lose_msg);
}

char status_msg[MAX_MSG];
status_msg[0] = '\0';
char *broadcast_msg = status_message(status_msg, &game);
broadcast(&game, broadcast_msg);
announce_turn(&game);

void anounce_game_status(struct game_state *game){
	char status_msg[MAX_MSG];
	status_msg[0] = '\0';
	char *broadcast_msg = status_message(status_msg, &game);
	broadcast(game, broadcast_msg);
	announce_turn(game);
}

char leave_msg[MAX_MSG];
sprintf(leave_msg, "%s leaves the game! say goodbye to him\r\n", p->name);
broadcast(&game, leave_msg);

void leave_msg(struct game_state *game, struct client *p){
	char leave_msg[MAX_MSG];
	sprintf(leave_msg, "%s leaves the game! say goodbye to him\r\n", p->name);
	broadcast(&game, leave_msg);
}
if(num_read == 0){
    remove_player(&(game.head), cur_fd);
    leave_msg(&game, p);
    if(game.has_next_turn->fd == p->fd){
        advance_turn(&game);
    }
    announce_turn(&game);
    break;
}
if(strstr(p->inbuf, "\r\n") == NULL){
    break;
}else{
    p->in_ptr = p->inbuf;
}

void check_disconnection(int num_read, struct game_state *game, struct client *p){
	if(num_read == 0){
	    remove_player(&(game->head), p->fd);
	    leave_msg(game, p);
	    if(game->has_next_turn->fd == p->fd){
	        advance_turn(game);
	    }
	    announce_turn(game);
	    break;
	}
	if(strstr(p->inbuf, "\r\n") == NULL){
	    break;
	}else{
	    p->in_ptr = p->inbuf;
	}
}

char new_game_msg[MAX_MSG];
sprintf(new_game_msg, "Let's begin with a new game!\r\n");
broadcast(&game, new_game_msg);

void new_game_msg(struct game_state *game){
	char new_game_msg[MAX_MSG];
    sprintf(new_game_msg, "Let's begin with a new game!\r\n");
    broadcast(game, new_game_msg);
}

char msg[MAX_MSG];
sprintf(msg, "%s has joined the game \r\n", (game.head)->name);
printf("%s", msg);
broadcast(&game, msg);

void join_msg(struct game_state *game){
	char msg[MAX_MSG];
	sprintf(msg, "%s has joined the game \r\n", (game->head)->name);
	broadcast(game, msg);
}