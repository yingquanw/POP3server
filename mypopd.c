#include "netbuffer.h"
#include "mailuser.h"
#include "server.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024


typedef enum state {
    Undefined,
    AUTHORIZATION,
    TRANSACTION,
    UPDATE
} State;

typedef struct serverstate {
    int fd;
    net_buffer_t nb;
    char recvbuf[MAX_LINE_LENGTH + 1];
    char *words[MAX_LINE_LENGTH];
    int nwords;
    State state;
    struct utsname my_uname;
    char currUser[MAX_LINE_LENGTH + 1];
    mail_list_t mail_list;
    // TODO: Add additional fields as necessary
} serverstate;
static void handle_client(int fd);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Invalid arguments. Expected: %s <port>\n", argv[0]);
        return 1;
    }
    run_server(argv[1], handle_client);
    return 0;
}

// syntax_error returns
//   -1 if the server should exit
//    1 otherwise
int syntax_error(serverstate *ss) {
    if (send_formatted(ss->fd, "-ERR %s\r\n", "Syntax error in parameters or arguments") <= 0) return -1;
    return 1;
}

// checkstate returns
//   -1 if the server should exit
//    0 if the server is in the appropriate state
//    1 if the server is not in the appropriate state
int checkstate(serverstate *ss, State s) {
    if (ss->state != s) {
        if (send_formatted(ss->fd, "-ERR %s\r\n", "Bad sequence of commands") <= 0) return -1;
        return 1;
    }
    return 0;
}

char *strcatarray(char * dest, char *strings[], size_t number) {
    dest[0] = '\0';
    for (size_t i = 1; i < number; i++) {
        strcat(dest, strings[i]);
    }
    printf("%s\n", dest);
    return dest;
}

// All the functions that implement a single command return
//   -1 if the server should exit
//    0 if the command was successful
//    1 if the command was unsuccessful

int do_quit(serverstate *ss) {
    dlog("Executing quit\n");
    // TODO: Implement this function
    if (ss->nwords != 1) {
        send_formatted(ss->fd, "-ERR invalid args\r\n");
        return 1;
    }
    if (ss->state == AUTHORIZATION) {
        if (send_formatted(ss->fd, "+OK Service closing transmission channel\r\n") <= 0) {
            return 1;
        }
    }
    if (ss->state == TRANSACTION) {
        ss->state = UPDATE;
        if (mail_list_destroy(ss->mail_list)) {
            send_formatted(ss->fd, "-ERR deletion error\r\n");
        }
        if (send_formatted(ss->fd, "+OK Service closing transmission channel\r\n") <= 0) {
            return 1;
        }
    }
    return -1;
}

int do_user(serverstate *ss) {
    dlog("Executing user\n");
    // TODO: Implement this function
    if (checkstate(ss, AUTHORIZATION)) {
        return 1;
    }
    if (ss->nwords != 2) {
        send_formatted(ss->fd, "-ERR Sorry, invalid args\r\n");
        return 1;
    }
    if (!is_valid_user(ss->words[1], NULL)) {
        send_formatted(ss->fd, "-ERR Sorry, no user called %s\r\n", ss->words[1]);
        return 1;
    }
    if (send_formatted(ss->fd, "+OK Please enter the password %s\r\n", ss->words[1]) <= 0) {
        return 1;
    }
    strcpy(ss->currUser, ss->words[1]);
    return 0;
}

int do_pass(serverstate *ss) {
    dlog("Executing pass\n");
    // TODO: Implement this function
    if (checkstate(ss, AUTHORIZATION)) {
        return 1;
    }
    if (!strcmp(ss->currUser, "empty")) {
        send_formatted(ss->fd, "-ERR Sorry, no user\r\n");
        return 1;
    }
    if (ss->nwords < 2) {
        send_formatted(ss->fd, "-ERR Sorry, invalid args\r\n");
        return 1;
    }
    char password[MAX_LINE_LENGTH+1];
    if (!is_valid_user(ss->currUser, strcatarray(password, ss->words, ss->nwords))) {
        send_formatted(ss->fd, "-ERR Sorry, wrong password\r\n");
        return 1;
    }
    if (send_formatted(ss->fd, "+OK Welcome %s\r\n", ss->currUser) <= 0) {
        return 1;
    }
    ss->mail_list = NULL;
    ss->state = TRANSACTION;
    return 0;
}

