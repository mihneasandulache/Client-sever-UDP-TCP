#include <arpa/inet.h>
#include <bits/stdc++.h>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <string>

using namespace std;

typedef struct udpMessage {
	uint8_t type;
	int port;
	char ip[16];
	char topic[50];
	char message[1500];
} udpMessage;
/* structura pentru mesaj primit de la clientul UDP
si trimis catre clientul TCP */

typedef struct tcpClient {
	char id[10];
	int socket;
	int subscribedTopicsCount = 0;
	char subscribedTopics[100000][51] ;
	int isSubscribed[100000];
} tcpClient;
// structura pentru client TCP

struct sockaddr_storage newTcpClient;

int findTCPClient(char id[10], vector<tcpClient> &clients)
{
	int newClientIndex = -1;
	for (int i = 0; i < clients.size(); i++) {
		if (strcmp(clients[i].id, id) == 0) {
			newClientIndex = i;
			break;
		}
	}
	return newClientIndex;
}
// functie care intoarce index-ul unui client sau -1

int sendAll(int socket, void *buffer, int len) {
	char *udpMess = (char *)buffer;
	int bytesSent = 0;
	while (bytesSent < len) {
		int rc = send(socket, udpMess, len - bytesSent, 0);
		if(rc < 0) {
			return -1;
		} else {
			if(rc == 0) {
				return 0;
			}
		}
		bytesSent += rc;
	}
	return bytesSent;
}
/* functie care trimite un pachet catre un client TCP asigurandu-se
ca au fost toti octetii trimisi */

udpMessage buildUDPMessage(udpMessage &udpMess, char *buffer, socklen_t len, struct sockaddr_in serverAddress)
{
	memcpy(udpMess.topic, buffer, 50);
	udpMess.topic[strlen(udpMess.topic)] = '\0';
	udpMess.type = buffer[50];
	memcpy(udpMess.message, &buffer[51], 1500);
	udpMess.message[strlen(udpMess.message)] = '\0';
	udpMess.port = ntohs(serverAddress.sin_port);
	strcpy(udpMess.ip, inet_ntoa(serverAddress.sin_addr));
	udpMess.ip[strlen(udpMess.ip)] = '\0';
	return udpMess;
}
// functie care construieste un pachet ce va fi trimis catre clientul TCP

void closeAll(int tcpSocket, int udpSocket, fd_set *readFds, int maxFd)
{
	int i;
	for (i = 3; i <= maxFd; i++) {
		if (FD_ISSET(i, readFds)) {
			close(i);
		}
	}
}
// functie care inchide toate socket-urile

bool isWildcard(char *topic) {
    int len = strlen(topic);
    for(int i = 0; i < len; i++) {
        if(topic[i] == '*' || topic[i] == '+') {
            return true;
        }
    }
    return false;
}

int matchWildcardTopic(char *subscription, char *topic) {
    if (strcmp(subscription, "*") == 0) {
        return 1;
    }
    char aux[100];
    memset(aux, 0, 100);
    strcpy(aux, subscription);
    char *tokens[100];
    int numTokens = 0;
    char *token = strtok(aux, "/");
    while (token != NULL) {
        tokens[numTokens++] = token;
        token = strtok(NULL, "/");
    }

    char aux2[100];
    memset(aux2, 0, 100);
    strcpy(aux2, topic);
    char *tokens2[100];
    int numTokens2 = 0;
    char *token2 = strtok(aux2, "/");
    while (token2 != NULL) {
        tokens2[numTokens2++] = token2;
        token2 = strtok(NULL, "/");
    }

    int i = 0;
    int j = 0;
    char *nextAfterStar = NULL;
    while (i < numTokens && j < numTokens2) {
        if (strcmp(tokens[i], tokens2[j]) == 0 || strcmp(tokens[i], "+") == 0) {
            i++;
            j++;
        } else {
            if (strcmp(tokens[i], "*") == 0) {
                if (i + 1 == numTokens) {
                    return 1;
                }
                nextAfterStar = tokens[i + 1];
                i++;
            } else if (nextAfterStar != NULL && strcmp(tokens[i], "+") != 0) {
                while (j < numTokens2 && strcmp(tokens2[j], nextAfterStar) != 0) {
                    j++;
                }
                if (j == numTokens2) {
                    return 0;
                } else {
                    i++;
                    j++;
                }
            } else {
                return 0;
            }
        }
    }
    if (i == numTokens && j == numTokens2) {
        return 1;
    }
    return 0;
}
/* functie care face match intre un topic trimis de clientul UDP
si un topic wildcard la care s-a abonat un client TCP, dar si un topic normal */

int step = 0;

