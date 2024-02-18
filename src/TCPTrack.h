#ifndef TCPTRACK_H
#define TCPTRACK_H 1

#include <pthread.h>
#include <string>
#include "util.h"
#include "Sniffer.h"
#include "TextUI.h"
#include "PacketBuffer.h"
#include "TCContainer.h"

using namespace std;

class TCPTrack
{
public:
	TCPTrack();
	void run( int argc, char **argv ); // run tcptrack
	void shutdown(); // quit tcptrack

	// general tcptrack configuration settings
	// should probably move these later
	util_time_t remto; // closed connection removal timeout
	bool detect; // detect pre-existing connections?
	bool names;  // Convert addresses/ports to names?
	bool promisc; // enable promisc mode?
	unsigned int refresh_intvl; // How often are we refreshing the UI (usec)

	// other threads call this when they have an unhandled exception.
	// shuts tcptrack down abruptly and prints the error.
	void fatal( string msg );
private:
	Sniffer *s;
	TextUI *ui;
	PacketBuffer *pb;
	TCContainer *c;
	
	string ferr; // fatal error message sent from another thread
	pthread_mutex_t ferr_lock;
};

void printusage(int argc, char **argv);
struct config parseopts(int argc, char **argv);

#endif
