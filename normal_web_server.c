#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>
#include<netdb.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<signal.h>

#define MAX_CHARS 99999
#define CLIENTS_MAX 1000

const int MAX_PORT = 65535l;
const int MIN_PORT = 0;

const char HTTP_RESPONSE_200[] =
        "HTTP/1.1 200 OK\n"
        "Content-Type: text/plain\n\n";

const char HTTP_RESPONSE_404[] = "HTTP/1.1 404 Not Found\n\n";

int running = 1;
int slot = 0;

char *web_server_root_path;
int listening_socket;
int clients[CLIENTS_MAX];

void web_server_start(char *);

void web_server_respond(int);

void web_server_stop();

int decode_cmd(const char *s, char *dec);

int ishex(int x) {
    return (x >= '0' && x <= '9') ||
           (x >= 'a' && x <= 'f') ||
           (x >= 'A' && x <= 'F');
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port> \n", argv[0]);
        exit(1);
    }

    if (atoi(argv[1]) > MAX_PORT || (long) argv[1] < MIN_PORT) {
        printf("Port must be between %d - %d", MIN_PORT, MAX_PORT);
        exit(1);
    }

    struct sockaddr_in client_address;

    socklen_t address_length;

    web_server_root_path = getenv("PWD");

    int i;
    for (i = 0; i < CLIENTS_MAX; i++)
        clients[i] = -1;

    web_server_start(argv[1]);

    while (running) {
        address_length = sizeof(client_address);

        clients[slot] = accept(listening_socket, (struct sockaddr *) &client_address, &address_length);

        if (clients[slot] < 0) {
            perror("Error: accept()");
        } else {
            if (fork() == 0) {
                web_server_respond(slot);
                exit(0);
            }
        }

        while (clients[slot] != -1)
            slot = (slot + 1) % CLIENTS_MAX;
    }

    signal(SIGINT, web_server_stop);
    return 0;
}


void web_server_start(char *port) {

    struct addrinfo hints, *res, *p;

    memset (&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        perror("Error: $getaddrinfo()");
        exit(1);
    }

    for (p = res; p != NULL; p = p->ai_next) {
        listening_socket = socket(p->ai_family, p->ai_socktype, 0);
        if (listening_socket == -1) {
            continue;
        }

        if (bind(listening_socket, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
    }

    if (p == NULL) {
        perror("Error: socket() or bind().");
        exit(1);
    }

    freeaddrinfo(res);
    if (listen(listening_socket, 1000000) != 0) {
        perror("Error: listen().");
        exit(1);
    }
}

void web_server_respond(int n) {

    char *client_request[MAX_CHARS];

    char message[MAX_CHARS];

    char path[MAX_CHARS];

    int received;

    memset((void *) message, (int) '\0', MAX_CHARS);

    received = recv(clients[n], message, MAX_CHARS, 0);

    if (received == 0) {
        fprintf(stderr, "Error: Client Disconnected.\n");
    } else if (received < 0) {
        fprintf(stderr, ("Error: recv()\n"));
    } else {
        client_request[0] = strtok(message, " \t\n");

        if (strncmp(client_request[0], "GET\0", 4) == 0) {
            int j;
            char *hishcmd[MAX_CHARS];
            char http_version[MAX_CHARS];

            for (j = 1; j < MAX_CHARS; j++) {
                client_request[j] = strtok(NULL, " ");

                if (strncmp(client_request[j], "HTTP/1.1", 8) == 0) {
                    strcpy(http_version, "HTTP/1.1");
                    break;
                } else if (strncmp(client_request[j], "HTTP/1.0", 8) == 0) {
                    strcpy(http_version, "HTTP/1.0");
                    break;
                }
                hishcmd[j - 1] = client_request[j];
            }

            if (strncmp(http_version, "HTTP/1.0", 8) != 0 && strncmp(http_version, "HTTP/1.1", 8) != 0) {
                write(clients[n], HTTP_RESPONSE_404, strlen(HTTP_RESPONSE_404));
            } else {
                strcpy(path, web_server_root_path);

                char url[MAX_CHARS];
                puts(decode_cmd(hishcmd[0], url) < 0 ? "bad string" : url);

                if (strncmp(url, "/exec/", 6) == 0) {
                    char uri[MAX_CHARS];
                    for (int i = 0; i < j - 1; i++) {
                        strcat(uri, hishcmd[i]);
                        strcat(uri, " ");
                    }

                    char *cmd[MAX_CHARS];
                    cmd[0] = strtok(uri, "/");
                    cmd[1] = strtok(NULL, "/");

                    puts(decode_cmd(cmd[1], cmd[1]) < 0 ? "bad string" : cmd[1]);

                    if (strlen(cmd[1]) <= 1) {
                        write(clients[n], HTTP_RESPONSE_404, strlen(HTTP_RESPONSE_404));
                    } else {
                        send(clients[n], HTTP_RESPONSE_200, strlen(HTTP_RESPONSE_200), 0);

                        FILE *fp;
                        fp = popen(cmd[1], "r");
                        char content[MAX_CHARS];
                        while (fgets(content, MAX_CHARS, fp) != NULL) {
                            write(clients[n], content, strlen(content));
                        }
                    }
                } else {
                    write(clients[n], HTTP_RESPONSE_404, strlen(HTTP_RESPONSE_404));
                }
            }
        } else {
            write(clients[n], HTTP_RESPONSE_404, strlen(HTTP_RESPONSE_404));
        }
    }

    shutdown(clients[n], SHUT_RDWR);
    close(clients[n]);
    clients[n] = -1;
}

int decode_cmd(const char *s, char *dec) {
    char *o;
    const char *end = s + strlen(s);
    int c;

    for (o = dec; s <= end; o++) {
        c = *s++;
        if (c == '+') c = ' ';
        else if (c == '%' && (!ishex(*s++) ||
                              !ishex(*s++) ||
                              !sscanf(s - 2, "%2x", &c)))
            return -1;

        if (dec) *o = c;
    }
    return o - dec;
}

void web_server_stop() {
    shutdown(clients[slot], SHUT_RDWR);
    close(clients[slot]);
    running = 0;
    exit(0);
}
