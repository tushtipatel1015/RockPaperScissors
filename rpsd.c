#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include "network.h"
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/select.h>

//MACROS
#define WAITING_PLAYERS 16  //maximum number of players that can wait in the queue to be accepted by the server 
#define NAME_LEN 64 //max size a players name can be
#define MAX_PLAYERS 1024

// GLOBAL VARS
int players_active = 0;
char players_active_arr[MAX_PLAYERS][NAME_LEN];

//STRUCTS
/*Player: 
    stores information on 
    players name, move, and turn
    for each game
*/
typedef struct{
    int fd; //file descriptr
    char player_name[NAME_LEN]; //players name gets stored
    char player_move[10]; //stores the players moves played during the round
    bool player_turn; //keeps track of players turn
} Player;


/* Game Current:
    tracks currently running games of server
*/
typedef struct{
    pid_t pid;  //process id of child process running game
    char player1[NAME_LEN]; //name of first player
    char player2[NAME_LEN]; //name of second player
} GameCurrent;

// MORE GLOBAL VARS
GameCurrent games_current[MAX_PLAYERS];
int games_current_count = 0;



//HELPER METHODS
/*Message:
    either recieves messages from player
        P: play, takes players name as argument 
        M: move, takes players move as argument
        C: continue
        Q: quit
    OR sends messages to player
        W: wait, sends 1 (version number) for successful connection
        B: begin, takes opposing players name as argument
        R: result, W for win, L for lose, D for draw, or F for forfeit
*/
bool message(int fd, char *buf, int buff_len, bool send_msg){
    if (send_msg){
        write(fd, buf, strlen(buf)); // writing message to player
        return true;
    } else{//taking in players message
        int len = 0;
        while (len < buff_len -1){
            int ret = read(fd, buf + len, 1); //reading form file descriptor
            if (ret <= 0){
                return false;  // failed to read message (player might have left)
            }
            len++; //moving to next part of message

            //when a complete message is received
            if ((len == 1 && (buf[0] == 'Q' || buf[0] == 'C')) ||(len >= 2 && buf[len - 2] == '|' &&buf[len - 1]== '|')){
                break; // exit
            }
        }
        buf[len] = '\0'; //adding terminator character
        return true; //at this point message was read successfully
    }
}
// Valid Move:
//    determines if a player sent a valid move
bool valid_move(const char *move){
    return strcmp(move, "ROCK") == 0 || strcmp(move, "PAPER") == 0 ||strcmp(move, "SCISSORS") == 0;
}


void to_uppercase(char *str){ //converts strings to uppercase
    for (int i = 0; str[i] != '\0'; i++){
        str[i] = toupper(str[i]);
    }
}

// Find Result:
//  determines the winner of the round
const char *find_result( char *player, char *opponent){
    //avoids letter casing comparisions
    to_uppercase(player);
    to_uppercase(opponent);


    if (strcmp(player, opponent) == 0){ //check for draw
        return "D";
    }
    //determining winner and loser of round
    if ((strcmp(player, "ROCK") == 0 && strcmp(opponent, "SCISSORS") == 0) ||(strcmp(player, "PAPER") == 0 && strcmp(opponent,"ROCK") == 0) || (strcmp(player,"SCISSORS") == 0 && strcmp(opponent,"PAPER") == 0)){
        return "W";
    }
    return "L";
}

// Name Check:
//  checks if player is already logged into server
bool name_check(Player *p1, Player *p2, const char *name){

    //checks if the new player matches player 1's name who is currently waiting
    if (p1 && p1->fd > 0 && strcmp(p1->player_name, name) == 0){
        return true;
    }

    //checks if the new player matches player 2's name who is currently waiting
    if (p2 && p2->fd > 0 && strcmp(p2->player_name, name) == 0){
        return true;
    }

    //goes through the array of active players and checks if the name exists in that
    for (int i = 0; i <players_active; i++){
        if (strcmp(players_active_arr[i], name) == 0){
            return true;
        }
    }
    return false;
}

// Add Player:
//  adds player to array of logged in players
void add_player(const char *name){

    //if statement to check if there is enough space, and then adds a players name only if the name does not already exist
    if (players_active < MAX_PLAYERS){
        strncpy(players_active_arr[players_active], name, NAME_LEN);
        players_active++;
    }
}


// Remove Player
//  removes player from array of logged in players
void remove_player(const char *name){

    //goes through the array to check for player's name
    for (int i = 0; i < players_active; i++){
        //this if statement is actually checking the players name and if it matches it keeps on going
        if (strcmp(players_active_arr[i], name) == 0){ 
            //moves the last player into another area reserved for a removed players' slot
            strncpy(players_active_arr[i], players_active_arr[players_active - 1], NAME_LEN);
            //this makes sure to clear the last slot and not lead to any unexpected behavior
            players_active_arr[players_active - 1][0] = '\0';
            players_active--;
            return;
        }
    }
}