int do_stat(serverstate *ss) {
    dlog("Executing stat\n");
    // TODO: Implement this function
    if (checkstate(ss, TRANSACTION)) {
        return 1;
    }
    if (ss->nwords != 1) {
        send_formatted(ss->fd, "-ERR Sorry, invalid args\r\n");
        return 1;
    }
    if (ss->mail_list == NULL) {
        ss->mail_list = load_user_mail(ss->currUser);
    }
    int length = mail_list_length(ss->mail_list, 0);
    int size = mail_list_size(ss->mail_list);
    if (send_formatted(ss->fd, "+OK %d %d\r\n", length, size) <= 0) {
        return 1;
    }
    return 0;
}

//void printMailSummary(serverstate *ss, mail_item_t mail, int pos) {
//    int size = (int) mail_item_size(mail);
//    send_formatted(ss->fd, "%d %d\r\n", pos, size);
//}

int do_list(serverstate *ss) {
    dlog("Executing list\n");
    if (checkstate(ss, TRANSACTION)) {
        return 1;
    }
    if (ss->nwords > 2) {
        send_formatted(ss->fd, "-ERR invalid args\r\n");
        return 1;
    }
    if (ss->mail_list == NULL) {
        ss->mail_list = load_user_mail(ss->currUser);
    }
    if (ss->nwords == 1) {
        int numBeforeDel = (int) mail_list_length(ss->mail_list, 0);
        if (send_formatted(ss->fd, "+OK %d messages\r\n", numBeforeDel) <= 0) {
            return 1;
        }
        int numAfterDel = (int) mail_list_length(ss->mail_list, 1);
        for (int i = 0; i < numAfterDel; i++) {
            mail_item_t mail = mail_list_retrieve(ss->mail_list, i);
            if (mail) {
                int size = (int) mail_item_size(mail);
                send_formatted(ss->fd, "%d %d\r\n", i + 1, size);
            }
        }
        if (send_formatted(ss->fd, "%s\r\n", ".") <= 0) {
            return 1;
        }
    } else {
        int mailNum = atoi(ss->words[1]);
        if (!mailNum) {
            send_formatted(ss->fd, "-ERR Sorry, not a number\r\n");
            return 1;
        }
        mail_item_t mail = mail_list_retrieve(ss->mail_list, mailNum - 1);
        if (!mail) {
            send_formatted(ss->fd, "-ERR Sorry, not such message\r\n");
            return 1;
        }
        int size = (int) mail_item_size(mail);
        send_formatted(ss->fd, "+OK %d %d\r\n", mailNum, size);
    }
    return 0;
}


int do_retr(serverstate *ss) {
    dlog("Executing retr\n");
    if (checkstate(ss, TRANSACTION)) {
        return 1;
    }
    if (ss->nwords != 2) {
        send_formatted(ss->fd, "-ERR invalid args\r\n");
        return 1;
    }
    int mailNum = atoi(ss->words[1]);
    if (!mailNum) {
        send_formatted(ss->fd, "-ERR Sorry, not a number\r\n");
        return 1;
    }
    if (ss->mail_list == NULL) {
        ss->mail_list = load_user_mail(ss->currUser);
    }
    mail_item_t mail = mail_list_retrieve(ss->mail_list, mailNum - 1);
    if (!mail) {
        send_formatted(ss->fd, "-ERR Sorry, not such message\r\n");
        return 1;
    }
    if (send_formatted(ss->fd, "+OK Message follows\r\n") <= 0) {
        return 1;
    }
    FILE *file = mail_item_contents(mail);
    char buffer[1025];
    size_t bytes_read;
    bytes_read = fread(buffer, 1, sizeof(buffer), file);
    while (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        send_formatted(ss->fd, "%s", buffer);
        bytes_read = fread(buffer, 1, sizeof(buffer), file);
    }
    fclose(file);
    if (send_formatted(ss->fd, "%s\r\n", ".") <= 0) {
        return 1;
    }
    return 0;
}