int main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IONBF, BUFSIZ);
	// dezactivam retinerea mesajelor intr-un buffer
	if (argc != 2) {
		perror("Usage: ./server <port>");
		exit(1);
	}

	int port = atoi(argv[1]);
	if (port < 1024 || port > 65535) {
		perror("Wrong port number");
		exit(1);
	}

	int tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (tcpSocket < 0) {
		perror("Error creating socket");
		exit(1);
	}
	// setam socket-ul de TCP pentru listen
	int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (udpSocket < 0) {
		perror("Error creating socket");
		exit(1);
	}
	// setam socket-ul de UDP pentru listen
    int flag = 1;
	int rc = setsockopt(tcpSocket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
	if (rc < 0) {
		perror("Error disabling Nagle's algorithm");
		exit(1);
	}
	// dezactivam algoritmul lui Nagle
	vector<tcpClient> clients;
	// vector de clienti TCP
	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(port);
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	inet_pton(AF_INET, "0.0.0.0", &serverAddress.sin_addr);
	socklen_t len = sizeof(serverAddress);

	if (bind(tcpSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
		perror("Error binding socket");
		exit(1);
	}
	if (bind(udpSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
		perror("Error binding socket");
		exit(1);
	}
	// facem bind pe cei doi socketi de listen

	if (listen(tcpSocket, 32) < 0) {
		perror("Error listening");
		exit(1);
	}

	fd_set readFds, aux;
	int maxFd = max(tcpSocket, udpSocket);
	FD_ZERO(&readFds);
	FD_SET(tcpSocket, &readFds);
	FD_SET(udpSocket, &readFds);
	FD_SET(0, &readFds);
	// initializam set-ul cu file descriptori
	FILE *f = fopen("server.log", "a");
	while (true) {
		FD_ZERO(&aux);
		aux = readFds;
		if (select(maxFd + 1, &aux, NULL, NULL, NULL) < 0) {
			perror("Error selecting");
			exit(1);
		}
		int fd;
		for (fd = 0; fd <= maxFd; fd++) {
			char *buffer = (char *)malloc(1600);
			if (FD_ISSET(fd, &aux)) {
				if (fd == 0) {
					memset(buffer, 0, 1600);
					fgets(buffer, 1600, stdin);
					if (strncmp(buffer, "exit", 4) == 0) {
						closeAll(tcpSocket, udpSocket, &readFds, maxFd);
						exit(0);
					}
					// intram pe cazul de STDIN unde putem primi doar comanda de exit
				} else {
					if (fd == udpSocket) {
						int bytesReceived =
							recvfrom(udpSocket, buffer, 1600, 0, (struct sockaddr *)&serverAddress, &len);
							// primesc ce trimite clientul UDP
						if(bytesReceived == 1 || bytesReceived == 0) {
							fprintf(f,"Received 0 bytes\n");
							continue;
						}
						// daca se trimite un mesaj gol continui
						if (bytesReceived < 0) {
							perror("No message received!");
							exit(EXIT_FAILURE);
						}
						udpMessage udpMess;
						udpMess = buildUDPMessage(udpMess, buffer, len, serverAddress);
						int toSend = sizeof(udpMess);
						// construiesc mesajul
						for(auto &c : clients) {
							if(c.socket != -1) {
								for(int i = 0; i < c.subscribedTopicsCount; i++) {
									if(matchWildcardTopic(c.subscribedTopics[i], udpMess.topic) == 1 && c.isSubscribed[i]) {
										int rc = sendAll(c.socket, &toSend, sizeof(int));
										if(rc < 0) {
											perror("Error sending size of message\n");
											exit(1);
										}
										rc = sendAll(c.socket, &udpMess, toSend);
										// il trimit tuturor clientilor conectati si abonati la topic sau care fac match
										if(rc < 0) {
											perror("Error sending message\n");
											exit(1);
										}
										break;
										/* ies din iteratie pentru a evita cazul in care in urma abonarii la un topic cu
										wildcar voi fi abonat de doua ori la acelasi topic si astfel trimit un singur mesaj */
									} 
								}
							}
						}
					} else {
						if (fd == tcpSocket) {
							socklen_t tcpClientLen = sizeof(newTcpClient);
							int newTcpSocket = accept(tcpSocket, (struct sockaddr *)&newTcpClient, &tcpClientLen);
							// accept o conexiune cu un nou client TCP
							if (newTcpSocket < 0) {
								perror("Error accepting connection");
								exit(1);
							}
							int flag = 1;
							if (setsockopt(newTcpSocket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)) < 0) {
								perror("Error disabling Nagle's algorithm");
								exit(1);
							}
							char buffer[1600];
							memset(buffer, 0, 1600);
							int idLen;
							recv(newTcpSocket, &idLen, sizeof(int), 0);
							// primesc lungimea id-ului
							recv(newTcpSocket, buffer, idLen, 0);
							char id[10];
							memcpy(id, buffer, 10);
							id[strlen(id)] = '\0';
							// primesc id-ul acestuia
							int newClientIndex = findTCPClient(id, clients);
							if (newClientIndex != -1) {
								if (clients[newClientIndex].socket != -1) {
									close(newTcpSocket);
									printf("Client %s already connected.\n", clients[newClientIndex].id);
									// verific daca este vreun client conectat cu acelasi id
								} else {
									clients[newClientIndex].socket = newTcpSocket;
									printf("New client %s connected from %s:%d.\n", clients[newClientIndex].id,
										   inet_ntoa(((struct sockaddr_in *)&newTcpClient)->sin_addr),
										   ntohs(((struct sockaddr_in *)&newTcpClient)->sin_port));
									FD_SET(newTcpSocket, &readFds);
									if (newTcpSocket > maxFd) {
										maxFd = newTcpSocket;
									}
									// daca este un vechi client care s-a conectat din nou, ii actualizez socket-ul
								}
							} else {
								if (newClientIndex == -1) {
									tcpClient newClient;
									memcpy(newClient.id, id, 10);
									newClient.id[strlen(newClient.id)] = '\0';
									newClient.socket = newTcpSocket;
									clients.push_back(newClient);
									FD_SET(newTcpSocket, &readFds);
									if (newTcpSocket > maxFd) {
										maxFd = newTcpSocket + 1;
									}
									printf("New client %s connected from %s:%d.\n", id,
										   inet_ntoa(((struct sockaddr_in *)&newTcpClient)->sin_addr),
										   ntohs(((struct sockaddr_in *)&newTcpClient)->sin_port));
									// daca este un client nou il adaug la vectorul de clienti
								}
							}
						} else {
							// intru pe cazul in care primesc mesaje de la clienti TCP deja conectat
							memset(buffer, 0, 1600);
							int len;
							int rc = recv(fd, &len, sizeof(int), 0);
							if (rc < 0) {
								perror("Error receiving message size");
								exit(1);
							}
                            rc = recv(fd, buffer, len, 0);
                            if (rc < 0) {
                                perror("Error receiving message");
                                exit(1);
                            }
                            if(rc == 0 || strncmp(buffer, "exit", 4) == 0) {
                                int i;
                                for(i = 0; i < clients.size(); i++) {
                                    if(clients[i].socket == fd) {
                                        printf("Client %s disconnected.\n", clients[i].id);
                                        clients[i].socket = -1;
                                        close(fd);
                                        FD_CLR(fd, &readFds);
                                        break;
                                    }
                                }
                            } else {
                                if(strncmp(buffer, "subscribe", 9) == 0) {
									// fprintf(f, "intrat pe subsbcribe de pe fd: %d", fd);
									char *aux = strtok(buffer, " ");
									aux = strtok(NULL, " ");
									aux[strlen(aux)] = '\0';
									// iau topic-ul
                                    for(auto &client : clients) {
										//fprintf(f, "Numarul de abonari: %d ale lui %s\n", client.subscribedTopicsCount, client.id);
                                        int alreadySubscribed = 0;
                                        if(client.socket == fd) {
											for(int i = 0; i < client.subscribedTopicsCount; i++) {
												if(strcmp(client.subscribedTopics[i], aux) == 0 && client.isSubscribed[i]) {
													alreadySubscribed = 1;
													perror("Already subscribed to this topic!\n");
													// verific daca este deja abonat la el
													break;
												} else {
													if(strcmp(client.subscribedTopics[i], aux) == 0 && !client.isSubscribed[i]) {
														client.isSubscribed[i] = 1;
														alreadySubscribed = 1;
														break;
														// daca a mai fost abonat dar s-a dezabonat, doar marchez topic-ul ca fiind un abonament
													}
												}
											}
											if(alreadySubscribed) {
												break;
											}
											strcpy(client.subscribedTopics[client.subscribedTopicsCount], aux);
											client.isSubscribed[client.subscribedTopicsCount] = 1;
											client.subscribedTopicsCount++;
											client.subscribedTopics[client.subscribedTopicsCount - 1][strlen(client.subscribedTopics[client.subscribedTopicsCount - 1])] = '\0';
											// daca e un abonament nou, il adaug in vectorul de topic-uri la care e clientul abonat
											break;
                                        }
                                    }
                                } else {
                                    if(strncmp(buffer, "unsubscribe", 11) == 0) {
										char *aux = strtok(buffer, " ");
										aux = strtok(NULL, " ");
                                        int found = 0;
                                        for(auto &client : clients) {
                                            if(client.socket == fd) {
												for(int i = 0; i < client.subscribedTopicsCount; i++) {
													if(strcmp(client.subscribedTopics[i], aux) == 0 && client.isSubscribed[i] == 1) {
														found = 1;
														client.isSubscribed[i] = 0;
														// daca se doreste dezabonarea, doar marchez deazbonarea, nu sterg topic-ul
														break;
													}
												}
												break;
                                            }
                                        }
										if(!found) {
											perror("Not subscribed to this topic!\n");
											// daca nu a fost gasit topic-ul, inseamna ca nu este abonat si nu se poate dezabona
										}
									} else {
										perror("Invalid command\n");
                                    }
                                }
                            }
                        }
					}
				}
			}
		}
	}
	close(tcpSocket);
	close(udpSocket);
}