/*Single Game:
    implementation of a singular game of RPS between 2 players
*/
void single_game(Player p1, Player p2){
    //prompting begin game messages to both players
    char b1[NAME_LEN +8], b2[NAME_LEN +8];
    snprintf(b1, sizeof(b1), "B|%s||", p2.player_name);
    snprintf(b2, sizeof(b2), "B|%s||", p1.player_name);
    message(p1.fd, b1, NAME_LEN, true);
    message(p2.fd, b2, NAME_LEN, true);
    
    bool active_game = true; // is true until a player quits or forfeits game
    while(active_game == true){ // keep playing until a player decides to quit/forfeit
        //getting the players moves
        for (int i = 0; i < 2; i++){
            char buf[NAME_LEN];
            // bool mssg_check;
            
            if (i == 0){ // player 1 move
                if (!message(p1.fd, buf, NAME_LEN, false) || strncmp(buf, "M|", 2) != 0) {
                    message(p2.fd, "R|F|||", NAME_LEN, true);
                    remove_player(p1.player_name);
                    remove_player(p2.player_name);
                    close(p1.fd);
                    close(p2.fd);
                    exit(EXIT_SUCCESS);
                }
                sscanf(buf, "M|%[^|]", p1.player_move);
                to_uppercase(p1.player_move);
                if (!valid_move(p1.player_move)) {
                    message(p2.fd, "R|F|||", NAME_LEN, true);
                    remove_player(p1.player_name);
                    remove_player(p2.player_name);
                    close(p1.fd);
                    close(p2.fd);
                    exit(EXIT_SUCCESS);
                }
            }else{ // player 2 move
                if (!message(p2.fd, buf, NAME_LEN, false) || strncmp(buf, "M|", 2) != 0) {
                    message(p1.fd, "R|F|||", NAME_LEN, true);
                    remove_player(p1.player_name);
                    remove_player(p2.player_name);
                    close(p1.fd);
                    close(p2.fd);
                    exit(EXIT_SUCCESS);
                }
                sscanf(buf, "M|%[^|]", p2.player_move);
                to_uppercase(p2.player_move);
                if (!valid_move(p2.player_move)){
                    message(p1.fd, "R|F|||", NAME_LEN, true);
                    remove_player(p1.player_name);
                    remove_player(p2.player_name);
                    close(p1.fd);
                    close(p2.fd);
                    exit(EXIT_SUCCESS);
                }
            }
        }

        if (active_game != true){ //messages that don't start with M| automatically end the game
            break;
        }

        //finding out the result of the match
        const char *r1 =find_result(p1.player_move, p2.player_move);
        const char *r2 =find_result(p2.player_move,p1.player_move);

        // sending results to the players
        char res1[NAME_LEN], res2[NAME_LEN];
        snprintf(res1, sizeof(res1), "R|%s|%s||", r1, p2.player_move);
        snprintf(res2, sizeof(res2), "R|%s|%s||", r2,p1.player_move);
        message(p1.fd, res1, NAME_LEN,true);
        message(p2.fd, res2, NAME_LEN,true); 


        
        bool continue1 = false, continue2 = false; //ask if players want to continue or quit
        for (int i = 0; i < 2; i++){
            char buf[NAME_LEN];
            bool mssg_check;
            if (i == 0) { // player 1
                mssg_check = message(p1.fd, buf, NAME_LEN, false);
                if (!mssg_check){
                    message(p2.fd, "R|F|||", NAME_LEN, true); //forfeit for any incorrect formats
                    remove_player(p1.player_name);
                    remove_player(p2.player_name);
                    close(p1.fd);
                    close(p2.fd);
                    exit(EXIT_SUCCESS);
                }else if(strcmp(buf, "Q") == 0){
                    active_game = false;
                    break;
                }else if(strcmp(buf, "C" ) == 0){
                    continue1 = true;
                } else{
                    active_game = false;
                    break;
                }
            } else{ // player 2
                mssg_check = message(p2.fd, buf, NAME_LEN, false);
                if (!mssg_check){
                    message(p1.fd, "R|F|||", NAME_LEN, true); //forfeit for any ill formats
                    remove_player(p1.player_name);
                    remove_player(p2.player_name);
                    close(p1.fd);
                    close(p2.fd);
                    exit(EXIT_SUCCESS);
                }else if(strcmp(buf, "Q") == 0){
                    active_game = false;
                    break;
                }else if(strcmp(buf, "C" ) == 0){
                    continue2 = true;
                }else{
                    active_game = false;
                    break;
                }
            }
        }
        
        // if both sent C, loop again; else end
        if (!(continue1 && continue2)){
            active_game = false;
        }
        if (!active_game){
            break;
        }

    }

    //remove_player(p1.player_name);
    // remove_player(p2.player_name);
    
    // exiting the game and clearing players
    close(p1.fd);
    close(p2.fd);
    exit(EXIT_SUCCESS);
}

// Handle Signal Child:
//  handling zombie (basically unneeded child proccesses are exited)
void handle_sigchld(int sig) {
    (void)sig;
    pid_t p;
    while ((p = waitpid(-1, NULL, WNOHANG)) > 0){ // we're finding dead children
        for (int i = 0; i < games_current_count; i++){ //going through all active games
            if (games_current[i].pid == p){ 
                //remove players from dead child
                remove_player(games_current[i].player1);
                remove_player(games_current[i].player2);

                //remaining active games are reordered to make room 
                games_current[i] = games_current[games_current_count - 1];
                games_current_count--;
            }
            }
    }
    
}

