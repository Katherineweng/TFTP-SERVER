/* 
	Author: Katherine Weng
*/

/* To compile: gcc -o server.out server.c lib/libunp.a */
/* "../lib/unp.h" */
#include "../../unpv13e/lib/unp.h"
#include <string.h>

/* Change the #include for submission
	#include "unp.h"
*/

/*---------------------------- GLOBAL VAIRABLES -----------------------------*/
/*  	
		OP # 		TYPE
		01 			RRQ
		02 			WRQ
		03 			DATA
		04 			ACK
		05 			ERROR
 */

/* Buff size in byte */
#define BUFFsize 1024
/* size of one block of data */
#define BLOCK_SIZE 512
/* Sender retransmits its last packet upon NOT receiving data for 1 second */
#define RETRANSMIT_TIME_LIMIT 1 
/* Abort the connection if NOT heard from the other party for 10 seconds */
#define ABORT_TIME_LIMIT 10

int MAX_TID_COUNT; /* end port - start port */
int COUNT = 0; /* number of port taken */
int TIME_COUNT = 0; /* How many 1 second has passed by */
char* HOLDING_DATA; /* data that might need to be resent */
unsigned short int blockNo; /* block# that might need to be resent */
int SOCKFD; /* child sockfd */
struct sockaddr_in CLIADDR; /* child client address */

/*---------------------------------------------------------------------------*/

/*-------------------------- Struct Declaration -----------------------------*/
/* RRQ or WRQ */
typedef struct RQ {
	unsigned short int OpCode; /* 2 bytes */

	char FileName[BLOCK_SIZE+1]; /* string */
} RQ;
/* Data */
typedef struct DATA {
	unsigned short int OpCode; /* 2 bytes */
	unsigned short int BlockNo; /* 2 bytes */
	char Data[BLOCK_SIZE+1];	/* n <= 512 bytes */
} DATA;
/* Acknowledgement */
typedef struct ACK {
	unsigned short int OpCode; /* 2 bytes */
	unsigned short int BlockNo; /* 2 bytes */
} ACK;
/* error */
typedef struct ERR {
	unsigned short int OpCode; /* 2 bytes */
	unsigned short int ErrorCode; /* 2 bytes */
	char ErrMsg[BLOCK_SIZE+1]; /* n bytes */
} ERR;
/*----------------------------------------------------------------------------*/

/*-------------------------- Function Declaration ---------------------------------------*/
void sigchld_handler(int signum); /* SIGCHLD signal handler */
void send_data(); /* Send DATA Packet */
void send_ack(); /* Send ACK Packet */
void send_error(unsigned short int errCode, char* errMsg); /* Send Error Packet */
void execute_request(RQ* packet); /* execute requests */
void execute_RRQ(int file); /* execute RRQ */
int verify_RRQstatus(char* msg); /* Checks ACK packet received after each DATA send */
void execute_WRQ(int file); /* execute WRQ */
int verify_WRQstatus(char* msg, int file); /* Checks DATA packet received before each ACK send */
void RRQhandler(int signum); /* SIGALARM handler for RRQ, resend DATA packet */
void WRQhandler(int signum); /* SIGALARM handler for WRQ, resend ACK packet */
/*----------------------------------------------------------------------------------------*/

