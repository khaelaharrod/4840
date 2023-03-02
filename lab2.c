/*
 *
 * CSEE 4840 Lab 2 for 2019
 *
 * Name/UNI: Khaela Harrod (klh2173)
 */
#include "fbputchar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "usbkeyboard.h"
#include <pthread.h>

/* Update SERVER_HOST to be the IP address of
 * the chat server you are connecting to
 */
/* arthur.cs.columbia.edu */
#define SERVER_HOST "128.59.19.114"
#define SERVER_PORT 42000

#define BUFFER_SIZE 128
#define MAX_MSG_LEN BUFFER_SIZE - 17

/*
 * References:
 *
 * http://beej.us/guide/bgnet/output/html/singlepage/bgnet.html
 * http://www.thegeekstuff.com/2011/12/c-socket-programming/
 * 
 */

int sockfd; /* Socket file descriptor */

struct libusb_device_handle *keyboard;
uint8_t endpoint_address;

pthread_t network_thread;
void *network_thread_f(void *);

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

char lower[37] = "abcdefghijklmnopqrstuvwxyz1234567890";
char upper[37] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()";

char symbol[14]       = " -=[]\\\0;'`,./";
char symbol_shift[14] = " _+{}|\0:\"~<>?";



void deleteChar (char * strA, int pos) {
	memmove(strA + pos, strA + pos + 1, strlen(strA) - pos);
}

void insertChar(char* destination, int pos, char ch) //char* seed)
{
	char seed[2] = {ch, 0};
	char strC[BUFFER_SIZE];

	strncpy(strC,destination,pos);
	strC[pos] = '\0';
	strcat(strC,seed);
	strcat(strC,destination+pos);
	strcpy(destination,strC);
}

char debug( int modifier, int key) 
{
	int shift = (modifier == 2 || modifier == 32);
	if (key > 3 && key < 30) {
		if (shift) {
			return upper[key-4];
		}
		else {
			return lower[key-4];
		}
	}
	else if (key == 44) {
		return ' ';
	}
	return 0;
}

void translate( int modifier, int key, int * cursor, char * msg) //int msgLen) 
{
	int shift = (modifier == 2 || modifier == 32);

	if (key == 42) {        //backspace key
		if ((*cursor) > 0)
		deleteChar(msg, --(*cursor));
	}
	else if (key == 79) {   //right arrow
		if ((*cursor) < strlen(msg)) 
			(*cursor)++;
	} 
	else if (key == 80) {   //left arrow
		if ((*cursor) > 0)
			(*cursor)--;
	}
	else if( strlen(msg) < MAX_MSG_LEN ) {

		if (key > 3 && key < 40 ) {
			if (shift) {
				insertChar(msg, (*cursor)++, upper[key-4]);
			}
			else {
				insertChar(msg, (*cursor)++, lower[key-4]);
			}
		}

		if (key > 43 && key < 57 && key != 50) {
			if (shift) {
				insertChar(msg, (*cursor)++, symbol_shift[key-44]);
			}
			else {
				insertChar(msg, (*cursor)++, symbol[key-44]);
			}
		}
	}
}

void clearScreen() 
{
	for (int col = 0 ; col < 64 ; col++) {
		for (int row = 0 ; row < 24 ; row++ ) {
			fbputchar(' ', row, col);
		}
	}
}

void clearLowerScreen() 
{
	for (int col = 0 ; col < 64 ; col++) {
		fbputchar(' ', 22, col);
		fbputchar(' ', 23, col);
	}
}

void clearUpperScreen() 
{
	for (int col = 0 ; col < 64 ; col++) {
		for (int row = 0 ; row < 21 ; row++ ) {
			fbputchar(' ', row, col);
		}
	}
}

void split()
{
	for (int col = 0 ; col < 64; col++) {
		fbputchar('-', 21, col);
	}
}


int main()
{
  int err, col;

  struct sockaddr_in serv_addr;

  struct usb_keyboard_packet packet;
  int transferred;
  char keystate[12];

  if ((err = fbopen()) != 0) {
    fprintf(stderr, "Error: Could not open framebuffer: %d\n", err);
    exit(1);
  }

  /* Draw rows of asterisks across the top and bottom of the screen */
  for (col = 0 ; col < 64 ; col++) {
    fbputchar('*', 0, col);
    fbputchar('*', 23, col);
  }

  fbputs("Hello CSEE 4840 World!", 4, 10);

  /* Open the keyboard */
  if ( (keyboard = openkeyboard(&endpoint_address)) == NULL ) {
    fprintf(stderr, "Did not find a keyboard\n");
    exit(1);
  }
    
  /* Create a TCP communications socket */
  if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
    fprintf(stderr, "Error: Could not create socket\n");
    exit(1);
  }

  /* Get the server address */
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  if ( inet_pton(AF_INET, SERVER_HOST, &serv_addr.sin_addr) <= 0) {
    fprintf(stderr, "Error: Could not convert host IP \"%s\"\n", SERVER_HOST);
    exit(1);
  }

  /* Connect the socket to the server */
  if ( connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    fprintf(stderr, "Error: connect() failed.  Is the server running?\n");
    exit(1);
  }

  /* Start the network thread */
  pthread_create(&network_thread, NULL, network_thread_f, NULL);

  /* Look for and handle keypresses */
  for (;;) {
    libusb_interrupt_transfer(keyboard, endpoint_address,
			      (unsigned char *) &packet, sizeof(packet),
			      &transferred, 0);
    if (transferred == sizeof(packet)) {
      sprintf(keystate, "%02x %02x %02x", packet.modifiers, packet.keycode[0],
	      packet.keycode[1]);
      printf("%s\n", keystate);
      fbputs(keystate, 6, 0);
      if (packet.keycode[0] == 0x29) { /* ESC pressed? */
	break;
      }
    }
  }

  /* Terminate the network thread */
  pthread_cancel(network_thread);

  /* Wait for the network thread to finish */
  pthread_join(network_thread, NULL);

  return 0;
}

void *network_thread_f(void *ignored)
{
  char recvBuf[BUFFER_SIZE];
  int n;
  /* Receive data */
  while ( (n = read(sockfd, &recvBuf, BUFFER_SIZE - 1)) > 0 ) {
    recvBuf[n] = '\0';
    printf("%s", recvBuf);
    fbputs(recvBuf, 8, 0);
  }

  return NULL;
}