int do_rset(serverstate *ss) {
    dlog("Executing rset\n");
    if (checkstate(ss, TRANSACTION)) {
        return 1;
    }
    if (ss->nwords != 1) {
        send_formatted(ss->fd, "-ERR invalid args\r\n");
        return 1;
    }
    int numRestored = mail_list_undelete(ss->mail_list);
    if (send_formatted(ss->fd, "+OK %d messages restored\r\n", numRestored) <= 0) {
        return 1;
    }
    return 0;
}
int do_noop(serverstate *ss) {
    dlog("Executing noop\n");
    if (checkstate(ss, TRANSACTION)) {
        return 1;
    }
    if (ss->nwords != 1) {
        send_formatted(ss->fd, "-ERR invalid args\r\n");
        return 1;
    }
    if (send_formatted(ss->fd, "+OK NOOP\r\n") <= 0) {
        return 1;
    }
    return 0;
}

int do_dele(serverstate *ss) {
    dlog("Executing dele\n");
    if (checkstate(ss, TRANSACTION)) {
        return 1;
    }
    if (ss->nwords != 2) {
        send_formatted(ss->fd, "-ERR Sorry, invalid number of args\r\n");
        return 1;
    }
    int mailNum = atoi(ss->words[1]);
    if (!mailNum) {
        send_formatted(ss->fd, "-ERR Sorry, not a number\r\n");
        return 1;
    }
    if (ss->mail_list == NULL) {
        ss->mail_list = load_user_mail(ss->currUser);
    }
    mail_item_t mail = mail_list_retrieve(ss->mail_list, mailNum - 1);
    if (!mail) {
        send_formatted(ss->fd, "-ERR Sorry, no such mail item\r\n");
        return 1;
    }
    mail_item_delete(mail);
    if (send_formatted(ss->fd, "+OK Message deleted\r\n") <= 0) {
        return 1;
    }
    return 0;
}


void handle_client(int fd) {
    size_t len;
    serverstate mstate, *ss = &mstate;
    ss->fd = fd;
    ss->nb = nb_create(fd, MAX_LINE_LENGTH);
    ss->state = Undefined;
    uname(&ss->my_uname);
    if (send_formatted(fd, "+OK POP3 Server on %s ready\r\n", ss->my_uname.nodename) <= 0) return;
    ss->state = AUTHORIZATION;
    strcpy(ss->currUser, "empty");
    while ((len = nb_read_line(ss->nb, ss->recvbuf)) >= 0) {
        if (ss->recvbuf[len - 1] != '\n') {
            // command line is too long, stop immediately
            send_formatted(fd, "-ERR Syntax error, command unrecognized\r\n");
            break;
        }
        if (strlen(ss->recvbuf) < len) {
            // received null byte somewhere in the string, stop immediately.
            send_formatted(fd, "-ERR Syntax error, command unrecognized\r\n");
            break;
        }
        // Remove CR, LF and other space characters from end of buffer
        while (isspace(ss->recvbuf[len - 1])) ss->recvbuf[--len] = 0;
        dlog("Command is %s\n", ss->recvbuf);
        if (strlen(ss->recvbuf) == 0) {
            send_formatted(fd, "-ERR Syntax error, blank command unrecognized\r\n");
            break;
        }
        // Split the command into its component "words"
        ss->nwords = split(ss->recvbuf, ss->words);
        char *command = ss->words[0];
        if (!strcasecmp(command, "QUIT")) {
            if (do_quit(ss) == -1) break;
        } else if (!strcasecmp(command, "USER")) {
            if (do_user(ss) == -1) break;
        } else if (!strcasecmp(command, "PASS")) {
            if (do_pass(ss) == -1) break;
        } else if (!strcasecmp(command, "STAT")) {
            if (do_stat(ss) == -1) break;
        } else if (!strcasecmp(command, "LIST")) {
            if (do_list(ss) == -1) break;
        } else if (!strcasecmp(command, "RETR")) {
            if (do_retr(ss) == -1) break;
        } else if (!strcasecmp(command, "RSET")) {
            if (do_rset(ss) == -1) break;
        } else if (!strcasecmp(command, "NOOP")) {
            if (do_noop(ss) == -1) break;
        } else if (!strcasecmp(command, "DELE")) {
            if (do_dele(ss) == -1) break;
        } else if (!strcasecmp(command, "TOP") ||
                   !strcasecmp(command, "UIDL") ||
                   !strcasecmp(command, "APOP")) {
            dlog("Command not implemented %s\n", ss->words[0]);
            if (send_formatted(fd, "-ERR Command not implemented\r\n") <= 0) break;
        } else {
            // invalid command
            if (send_formatted(fd, "-ERR Syntax error, command unrecognized\r\n") <= 0) break;
        }
    }
    nb_destroy(ss->nb);
}