int main( int argc, char **argv ) {
	/*-------------------------- CHECK COMMAND LINE ARGUMENTS --------------------------*/
	if ( argc != 3 ) {
		fprintf( stderr, "ERROR: Invalid argument(s)\n"
						 "USAGE: a.out <start of port range>"
						 " <end of port range>\n" );
		return EXIT_FAILURE;
	}

	int start = atoi( *(argv+1) );	/* start of port range */
	int end	  = atoi( *(argv+2) );	/* end of port range*/

	MAX_TID_COUNT = end - start;

	printf("start: %d, end: %d\n", start, end);

	/*----------------------------------------------------------------------------------*/

	/*----------------------------------- UDP SETUP ------------------------------------*/
	int					sockfd, n;
	struct sockaddr_in	servaddr, cliaddr;

	sockfd = Socket(AF_INET, SOCK_DGRAM, 0);


	/* server info */
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(start);

	Bind(sockfd, (SA *) &servaddr, sizeof(servaddr));

	socklen_t	len = sizeof(cliaddr);
	char*		msg = calloc( BUFFsize, sizeof(char) ); /* holds recvd packet */
	/*----------------------------------------------------------------------------------*/

	/*----------------------------- START RECEIVING REQUEST ----------------------------*/

	printf("Sockfd %d bind to port %d\n", sockfd, start);
	/*signal(SIGCHLD, sigchld_handler);*/
	for ( ; ; ) {

		Recvfrom(sockfd, msg, BUFFsize, 0, (SA *) &cliaddr, &len);
		printf( "Parent: fd %d received %d bytes\n", sockfd, n );

		if ( COUNT == MAX_TID_COUNT ) {
			printf("TOO BUSY\n");
			continue;
		}

		struct sockaddr_in childserv;
		/* interact with client using a different port */
		SOCKFD = Socket(AF_INET, SOCK_DGRAM, 0);

		/* child server info */
        bzero(&childserv, sizeof(childserv));
        childserv.sin_family = AF_INET;
        childserv.sin_addr.s_addr = htonl(INADDR_ANY);
        childserv.sin_port = htons(end-COUNT); /* end-Count will give the port number to use */

		Bind(SOCKFD, (SA *) &childserv, sizeof(childserv));
		printf("SOCKET %d bind to port %d\n", SOCKFD, end-COUNT);

		COUNT++;

		bzero(&CLIADDR, sizeof(CLIADDR));
		CLIADDR = cliaddr;
		HOLDING_DATA = calloc(BLOCK_SIZE+1, sizeof(char));

		pid_t pid = fork(); /* create a child process */
		if ( pid == -1 ) { /* Error checking */
		    perror( "fork() failed" );
    		return EXIT_FAILURE;
		}

		/* Let child process handle the request */
		if ( pid == 0 ) {

			close(sockfd); /* close parent socket in child process */

			printf("Child: USING SOCKFD %d\n", SOCKFD);

			/* Get OPCODE */
			unsigned short int* opcode_ptr = (unsigned short int*) msg;
			unsigned short int OpCode = ntohs(*opcode_ptr);

			/* If the request is not RRQ nor WRQ */
			if ( OpCode != 1 && OpCode != 2 ) {
				send_error(0, "INVALID REQUEST\0");
				return EXIT_FAILURE;
			}

			/* prepare request packet struct */
			RQ* packet = calloc( 1, sizeof(RQ) );
			packet->OpCode = OpCode;
			strcpy(packet->FileName, msg+2); /* Get file name */

			printf("OP: %hu, FILE: %s\n", packet->OpCode, packet->FileName);

			execute_request(packet); /* call to execute given request */

			close(SOCKFD); /* close child socket in child process after execution */

			return EXIT_SUCCESS;
		}

		/* Let parent process go back to the top of the loop waiting for next packet */
		else {
			memset(msg, 0, BUFFsize); /* reset msg string */
			printf("Parent: child %d is created\n", pid);
			printf("Parent: BACK TO TOP OF LOOP\n");
		}
	}

	free(msg);
	return EXIT_SUCCESS;
}

void send_data() {
	/* prepare DATA packet struct */
	DATA* packet = calloc(1, sizeof(DATA));
	packet->OpCode = htons(3);
	packet->BlockNo = htons(blockNo);
	strcpy(packet->Data, HOLDING_DATA);

	Sendto(SOCKFD, packet, strlen(HOLDING_DATA)+4, 0, (SA *) &CLIADDR, sizeof(CLIADDR));
}

void send_ack() {
	/* prepare ACK packet struct */
	ACK* packet = calloc(1, sizeof(ACK));
	packet->OpCode = htons(4);
	packet->BlockNo = htons(blockNo);

	Sendto(SOCKFD, packet, 4, 0, (SA *) &CLIADDR, sizeof(CLIADDR));
}

/*
Error Codes
   Value     Meaning
   0         Not defined, see error message (if any).
   1         File not found.
   2         Access violation.
   3         Disk full or allocation exceeded.
   4         Illegal TFTP operation.
   5         Unknown transfer ID.
   6         File already exists.
   7         No such user.
*/
void send_error(unsigned short int errCode, char* errMsg) {
	/* prepare ERROR packet struct */
	ERR* packet = calloc(1, sizeof(ERR));
	packet->OpCode = htons(5);
	packet->ErrorCode = htons(errCode);
	memcpy(packet->ErrMsg, errMsg, strlen(errMsg));

	Sendto(SOCKFD, packet, strlen(errMsg)+4, 0, (SA *) &CLIADDR, sizeof(CLIADDR));
	abort();
}

void execute_request(RQ* packet) {
	FILE* file;

	if (packet->OpCode == 1) { /* RRQ */
		/* Check if file is available */
		if ( (file = fopen(packet->FileName, "r") ) == NULL) {
			send_error(1, "File not found.");
			fclose(file);
			abort();
		}
		int f = open(packet->FileName, O_RDONLY);
		printf("file descriptor: %d\n", f);
		execute_RRQ(f);
		close(f);
	} else if (packet->OpCode == 2) { /* WRQ */
		/* Check if file already exists */
		if ( (file = fopen(packet->FileName, "r") ) != NULL) {
			send_error(6, "File already exists.");
			fclose(file);
			abort();
		}
		/* Create file and Get file descriptor */
		int f = open(packet->FileName, O_CREAT | O_WRONLY);
		printf("file descriptor: %d\n", f);
		execute_WRQ(f);
		close(f);
	}
}

