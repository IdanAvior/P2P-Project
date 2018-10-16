/**
 *  p2p.h - Header file for nework communications exercise
 *
 *  part of a peer-to-peer file sharing system
 *  Advanced Programming course 
 *  Ort Braude College
 *
 *  @Date: 	12 December 2016
 *  @Author:   	Ron Sivan    
 *  @email:	ronny.sivan@gmail.com
 *
 *
 * This file is part of a peer-to-peer system allows sharing of files among 
 * participating clients. It consits of several source files:
 *
 * 1. p2p.h - a common header file.
 * 2. p2pserver.c - the server program, only one of which should be active.
 * 3. p2pclient.c - a client, several copies of which may be active 
 *    concurrently.
 * 
 * The server keeps track of what shareable files exist and which clients have 
 * them. Clients may both offer files for sharing and ask for files to be sent
 * to them. A client may request a peer (another client) to serve it a file it 
 * has; the peer responds using its serving subprocess.
 *
 * The protocol is as follows:
 *
 * 1. A client may send a MSG_NOTIFY message, notifying the server of a file 
 *    at its disposal it is willing to share. The message contains the name of 
 *    the file, the IP address of the client and its port number. (See below.)
 *
 *    The server responds with a MSG_ACK message which contains the client's 
 *    port number. If the client did not specify a port number (port was 0 in 
 *    the MSG_NOTIFY), the server generates a new, unique port number and 
 *    places it in the MSG_ACK message. 
 *
 *    The client should use that port when serving requests from other clients.
 *    Having the port echoed in the MSG_NOTIFY insures that each client is
 *    assigned only one port, upon the first time it notifies the server. 
 *
 * 2. A client may ask the server to see what files are being shared, using
 *    the message MSG_DIRREQ. 
 *
 *    The server responds to this message with MSG_DIRHDR containing the 
 *    number of available files, followed by that number of MSG_DIRENT 
 *    messages. Each MSG_DIRENT message contains the name of a file and the 
 *    IP address and port number of some client having that file. 
 *    If more than one client is known to have the same file, this information 
 *    is conveyed to the requesting client through several MSG_DIRENT 
 *    messages, all bearing the same file name, each specifying one of the 
 *    possible sources to the file.     
 *
 * 3. Any client, say R, may ask any other client, say S, to send it a copy
 *    of a file S may have at its disposal, by using the MSG_FILEREQ message. 
 *    The message contains the name of the file R desires and is sent to the
 *    address and port the server has associated with client S when the latter 
 *    reported having the file.
 *
 *    S responds by sending a MSG_FILESRV message, containing the size of the 
 *    requested file, followed by the contents of the whole file. However, 
 *    S can legitimately indicate a length of zero and send nothing if it so
 *    chooses (because, for example, S no longer posesses a copy of the file,
 *    or it is too busy.)
 * 
 *    R may, in this case, try a different client from among those listed by 
 *    the server as associated with the requested file. 
 *
 *    It is expected of clients which have successfully obtained a file from a 
 *    peer to register themselves with the server as now having that file too. 
 */

#include <netinet/in.h>

/* 
 *  Port number for p2p server. 
 *  Also the base for all port numbers allocated to peers.
 */ 
#define P_SRV_PORT 12345	

/* 
 *  Length of names of files 
 */
#define P_NAME_LEN 128		

/* 
 *  Size of buffer to read and write files 
 */
#define P_BUFF_SIZE 1024		

/*
 *  A structure for describing a shareable file.
 *  The structure contains the name of the file and the IP address 
 *  of a peer willing to share the file, and the port number on which 
 *  that peer is listening. 
 */

#define SHMKEY_PORT 65973
#define SHMKEY_NUM_ENT 65974
#define SHMKEY_FILE_ENT 65975
#define MAX_FILES 100

typedef struct file_ent
{
    char               fe_name[P_NAME_LEN + 1];
    in_addr_t          fe_addr;
    in_port_t          fe_port;
} 
    file_ent_t;

/*
 *  message types
 */
#define MSG_DUMP 	-2
#define MSG_SHUTDOWN 	-1

#define MSG_ERROR     	 0

#define MSG_NOTIFY    	 1
#define MSG_ACK       	 2
#define MSG_DIRREQ    	 3
#define MSG_DIRHDR    	 4
#define MSG_DIRENT    	 5 
#define MSG_FILEREQ   	 6
#define MSG_FILESRV   	 7

/*
 *  message formats: MSG_NOTIFY 
 *  Used by clients to notify the server of a files they are willing to share.
 */
typedef struct msg_notify 
{
    int         m_type;	 			// = MSG_NOTIFY
    char        m_name[P_NAME_LEN + 1];		// name of file the client has
    in_addr_t   m_addr;				// client's IP address
    in_port_t   m_port;				// client's perceived port
}
    msg_notify_t;

/*
 *  message formats: MSG_ACK
 *  The way the server responds to MSG_NOTIFY. It contains the client's port
 *  number, or, if the client did not specify a port, a new, unique number.
 */
typedef struct msg_ack 
{
    int         m_type;				// = MSG_ACK
    in_port_t   m_port;				// client's assigned port
}
    msg_ack_t;

/*
 *  message formats: MSG_DIRREQ
 *  Used by clients to obtain a list of the files currently being shared.
 */
typedef struct msg_dirreq
{
    int         m_type;				// = MSG_DIRREQ
}
    msg_dirreq_t;

/*
 *  message formats: MSG_DIRHDR
 *  Used by the server to respond to MSG_DIRREQ.
 */
typedef struct msg_dirhdr
{
    int         m_type;				// = MSG_DIRHDR
    int         m_count;			// number of files in list
}
    msg_dirhdr_t;

/*
 *  message formats: MSG_DIRENT
 *  Used by the server to convey the name of one file being offered.
 */
typedef struct msg_dirent
{
    int         m_type;				// = MSG_DIRENT
    char        m_name[P_NAME_LEN + 1];		// name of the file requested
    in_addr_t   m_addr;		// IP address of client offering the file
    in_port_t   m_port;		// Port number of client offering the file
}
    msg_dirent_t;

/*
 *  message formats: MSG_FILEREQ
 *  Used by a peer to request a file from another peer.
 */
typedef struct msg_filereq 
{
    int         m_type;				// = MSG_FILEREQ
    char        m_name[P_NAME_LEN + 1];		// name of the file requested
}
    msg_filereq_t;


/*
 *  message formats: MSG_FILESRV
 *  Used by a peer in response to MSG_FILEREQ message from a nother peer.
 */
typedef struct msg_filesrv
{
    int         m_type;				// = MSG_FILESRV
    int         m_file_size;			// size of file
}
    msg_filesrv_t;


/*
 *  message formats: MSG_SHUTDOWN
 *  Used by the server to bring down the system. 
 *  Any client receiving this message should terminate. 
 */
typedef struct msg_shutdown
{
    int         m_type;				// = MSG_SHUTDOWN
}
    msg_shutdown_t;

typedef struct shmPort
{
	int num;
} shmPort;

typedef struct shmNumEntries
{
	int num;
} shmNumEntries;

typedef struct shmFileEntries
{
	file_ent_t entries[MAX_FILES];
} shmFileEntries;



