#include <iostream>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <bits/stdc++.h>

typedef struct udpMessage {
	uint8_t type;
	int port;
	char ip[16];
	char topic[50];
	char message[1500];
} udpMessage;
// structura de mesaj primit de la server

using namespace std;

int recvAll(int socket, void *message, int len) {
    int bytesRecv = 0;
    char *udpMess = (char *)message;
    while(bytesRecv < len) {
        int rc = recv(socket, udpMess + bytesRecv, len - bytesRecv, 0);
        if(rc < 0) {
            return -1;
        }
        if(rc == 0) {
            return 0;
        }
        bytesRecv += rc;
    }
    return bytesRecv;
}
// functie prin care se primeste pachetul de la server pana se primesc toti octetii

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    if(argc != 4) {
        perror("Usage: ./subscriber <ID_client> <IP_server> <port_server>\n");
        return -1;
    }
    if(strlen(argv[1]) > 10) {
        perror("ID too long\n");
        return -1;
    }
    char id[10];
    memcpy(id, argv[1], 10);
    char ip[16];
    strcpy(ip, argv[2]);
    ip[strlen(ip)] = '\0';
    int port = atoi(argv[3]);
    if(port < 1024 || port > 65535) {
        perror("Invalid port\n");
        return -1;
    }
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    // creez socket-ul
    if(sockfd < 0) {
        perror("Error creating socket\n");
        return -1;
    }
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    int rc = inet_aton(ip, &serv_addr.sin_addr);
    if(rc == 0) {
        perror("Invalid IP\n");
        return -1;
    }
    rc = connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    // ma conectez la sever
    if(rc < 0) {
        perror("Error connecting to server\n");
        return -1;
    }

    int flag = 1;
    int nagle = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
    if (nagle < 0)
    {
        perror("Error disable Nagle algorithm\n");
        exit(1);
    }
    // dezactivez algoritmul lui Nagle

    fd_set read_fds, aux;
    FD_ZERO(&read_fds);
    FD_ZERO(&aux);
    FD_SET(sockfd, &read_fds);
    FD_SET(0, &read_fds);
    int fdmax = sockfd;
    // creez set-ul de file descriptori
    int idLen = strlen(id);
    rc = send(sockfd, &idLen, sizeof(int), 0);
    // trimit lungimea id-ului catre server
    if(rc < 0) {
        perror("Error sending ID length\n");
        return -1;
    }
    rc = send(sockfd, &id, idLen, 0);
    // trimit id-ul catre server
    if(rc < 0) {
        perror("Error sending ID\n");
        return -1;
    }
    // FILE *f = fopen("subscriber.log", "a");
    while(true) {
        aux = read_fds;
        rc = select(fdmax + 1, &aux, NULL, NULL, NULL);
        if(rc < 0) {
            perror("Error in select\n");
            return -1;
        }
        if(FD_ISSET(0, &aux)) {
            // intru pe cazul in care clientul da comenzi de la tastatura
            char buffer[1600];
            memset(buffer, 0, 1600);
            fgets(buffer, 1600, stdin);
            if(strncmp(buffer, "exit", 4) == 0) {
                // in cazul in care clientul da exit, trimit mesajul catre server
                char toSend[100];
                memset(toSend, 0, 100);
                strcat(toSend, "exit");
                toSend[strlen(toSend)] = '\0';
                int bytesToSend = strlen(toSend);
                int rc = send(sockfd, &bytesToSend, sizeof(int), 0);
                if(rc < 0) {
                    perror("Error sending message length\n");
                    return -1;
                }
                rc = send(sockfd, toSend, bytesToSend, 0);
                if(rc < 0) {
                    perror("Error sending exit\n");
                    return -1;
                }
                break;
            } else {
                if(strncmp(buffer, "subscribe", 9) == 0) {
                    char *topic = strtok(buffer, " ");
                    topic = strtok(NULL, " ");
                    topic[strlen(topic) - 1] = '\0';
                    // iau topic-ul
                    if(strlen(topic) == 0) {
                        perror("Subscribe needs a topic\n");
                        continue;
                    }
                    if(strlen(topic) > 51) {
                        perror("Topic too long\n");
                        continue;
                    }
                    char toSend[100];
                    memset(toSend, 0, 100);
                    strcat(toSend, "subscribe ");
                    strcat(toSend, topic);
                    toSend[strlen(toSend)] = '\0';
                    int bytesToSend = strlen(toSend);
                    // creez mesajul de subscribe si il trimit catre server
                    rc = send(sockfd, &bytesToSend, sizeof(int), 0);
                    if(rc < 0) {
                        perror("Error sending message length\n");
                        return -1;
                    }
                    // trimit mai intai lungimea mesajului
                    rc = send(sockfd, toSend, bytesToSend, 0);
                    if(rc < 0) {
                        perror("Error sending subscribe\n");
                        return -1;
                    }
                    printf("Subscribed to topic %s.\n", topic);
                } else {
                    if(strncmp(buffer, "unsubscribe", 11) == 0) {
                        char *topic = strtok(buffer, " ");
                        topic = strtok(NULL, " ");
                        topic[strlen(topic) - 1] = '\0';
                        if(strlen(topic) > 51) {
                            perror("Topic too long\n");
                            continue;
                        }
                        if(strlen(topic) == 0) {
                            perror("Unsubscribe needs a topic\n");
                            continue;
                        }
                        char toSend[100];
                        memset(toSend, 0, 100);
                        strcat(toSend, "unsubscribe ");
                        strcat(toSend, topic);
                        toSend[strlen(toSend)] = '\0';
                        // creez mesajul de unsubscribe si il trimit catre server
                        int bytesToSend = strlen(toSend);
                        rc = send(sockfd, &bytesToSend, sizeof(int), 0);
                        // trimit mai intai lungimea mesajului
                        if(rc < 0) {
                            perror("Error sending message length\n");
                            return -1;
                        }
                        rc = send(sockfd, toSend, bytesToSend, 0);
                        if(rc < 0) {
                            perror("Error sending unsubscribe\n");
                            return -1;
                        }
                        printf("Unsubscribed from topic %s.\n", topic);
                    } else {
                        perror("Invalid command\n");
                    }
                }
            }
        }

        if(FD_ISSET(sockfd, &aux)) {
            // intru pe cazul in care primesc mesaje de la server
            udpMessage msg;
            int lenReceived;
            rc = recvAll(sockfd, &lenReceived, sizeof(int));
            // primesc mai intai lungimea pachetului
            rc = recvAll(sockfd, &msg, lenReceived);
            // primesc mesajul
            if(rc < 0) {
                perror("Error receiving message\n");
                return -1;
            }
            if(rc == 0) {
                break;
            }
            if(msg.type == 0) {
                uint32_t num;
                num = ntohl(*(uint32_t *)(msg.message + 1));
                if(msg.message[0] == 1 && num != 0) {
                    printf("%s:%d - %s - INT - -%u\n", msg.ip, msg.port, msg.topic, num);
                } else {
                    printf("%s:%d - %s - INT - %u\n", msg.ip, msg.port, msg.topic, num);
                    // in cazul in care numarul este 0 si are bitul de semn 1, il vom afisa fara -
                }
            } else {
                if(msg.type == 1) {
                    uint16_t num = htons(*(uint16_t *)msg.message);
                    float res = (float)num / 100;
                    printf("%s:%d - %s - SHORT_REAL - %.2f\n", msg.ip, msg.port, msg.topic, res);
                } else {
                    if(msg.type == 2) {
                        uint32_t num = 0;
                        memcpy(&num, msg.message + 1, sizeof(uint32_t));
                        num = ntohl(num);
                        uint8_t power;
                        memcpy(&power, msg.message + 5, sizeof(uint8_t));
                        double res = (double)num / pow(10, power);
                        if(msg.message[0] == 1) {
                            res = -res;
                        }
                        printf("%s:%d - %s - FLOAT - %.4f\n", msg.ip, msg.port, msg.topic, res);
                    } else {
                        if(msg.type == 3) {
                            msg.message[1500] = '\0';
                            printf("%s:%d - %s - STRING - %s\n", msg.ip, msg.port, msg.topic, msg.message);
                        } else {
                            perror("Invalid message type\n");
                        }
                    }
                }
            }
        }
    }
}