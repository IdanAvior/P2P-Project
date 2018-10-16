/*
 * p2pclient.c
 *
 *  Created on: Jun 10, 2018
 *      Author: Idan Avior
 */

/*
 * Client program for P2P communication
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "p2p.h"

#define ADDRESS "127.0.0.1"
#define TRUE 1
#define FALSE 0
#define MAX_FILE_LEN 256
#define MAX_FILENAME_LEN 20

typedef int bool;

const int buf_size = P_BUFF_SIZE;

bool isShutdown(int argc, char** argv);

bool isSeed(int argc, char** argv);

bool isLeech(int argc, char** argv);

void resetBuf(char *buf);

void sendNotifyMsg(int i, const struct sockaddr_in* addr,
		in_port_t adv_port_num, char buf[buf_size], int sockfd, char* argv[]);

void rcvACK(char buf[buf_size], int sockfd, in_port_t *adv_port_num);

void reconnectToServer(int* sockfd, const struct sockaddr_in* addr);

void errorExit(const char *msg);

msg_dirhdr_t getDirHdr(char buf[buf_size], int sockfd);

void getFileEntries(int* i, msg_dirhdr_t dirhdr, char buf[buf_size], int sockfd,
		file_ent_t avail_files[dirhdr.m_count]);

void connectToPeer(const file_ent_t* ent, int* sockfd);

void sendFileSrv(const msg_filereq_t* req, char buf[buf_size], int fd,
		int* file_desc);

void sendFile(char buf[buf_size], int file_desc, const msg_filereq_t* req,
		int fd);

void startServer(in_port_t adv_port_num, int* sockfd);

void sendNegativeFileSrv(char buf[buf_size], int fd);

int main(int argc, char* argv[]){
	char buf[buf_size];
	char *shared_files[MAX_FILES];
	int i;
	for (i = 0; i < MAX_FILES; i++){
		if ((shared_files[i] = malloc(MAX_FILENAME_LEN)) == NULL)
			errorExit("malloc");
	}
	int num_files = 0;
	in_port_t adv_port_num = 0;	// Advertised port number
	// Initialize socket
	int sockfd;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		errorExit("socket");
	}
	// Connect to server
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(P_SRV_PORT);
	inet_pton(AF_INET, ADDRESS, &addr.sin_addr);
	if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == -1){
		errorExit("connect");
	}
	// Shutdown message received
	if (isShutdown(argc, argv)){
		// Get all file entries from server
		msg_dirhdr_t dirhdr = getDirHdr(buf, sockfd);
		file_ent_t entries[dirhdr.m_count];
		getFileEntries(&i, dirhdr, buf, sockfd, entries);
		reconnectToServer(&sockfd, &addr);

		// Send shutdown message
		msg_shutdown_t sd;
		sd.m_type = MSG_SHUTDOWN;
		resetBuf(buf);
		sprintf(buf, "%d", sd.m_type);
		printf("Client - shutdown: sending MSG_SHUTDOWN to server\n");
		if (send(sockfd,buf,buf_size,0) == -1){
			perror("send");
		}
		if (close(sockfd) == -1){
			perror("close");
		}
		for (i = 0; i < dirhdr.m_count; i++){
			if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
				errorExit("socket");
			}
			// Attempt to connect to peer
			struct sockaddr_in peer_addr;
			peer_addr.sin_family = AF_INET;
			peer_addr.sin_addr.s_addr = entries[i].fe_addr;
			peer_addr.sin_port = entries[i].fe_port;
			if (connect(sockfd, (struct sockaddr*)(&peer_addr),
					sizeof(peer_addr)) != -1){
				char addrstr[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &(peer_addr.sin_addr.s_addr),
						addrstr, INET_ADDRSTRLEN);
				printf("Client - shutdown: sending shutdown message to %s:%hu\n",
						addrstr, peer_addr.sin_port);
				if (send(sockfd,buf,buf_size,0) == -1){
					perror("send");
				}
			}
			else {
				perror("connect");
			}
			if (close(sockfd) == -1){
				perror("close");
			}
		}
		for (i = 0; i < MAX_FILES; i++){
			free(shared_files[i]);
		}
		printf("Client - shutdown: shutting down\n");
		exit(EXIT_SUCCESS);
	}
	else if (isSeed(argc, argv)){
		// Seed command received
		if (argc > 2){
			// Legal command
			// Send first notify message with port no. 0
			sendNotifyMsg(2, &addr, adv_port_num, buf, sockfd, argv);
			strcpy(shared_files[num_files++], argv[2]);
			// Wait for ACK from server
			rcvACK(buf, sockfd, &adv_port_num);
			int i;
			for (i = 3; i < argc; i++){
				reconnectToServer(&sockfd, &addr);
				strcpy(shared_files[num_files++], argv[i]);
				// Prepare notify message
				sendNotifyMsg(i, &addr, adv_port_num, buf, sockfd, argv);
				rcvACK(buf, sockfd, &adv_port_num);
			}
		}
	}
	else if (isLeech(argc, argv)){
		// Requested files array
		int num_req_files = argc - 2;
		char req_files[num_req_files][MAX_FILE_LEN];
		int i;
		for (i = 0; i < num_req_files; i++){
			strcpy(req_files[i], argv[i+2]);
		}
		// Get number of available files from server
		msg_dirhdr_t dirhdr = getDirHdr(buf, sockfd);
		// Available files array
		file_ent_t avail_files[dirhdr.m_count];
		// Get all file entries
		getFileEntries(&i, dirhdr, buf, sockfd, avail_files);
		// Attempt to acquire requested files
		for (i = 0; i < num_req_files; i++){
			int j;
			for (j = 0; j < dirhdr.m_count; j++){
				if (strcmp(req_files[i], avail_files[j].fe_name) == 0){
					// Connect to peer
					file_ent_t ent = avail_files[j];
					connectToPeer(&ent, &sockfd);
					// Request file
					msg_filereq_t req;
					req.m_type = MSG_FILEREQ;
					strcpy(req.m_name, req_files[i]);
					resetBuf(buf);
					sprintf(buf, "%d %s", req.m_type, req.m_name);
					if (send(sockfd, buf, strlen(buf)+1, 0) == -1){
						errorExit("send to peer");
					}
					resetBuf(buf);
					if (recv(sockfd, buf, P_BUFF_SIZE, 0) == -1){
						errorExit("recv from peer");
					}
					msg_filesrv_t fsrv;
					sscanf(buf, "%d %d", &fsrv.m_type, &fsrv.m_file_size);
					// Peer is ready to share file
					if (fsrv.m_file_size >= 0){
						printf("Peer - filereq: request granted\n");
						resetBuf(buf);
						//Send ACK
						printf("Peer- file_request: sending ACK to peer\n");
						resetBuf(buf);
						msg_ack_t ack;
						ack.m_type = MSG_ACK;
						sprintf(buf, "%d", ack.m_type);
						if (send(sockfd, buf, strlen(buf)+1, 0) == -1){
							errorExit("send");
						}
						if (recv(sockfd, buf, P_BUFF_SIZE, 0) == -1){
							errorExit("recv");
						}
						// Create and write to new file
						int file_fd;
						if ((file_fd = open(req_files[i], O_WRONLY | O_CREAT, 0644)) == -1){
							errorExit("open");
						}
						if (write(file_fd, buf, strlen(buf)) == -1){
							errorExit("write");
						}
						if (close(file_fd) == -1){
							perror("close");
						}
						strcpy(shared_files[num_files++], req_files[i]);
						printf("Peer - filereq: file \"%s\" has been downloaded\n",
								req_files[i]);
						break;
					}
					else{
						// Peer unwilling to share file
						printf("Peer - filereq: request refused\n");
						continue;
					}
				}
			}
		}
		// Done acquiring files, notify server
		for (i = 0; i < num_files; i++){
			reconnectToServer(&sockfd, &addr);
			sendNotifyMsg(i, &addr, adv_port_num, buf, sockfd, shared_files);
			rcvACK(buf, sockfd, &adv_port_num);
		}

	}
	else {
		// Illegal command
		fprintf(stderr,"Error: not enough arguments\n");
	}

	// Finished communicating with server - moving on to peer phase

	startServer(adv_port_num, &sockfd);
	while (TRUE){
		int fd, len = 1;
		struct sockaddr peer_addr;
		printf("Peer - filesrv: waiting for request\n");
		if ((fd = accept(sockfd, &peer_addr, &len)) == -1){
			errorExit("accept (peer)");
		}
		char addstr[INET_ADDRSTRLEN];
		struct sockaddr_in* in_addr = (struct sockaddr_in*) &peer_addr;
		inet_ntop(AF_INET, &(in_addr->sin_addr.s_addr), addstr, INET_ADDRSTRLEN);
		printf("Peer - file_srv: accepted connection from %s:%hu\n",
				addstr, in_addr->sin_port);
		// Receive message with MSG_PEEK flag on to check message type
		if (recv(fd, buf, P_BUFF_SIZE, MSG_PEEK) == -1){
			errorExit("recv");
		}
		int msg_type;
		sscanf(buf, "%d", &msg_type);
		if (msg_type == MSG_SHUTDOWN){
			// Shutdown sequence
			printf("Peer - shutdown: shutdown message received\n");
			if (close(fd) == -1){
				perror("close");
			}
			if (close(sockfd) == -1){
				perror("close");
			}
			for (i = 0; i < MAX_FILES; i++){
				free(shared_files[i]);
			}
			printf("Peer - shutdown: shutting down\n");
			exit(EXIT_SUCCESS);
		}
		else{ // Message type assumed to be filereq as specified in protocol
			resetBuf(buf);
			if (recv(fd, buf, P_BUFF_SIZE, 0) == -1){
				errorExit("recv");
			}
			msg_filereq_t req;
			sscanf(buf, "%d %s", &req.m_type, req.m_name);
			printf("Peer - file_request: received request for file \"%s\"\n",
					req.m_name);
			// Open file
			int file_desc;
			if ((file_desc = open(req.m_name, O_RDONLY)) == -1){
				// Peer unable to send file
				sendNegativeFileSrv(buf, fd);
			}
			else{
				sendFileSrv(&req, buf, fd, &file_desc);
				// Wait for ACK from client
				resetBuf(buf);
				printf("Peer - file_srv: waiting for ACK from peer\n");
				if (recv(fd, buf, P_BUFF_SIZE, 0) == -1){
					errorExit("recv");
				}
				printf("Peer - file_srv: ACK received\n");
				//sleep(0.5);
				// Send file contents
				sendFile(buf, file_desc, &req, fd);
				//if (close(file_desc) == -1){
				//perror("close");
				//}
			}
		}
	}
}



// Function definitions





bool isShutdown(int argc, char** argv){
	int i;
	for (i = 1; i < argc; i++)
		if (strcmp(argv[i], "shutdown") == 0)
			return TRUE;
	return FALSE;
}

bool isSeed(int argc, char** argv){
	return (argc > 1 && strcmp(argv[1], "seed") == 0);
}

bool isLeech(int argc, char** argv){
	return (argc > 1 && strcmp(argv[1], "leech") == 0);
}

void resetBuf(char *buf){
	memset(buf, 0, sizeof(buf));
}

void sendNotifyMsg(int i, const struct sockaddr_in* addr,
		in_port_t adv_port_num, char buf[buf_size], int sockfd, char* argv[]) {
	printf("Client - share: sending MSG_NOTIFY for \"%s\" @ %s:%d\n",argv[i],
			ADDRESS, adv_port_num);
	// Prepare notify message
	msg_notify_t ntfmsg;
	ntfmsg.m_type = MSG_NOTIFY;
	strcpy(ntfmsg.m_name, argv[i]);
	ntfmsg.m_addr = addr->sin_addr.s_addr;
	ntfmsg.m_port = adv_port_num;
	sprintf(buf, "%d %ui %hu %s", ntfmsg.m_type, ntfmsg.m_addr, ntfmsg.m_port,
			ntfmsg.m_name);
	if (send(sockfd, buf, strlen(buf)+1, 0) == -1) {
		perror("send");
		exit(EXIT_FAILURE);
	}
}

void rcvACK(char buf[buf_size], int sockfd, in_port_t *adv_port_num) {
	// Wait for ACK from server
	msg_ack_t ack;
	resetBuf(buf);
	if (recv(sockfd, buf, P_BUFF_SIZE, 0) == -1) {
		perror("recv");
	}
	// Parse message
	sscanf(buf, "%d %hu", &ack.m_type, &ack.m_port);
	printf("Client - share: receiving MSG_ACK\n");
	if (*adv_port_num == 0) {
		*adv_port_num = ack.m_port;
		printf("Client - share: set port to %hu\n", ack.m_port);
	}
}

void reconnectToServer(int* sockfd, const struct sockaddr_in* addr) {
	if (close(*sockfd) == -1){
		errorExit("close socket");
	}
	if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	if (connect(*sockfd, (struct sockaddr*) &*addr, sizeof(*addr)) == -1) {
		perror("connect to server");
		exit(EXIT_FAILURE);
	}
}

void errorExit(const char *msg){
	perror(msg);
	exit(EXIT_FAILURE);
}

msg_dirhdr_t getDirHdr(char buf[buf_size], int sockfd) {
	// Get number of available files from server
	msg_dirreq_t dirreq;
	dirreq.m_type = MSG_DIRREQ;
	resetBuf(buf);
	sprintf(buf, "%d", dirreq.m_type);
	printf("Client - get_list: sending MSG_DIRREQ\n");
	if (send(sockfd, buf, strlen(buf) + 1, 0) == -1) {
		errorExit("send");
	}
	resetBuf(buf);
	if (recv(sockfd, buf, P_BUFF_SIZE, 0) == -1) {
		errorExit("recv");
	}
	msg_dirhdr_t dirhdr;
	sscanf(buf, "%d %d", &dirhdr.m_type, &dirhdr.m_count);
	printf("Client - get_list: receiving MSG_DIRHDR with %d items\n",
			dirhdr.m_count);
	// Send ACK
	printf("Client - get_list: sending ACK\n");
	resetBuf(buf);
	msg_ack_t ack;
	ack.m_type = MSG_ACK;
	sprintf(buf, "%d", ack.m_type);
	if (send(sockfd, buf, strlen(buf)+1, 0) == -1){
		errorExit("send");
	}
	return dirhdr;
}

void getFileEntries(int* i, msg_dirhdr_t dirhdr, char buf[buf_size], int sockfd,
		file_ent_t avail_files[dirhdr.m_count]) {
	// Get all file entries
	for (*i = 0; *i < dirhdr.m_count; (*i)++) {
		msg_dirent_t dirent;
		resetBuf(buf);
		if (recv(sockfd, buf, P_BUFF_SIZE, 0) == -1) {
			errorExit("recv");
		}
		sscanf(buf, "%d %ui %hu %s", &dirent.m_type, &dirent.m_addr,
				&dirent.m_port, dirent.m_name);
		char addrstr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(dirent.m_addr), addrstr, INET_ADDRSTRLEN);
		printf("Client - get_list: received MSG_DIRENT for \"%s\" @ %s:%hu\n",
				dirent.m_name, addrstr, dirent.m_port);
		// Send ACK
		msg_ack_t ack;
		ack.m_type = MSG_ACK;
		sprintf(buf, "%d", ack.m_type);
		if (send(sockfd, buf, strlen(buf)+1,0) == -1){
			errorExit("send");
		}
		file_ent_t fe;
		fe.fe_addr = dirent.m_addr;
		fe.fe_port = dirent.m_port;
		strcpy(fe.fe_name, dirent.m_name);
		avail_files[*i] = fe;
	}
}

void connectToPeer(const file_ent_t* ent, int* sockfd) {
	struct sockaddr_in peer_addr;
	peer_addr.sin_family = AF_INET;
	peer_addr.sin_addr.s_addr = ent->fe_addr;
	peer_addr.sin_port = ent->fe_port;
	char addrstr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(peer_addr.sin_addr.s_addr), addrstr, INET_ADDRSTRLEN);
	printf("Peer - file_request: connecting to %s:%hu\n", addrstr,
			peer_addr.sin_port);
	if (close(*sockfd) == -1){
		errorExit("close socket");
	}
	if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	if (connect(*sockfd, (struct sockaddr*) &peer_addr, sizeof(peer_addr))
			== -1) {
		perror("connect to peer");
		exit(EXIT_FAILURE);
	}
	printf("Peer - file_request: connected to peer\n");
}

void sendFileSrv(const msg_filereq_t* req, char buf[buf_size], int fd,
		int* file_desc) {
	if ((*file_desc = open(req->m_name, O_RDONLY)) == -1) {
		perror("open");
		exit(EXIT_FAILURE);
	}
	// Get file length
	int file_size = lseek(*file_desc, 0, SEEK_END);
	lseek(*file_desc, 0, SEEK_SET);
	// Send filesrv message
	msg_filesrv_t srv;
	srv.m_type = MSG_FILESRV;
	srv.m_file_size = file_size;
	resetBuf(buf);
	sprintf(buf, "%d %d", srv.m_type, srv.m_file_size);
	printf("Peer - file request: sending MSG_FILESRV\n");
	if (send(fd, buf, strlen(buf) + 1, 0) == -1) {
		errorExit("send");
	}
}

void sendFile(char buf[buf_size], int file_desc, const msg_filereq_t* req,
		int fd) {
	// Send file contents
	resetBuf(buf);
	int nr;
	while ((nr = read(file_desc, buf, P_BUFF_SIZE)) != 0) {
		if (nr == -1) {
			errorExit("read");
		}
	}
	printf("Peer - file_request: sending file \"%s\"\n", req->m_name);
	if (send(fd, buf, strlen(buf) + 1, 0) == -1) {
		errorExit("send");
	}
}

void startServer(in_port_t adv_port_num, int* sockfd) {
	struct sockaddr_in myaddr;
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = adv_port_num;
	inet_pton(AF_INET, ADDRESS, &myaddr.sin_addr);
	close(*sockfd);
	printf("Peer - start_server: starting peer server\n");
	if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		errorExit("socket");
	}
	printf("Peer - start_server: opened socket\n");
	if (bind(*sockfd, (struct sockaddr*) &myaddr, sizeof(myaddr)) == -1) {
		errorExit("bind");
	}
	printf("Peer - start_server: bound socket to port %hu\n", myaddr.sin_port);
	if (listen(*sockfd, 1) == -1) {
		errorExit("listen");
	}
	printf("Peer - start_server: listening on socket\n");
	char addrstr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(myaddr.sin_addr.s_addr), addrstr, INET_ADDRSTRLEN);
	printf("Peer - start_server: address: %s:%hu\n", addrstr, myaddr.sin_port);
}

void sendNegativeFileSrv(char buf[buf_size], int fd) {
	printf("Peer - filesrv: unable to send file\n");
	msg_filesrv_t srv;
	srv.m_type = MSG_FILESRV;
	srv.m_file_size = -1;
	resetBuf(buf);
	sprintf(buf, "%d %d", srv.m_type, srv.m_file_size);
	if (send(fd, buf, strlen(buf) + 1, 0) == -1) {
		errorExit("send");
	}
}

