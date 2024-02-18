/*
 *  The code in this file is part of tcptrack. For more information see
 *    http://www.rhythm.cx/~steve/devel/tcptrack
 *
 *     Copyright (C) Steve Benson - 2003
 *
 *  tcptrack is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your
 *  option) any later version.
 *
 *  tcptrack is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#ifndef CONNECTION_H
#define CONNECTION_H 1

#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <list>
#include "util.h"
#include "Packet.h"

using namespace std;

typedef unsigned short portnum_t;

class Connection
{
public:
	// constructor, which needs the initial packet that created this
	// connection, or any packet that is going from client->server.
	// See comments in Guesser.cc for an explanation...
	Connection( Packet &p );
	~Connection();

	// returns true if the given Packet is relevant to this connection.
	bool match( Packet &p, int &swap );

	// see if packet p is relevant to this connection.
	// if it is, true will be returned and this object's internal
	// state will be changed to reflect the new packet.
	bool acceptPacket( Packet &p );

	// get the addresses/ports which are the endpoints for this
	// connection.
	IPAddress & srcAddr();
	portnum_t srcPort();
	IPAddress & dstAddr();
	portnum_t dstPort();

	char srcHost[NI_MAXHOST];
	char dstHost[NI_MAXHOST];
	char srcService[NI_MAXSERV];
	char dstService[NI_MAXSERV];

        unsigned short IP_protocol;

	// timestamp of first packet seen
	util_time_t getFirstPktTimestamp();

	// timestamp of last packet sent either way
	util_time_t getLastPktTimestamp();

	// number of nanoseconds since last packet sent either way
	util_time_t getIdleTimeNS();

	// number of seconds since last packet sent either way
	util_time_t getIdleSeconds();

	// called to perform internal updates to counters and stuff.
	// should be called exactly once per refresh interval.
	void updateCounters();

	// called to recalculate averages
	void recalcAvg();

	// this implements an "activity light" that works just like the
	// activity LEDs on a modem.  returns true if a packet was sent
	// since the last time it was called.
	bool activityToggle();

	// number of packets sent for this connection
	int getLocalPacketCount();
	int getRemotePacketCount();

	// number of bytes sent either way in total for this connection
	long getTotalByteCount();

	// average number of bytes per second this connection is doing
	// this counts the tcp and ip headers (but not link layer header)
	int getAllBytesPerSecond();

	void doNameLookup();

private:
	void purgeAvgStack();
	void updateCountersForPacket( Packet &p );

	unsigned long finack_from_dst;
	unsigned long finack_from_src;
	bool recvd_finack_from_src;
	bool recvd_finack_from_dst;

	portnum_t srcport; // client port
	portnum_t dstport; // server port
	IPAddress *srcaddr; // client addr
	IPAddress *dstaddr; // server addr

	util_time_t first_pkt_ts;
	util_time_t last_pkt_ts;

	int loc_packet_count;
	int rem_packet_count;

	long total_byte_count;

	bool activity_toggle;

	list<struct avgstat> avgstack;	
	int avg_bps; // bytes per second

	unsigned int total_bytes_this_interval;

	pthread_t lookup_thread_tid;

	void startNameLookup();
};

void *NameLookup_thread( void * arg );

#endif
