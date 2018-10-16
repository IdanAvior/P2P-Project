/*
 * p2pserver.c
 *
 *  Created on: Jun 10, 2018
 *      Author: Idan Avior
 */

/*
 * Server program for P2P communication
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/shm.h>
#include "p2p.h"

#define TRUE 1
#define FALSE 0
#define QUEUE_LEN 5
#define ADDRESS "127.0.0.1"

void sig_handler(int sig);
void deleteShm(int shmid_port, int shmid_numEntries, int shmid_fileEntries);

int sockfd;
int shmid_port;
int shmid_numEntries;
int shmid_fileEntries;
const int buf_size = P_BUFF_SIZE;
const int max_files = MAX_FILES;
int main(int argc, char* argv[]){
	if ((shmid_port = shmget(SHMKEY_PORT, sizeof(int), 0644 | IPC_CREAT | IPC_EXCL)) == -1){
		perror("shmget 1");
		exit(EXIT_FAILURE);
	}
	shmPort *shm_port = (shmPort*)shmat(shmid_port, NULL, 0);
	shm_port->num = P_SRV_PORT + 1;
	if ((shmid_numEntries = shmget(SHMKEY_NUM_ENT, sizeof(int), 0644 | IPC_CREAT | IPC_EXCL)) == -1){
		perror("shmget 2");
		exit(EXIT_FAILURE);
	}
	shmNumEntries *shm_num_ent = (shmNumEntries*)shmat(shmid_numEntries, NULL, 0);
	shm_num_ent->num = 0;
	if ((shmid_fileEntries = shmget(SHMKEY_FILE_ENT, sizeof(file_ent_t)*MAX_FILES, 0644 | IPC_CREAT | IPC_EXCL)) == -1){
		perror("shmget 3");
		exit(EXIT_FAILURE);
	}
	in_port_t last_allocated_port_num = P_SRV_PORT;
	// Define signal response behavior
	struct sigaction action;
	action.sa_handler = sig_handler;
	sigemptyset(&action.sa_mask);
	sigaddset(&action.sa_mask, SIGQUIT);
	sigaddset(&action.sa_mask, SIGKILL);
	sigaddset(&action.sa_mask, SIGTERM);
	if (sigaction(SIGQUIT, &action, NULL) == -1){
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	// Initialize socket
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		perror("socket");
		exit(EXIT_FAILURE);
	}
	// Bind to address
	struct sockaddr_in myaddr;
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(P_SRV_PORT);
	myaddr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sockfd, (struct sockaddr*) &myaddr, sizeof(myaddr)) == -1){
		perror("bind");
		deleteShm(shmid_port, shmid_numEntries, shmid_fileEntries);
		exit(EXIT_FAILURE);
	}
	char addrstr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(myaddr.sin_addr), addrstr, INET_ADDRSTRLEN);
	printf("Server - server: opening socket on %s:%hu\n", addrstr, P_SRV_PORT);
	// Listen
	if (listen(sockfd, QUEUE_LEN) == -1){
		perror("listen");
		exit(EXIT_FAILURE);
	}

	// Server ready
	while(TRUE){
		// Accept connection
		int fd, len = 1;
		struct sockaddr addr;
		if ((fd = accept(sockfd,(struct sockaddr*)&addr, &len)) == -1){
			perror("accept (server)");
			exit(EXIT_FAILURE);
		}
		char addrstr2[INET_ADDRSTRLEN];
		struct sockaddr_in* in_addr = (struct sockaddr_in*)&addr;
		inet_ntop(AF_INET, &(in_addr->sin_addr.s_addr), addrstr2, INET_ADDRSTRLEN);
		printf("Server - accept: accepted connection from %s:%hu\n", addrstr2, in_addr->sin_port);
		pid_t pid = fork();
		if (pid == 0){
			char buf[buf_size];
			int num_bytes;
			// Read message - first time only to get msg_type
			printf("Server - recv: waiting for message\n");
			if ((num_bytes = recv(fd, buf, buf_size, MSG_PEEK)) == -1){
				perror("recv");
			}
			printf("Server - recv: message received\n");
			int msg_type;
			sscanf(buf, "%d", &msg_type);
			memset(buf,0,sizeof(buf));
			// Reread message
			if ((num_bytes = recv(fd, buf, buf_size, 0)) == -1){
				perror("recv");
			}
			if (msg_type == MSG_SHUTDOWN){
				printf("Server - shutdown: shutdown message received\n");
				kill(getppid(),SIGQUIT);
			}
			else if (msg_type == MSG_NOTIFY){
				printf("Server - notify: receiving MSG_NOTIFY\n");
				msg_notify_t msg_ntf;
				file_ent_t fent;
				sscanf(buf, "%d %ui %hu %s", &msg_ntf.m_type, &msg_ntf.m_addr, &msg_ntf.m_port, msg_ntf.m_name);
				strcpy(fent.fe_name, msg_ntf.m_name);
				fent.fe_addr = msg_ntf.m_addr;
				if (msg_ntf.m_port == 0){
					// Allocate new port number
					if ((shmid_port = shmget(SHMKEY_PORT, sizeof(int), 0644)) == -1){
						perror("shmid");
						exit(EXIT_FAILURE);
					}
					shmPort *shm = (shmPort*)shmat(shmid_port, NULL, 0);
					last_allocated_port_num = shm->num++;
					fent.fe_port = last_allocated_port_num;
					if (shmdt(shm) == -1){
						perror("shmdt");
						exit(EXIT_FAILURE);
					}
					printf("Server - notify: assigned port %hu\n", last_allocated_port_num);
				}
				else{
					fent.fe_port = msg_ntf.m_port;
				}
				if ((shmid_numEntries = shmget(SHMKEY_NUM_ENT, sizeof(int), 0644)) == -1){
					perror("shmget");
					exit(EXIT_FAILURE);
				}
				shmNumEntries *shm_num_ent = (shmNumEntries*)shmat(shmid_numEntries, NULL, 0);
				if ((shmid_fileEntries = shmget(SHMKEY_FILE_ENT, sizeof(file_ent_t)*MAX_FILES, 0644)) == -1){
					perror("shmget");
					exit(EXIT_FAILURE);
				}
				shmFileEntries* shm_file_ent = (shmFileEntries*)shmat(shmid_fileEntries, NULL, 0);
				shm_file_ent->entries[shm_num_ent->num++] = fent;
				// Prepare ACK message
				msg_ack_t ack;
				ack.m_type = MSG_ACK;
				struct sockaddr *sa = &addr;
				struct sockaddr_in *sin = (struct sockaddr_in*) sa;
				ack.m_port = fent.fe_port;
				char msg_buf[P_BUFF_SIZE];
				sprintf(msg_buf, "%d %d", ack.m_type, ack.m_port);
				// Send ACK message
				printf("Server - notify: sending MSG_ACK\n");
				if (send(fd, msg_buf, strlen(msg_buf)+1, 0) == -1){
					perror("send");
				}
				printf("Server - notify: MSG_ACK sent\n");
			}
			else if (msg_type == MSG_DIRREQ){
				printf("Server - dirreq: receiving MSG_DIRREQ\n");
				// Get shared memory
				if ((shmid_numEntries = shmget(SHMKEY_NUM_ENT, sizeof(int), 0644)) == -1){
					perror("shmget");
					exit(EXIT_FAILURE);
				}
				shmNumEntries *shm_num_ent = (shmNumEntries*)shmat(shmid_numEntries, NULL, 0);
				if ((shmid_fileEntries = shmget(SHMKEY_FILE_ENT, sizeof(file_ent_t)*MAX_FILES, 0644)) == -1){
					perror("shmget");
					exit(EXIT_FAILURE);
				}
				shmFileEntries* shm_file_ent = (shmFileEntries*)shmat(shmid_fileEntries, NULL, 0);
				// Prepare DIRHDR message
				msg_dirhdr_t msg;
				msg.m_type = MSG_DIRHDR;
				msg.m_count = shm_num_ent->num;
				char msg_buf[P_BUFF_SIZE];
				sprintf(msg_buf, "%d %d", msg.m_type, msg.m_count);
				// Send DIRHDR message
				printf("Server - dirreq: sending MSG_DIRHDR with %d items\n", msg.m_count);
				if (send(fd, msg_buf, strlen(msg_buf)+1, 0) == -1){
					perror("send");
				}
				// Wait for ACK
				printf("Server - dirreq: waiting for ACK\n");
				if (recv(fd, msg_buf, P_BUFF_SIZE, 0) == -1){
					perror("recv");
					exit(EXIT_FAILURE);
				}
				printf("Server - dirreq: ACK received\n");
				int i;
				// Prepare and send DIRENT messages
				for (i = 0; i < shm_num_ent->num; i++){
					msg_dirent_t dirent;
					dirent.m_type = MSG_DIRENT;
					dirent.m_addr = shm_file_ent->entries[i].fe_addr;
					dirent.m_port = shm_file_ent->entries[i].fe_port;
					strcpy(dirent.m_name, shm_file_ent->entries[i].fe_name);
					memset(msg_buf,0,sizeof(msg_buf));
					sprintf(msg_buf, "%d %ui %hu %s", dirent.m_type,
							dirent.m_addr, dirent.m_port, dirent.m_name);
					printf("Server - dirent: sending MSG_DIRENT for \"%s\"\n", dirent.m_name);
					if (send(fd, msg_buf, strlen(msg_buf)+1, 0) == -1){
						perror("send");
						exit(EXIT_FAILURE);
					}
					printf("Server - dirent: sent MSG_DIRENT for \"%s\"\n", dirent.m_name);
					// Wait for ACK
					printf("Server - dirent: waiting for ACK from client\n");
					if (recv(fd, msg_buf, P_BUFF_SIZE, 0) == -1){
						perror("recv");
						exit(EXIT_FAILURE);
					}
					printf("Server - dirent: received ACK from client\n");
				}
				if ((shmdt(shm_num_ent) == -1) || shmdt(shm_file_ent) == -1){
					perror("shmdt");
					exit(EXIT_FAILURE);
				}
			}
			else {
				//Invalid message
				printf("Server: invalid message received: %s\n", buf);
			}
			// Close socket
			if (close(fd) == -1){
				perror("close");
			}
			exit(EXIT_SUCCESS);
		}
	}
	// Close socket
	if (close(sockfd) == -1){
		perror("close");
	}
}

void deleteShm(int shmid_port, int shmid_numEntries, int shmid_fileEntries) {
	if (shmctl(shmid_port, IPC_RMID, 0) == -1) {
		perror("shmctl");
		exit(EXIT_FAILURE);
	}
	if (shmctl(shmid_numEntries, IPC_RMID, 0) == -1) {
		perror("shmctl");
		exit(EXIT_FAILURE);
	}
	if (shmctl(shmid_fileEntries, IPC_RMID, 0) == -1) {
		perror("shmctl");
		exit(EXIT_FAILURE);
	}
}

void sig_handler(int sig){
	if (sig == SIGQUIT){
		if (close(sockfd) == -1){
			perror("close");
		}
		deleteShm(shmid_port, shmid_numEntries, shmid_fileEntries);
		printf("Server - shutdown: shutting down\n");
		exit(EXIT_SUCCESS);
	}
	else if ((sig == SIGKILL) || (sig == SIGTERM)){
		if (close(sockfd) == -1){
			perror("close");
		}
		deleteShm(shmid_port, shmid_numEntries, shmid_fileEntries);
		printf("Server - shutdown: shutting down\n");
		exit(EXIT_FAILURE);
	}
}