/*
    Main:
        this sets up the game server to accept different players and then lets them play
        Rock, Paper, Scissors using different methods
*/

int main(int argc, char **argv) {
    //to see if the argument is even valid and to avoid unnecessary running of code
    if (argc != 2) {
        fprintf(stderr, "Usage: %s port\n", argv[0]);
        return EXIT_FAILURE;
    }

    //opens the socket for the server requested in the main terminal when testing the code
    int input_taker = open_listener(argv[1], WAITING_PLAYERS);
    if (input_taker < 0) {
        return EXIT_FAILURE;
    }
    
    //this sets up the SIGCHILD handling stuff to take off the zombie child processes
    signal(SIGCHLD, handle_sigchld); // reap zombie children

    Player players_waiting[MAX_PLAYERS];   //this is an array for all the players waiting to get connected in line
    int waiting_count = 0;  //keeps track of the number of players waiting in line currently

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds); //this just clears all the fds from the set
        FD_SET(input_taker, &read_fds); //this puts the socket that is going to listen to the client commands in the set
        int highest_fd = input_taker;

        //this for loop adds all the waiting player sokcets to the read set
        for (int i = 0; i < waiting_count; i++) {
            FD_SET(players_waiting[i].fd, &read_fds);   //this just adds each player's socket fd to the set kept by the select() method
            //updates the maximum fd number so the select() method knows how to many to check
            if (players_waiting[i].fd > highest_fd)
                highest_fd = players_waiting[i].fd;
        }

        int activity = select(highest_fd + 1, &read_fds, NULL, NULL, NULL);  //uses the select() method to wait for any of the monitored fds to become ready, and highest_fd + 1 is the highest fd value + 1
        if (activity < 0) {
            perror("select error");
            continue;
        }

        //this statement is put to basically handle any new connections
        if (FD_ISSET(input_taker, &read_fds)) {
            struct sockaddr_storage client_add;   //this is a struct to hold the client's address
            socklen_t len = sizeof(client_add);
            int fd = accept(input_taker, (struct sockaddr *)&client_add, &len); //this line basically uses accept() method to accept any new connection
            if (fd < 0){
                perror("accept");
                continue;
            }

            //this chunk of code waits for a player to send their name once connected to the server and handles everything after
            char buff[NAME_LEN];
            if (message(fd, buff, NAME_LEN, false) && strncmp(buff, "P|", 2) == 0) {
                char name[NAME_LEN];
                sscanf(buff, "P|%[^|]", name);  //scans the players name in this format

                //this checks for if there is any duplicate players in the list of names being maintained
                if (name_check(NULL, NULL, name)){
                    //if the name is already in the list, then send a loss message along with a message that says logged in formated like this
                    message(fd, "R|L|Logged in||", NAME_LEN, true);
                    close(fd);
                    continue;   //this just makes sure that the code just goes back to waiting for more players
                }

                //creates a new player struct for the incoming connection
                Player new_player = {0};  // zero out the struct
                new_player.fd = fd;
                strcpy(new_player.player_name, name);
                new_player.player_turn = false;
                add_player(name);   //adds the player's name to global list of the active players created in the beginning of the code
                message(fd, "W|1||", NAME_LEN, true);


                players_waiting[waiting_count++] = new_player;  //add the new player to the waiting list to match it with some other player
            }
            else{
                close(fd);
            }
        }

        //checking for disconnected players
        for (int i = 0; i <waiting_count; i++){ //going through players 
            if (FD_ISSET(players_waiting[i].fd, &read_fds)){
                char tmp;
                if(read(players_waiting[i].fd, &tmp, 1) == 0){
                    remove_player(players_waiting[i].player_name); //removing if they disconnected
                    close(players_waiting[i].fd) ;
                    players_waiting[i] = players_waiting[--waiting_count];
                    i--;
                }
            }
        }


        while (waiting_count >= 2){ //going through list of waitng players and connecting them to each other 
            Player p1 = players_waiting[0];
            Player p2 = players_waiting[1];

            // updating list as players get paired
            for (int i = 2; i < waiting_count; i++) {
                players_waiting[i - 2] = players_waiting[i];
            }
            waiting_count -= 2;
            
            pid_t pid = fork(); //children games depending on if multiple concurrent games should be necessary 
            if (pid == 0) {
                close(input_taker);
                single_game(p1,p2);

            }else if (pid > 0){ //adding a new game for another pair
                games_current[games_current_count].pid = pid;
                strncpy(games_current[games_current_count].player1, p1.player_name, NAME_LEN);
                strncpy(games_current[games_current_count].player2 , p2.player_name, NAME_LEN);
                games_current_count++;
                close(p1.fd);
                close(p2.fd);
            }else{
                perror("fork failed");
                close(p1.fd);
                close(p2.fd);
            }
        }
    }

    close(input_taker);
    return EXIT_SUCCESS;
}