void execute_RRQ(int file) {

	socklen_t	len = sizeof(CLIADDR);
	memset(HOLDING_DATA, 0, BLOCK_SIZE+1);
	char* msg = calloc(BUFFsize, sizeof(char));
	int n, rc;
	blockNo = 1; /* block # start with 1 */

	do {
		n = read( file, HOLDING_DATA, 512 );
		/*printf("HOLDING_DATA: %s\n", HOLDING_DATA);*/
		printf("Block No.: %hu, Sending %d bytes\n", blockNo, n);

		signal(SIGALRM, RRQhandler);
		alarm(0); /* Cancel the previous alarm */

		send_data(); /* call to sent data packet */

		alarm(1);

		Recvfrom(SOCKFD, msg, BUFFsize, 0, (SA *) &CLIADDR, &len);

		alarm(0); /* Cancel the previous alarm */

		verify_RRQstatus(msg); /* Check recvd  ACK packet */

		memset(HOLDING_DATA, 0, BLOCK_SIZE+1);
		memset(msg, 0, BUFFsize);
		blockNo++;

	} while ( n == 512 );

	free(HOLDING_DATA);
	free(msg);
}

int verify_RRQstatus(char* msg) {
	/* Get OPCODE */
	unsigned short int* opcode_ptr = (unsigned short int*) msg;
	unsigned short int OpCode = ntohs(*opcode_ptr);

	/* If received ACK packet and the block # matches what we sent */
	if (OpCode == 4 && ntohs(*(opcode_ptr+1)) == blockNo) {
		return 1;
	}
	printf("Failed verifying OpCode: %hu\n", OpCode);
	abort();
}

void execute_WRQ(int file) {
	blockNo = 0;
	/* start by sending ack packet with block# 0 */
	send_ack();

	socklen_t	len = sizeof(CLIADDR);
	char* msg = calloc(BUFFsize, sizeof(char));
	int n;

	do {
		/* wait to receive data */
		signal(SIGALRM, WRQhandler);
		alarm(0);

		alarm(1);

		Recvfrom(SOCKFD, msg, BUFFsize, 0, (SA *) &CLIADDR, &len);

		alarm(0); /* Cancel the previous alarm */

		/* Check recvd packet */
		n = verify_WRQstatus(msg, file);

		/*printf("N HERE: %d\n", n);*/

		memset(msg, 0, BUFFsize);

	} while ( n == 512 );

	free(msg);
}
/* returns number of bytes written to the file (i.e. data size received) */
int verify_WRQstatus(char* msg, int file) {
	/* Get OPCODE */
	unsigned short int* opcode_ptr = (unsigned short int*) msg;
	unsigned short int OpCode = ntohs(*opcode_ptr);
	int n;

	char* buff = calloc(BLOCK_SIZE+1, sizeof(char));
	strcpy(buff, msg+4);
	/*printf("%s\n", buff);*/
	if (OpCode == 3) {
		blockNo = ntohs(*(opcode_ptr+1)); /* set block # for ACK packet */
		send_ack();
		if ( ( n = write(file, buff, strlen(buff)) ) < 0 ) { /* +4 to skip first 4 bytes */
			send_error(0, "write() failed");
			abort();
		}
		return n;
	}
	printf("Failed verifying OpCode: %hu\n", OpCode);
	abort();
}


void RRQhandler(int signum) {
	if (signum == SIGALRM) {
		if (TIME_COUNT < ABORT_TIME_LIMIT) {
			send_data(); /* Resend DATA Packet */
			TIME_COUNT++; 
			printf("Counting: %d\n", TIME_COUNT);
			alarm(1); /* Schedule next 1 second */
		} else {
			TIME_COUNT = 0;
			abort(); /* if reached ABORT_TIME_LIMIT */
		}
	}
}

void WRQhandler(int signum) {
	if (signum == SIGALRM) {
		if (TIME_COUNT < ABORT_TIME_LIMIT) {
			send_ack(); /* Resend ACK Packet */
			TIME_COUNT++; 
			printf("Counting: %d\n", TIME_COUNT);
			alarm(1); /* Schedule next 1 second */
		} else {
			TIME_COUNT = 0;
			abort(); /* if reached ABORT_TIME_LIMIT */
		}
	}
}

