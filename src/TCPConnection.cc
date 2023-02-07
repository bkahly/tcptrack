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
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_PCAP_PCAP_H
#include <pcap/pcap.h>
#elif HAVE_PCAP_H
#include <pcap.h>
#endif
#include <time.h>
#include "headers.h"
#include "TCPConnection.h"
#include "util.h"
#include "TCPTrack.h"
#include "TCPPacket.h"
#include "SocketPair.h"
#include "GenericError.h"

extern TCPTrack *app;

TCPConnection::~TCPConnection()
{
	pthread_join(lookup_thread_tid, NULL);

	delete srcaddr;
	delete dstaddr;
	delete endpts;
}

bool TCPConnection::isFinished()
{
	if( state == TCP_STATE_CLOSED || state == TCP_STATE_RESET )
		return true;
	return false;
}

IPAddress & TCPConnection::srcAddr()
{
	return *srcaddr;
}

portnum_t TCPConnection::srcPort()
{
	return srcport;
}

IPAddress & TCPConnection::dstAddr()
{
	return *dstaddr;
}

portnum_t TCPConnection::dstPort()
{
	return dstport;
}

int TCPConnection::getPacketCount()
{
	return packet_count;
}

long TCPConnection::getTotalByteCount()
{
	return total_byte_count;
}

int TCPConnection::getState()
{
	return state;
}

TCPConnection::TCPConnection( TCPCapture &p )
{
	srcaddr = p.GetPacket().srcAddr().Clone();
	dstaddr = p.GetPacket().dstAddr().Clone();
	srcport = p.GetPacket().tcp().srcPort();
	dstport = p.GetPacket().tcp().dstPort();

	packet_count=1;
	if( p.GetPacket().tcp().syn() )
		state = TCP_STATE_SYN_SYNACK;
	else
		state = TCP_STATE_UP;

	// init per-second stats counters
	last_pkt_ts = time(NULL);
	activity_toggle=true;

	//payload_byte_count = p.GetPacket().payloadLen()-p.GetPacket().tcp().headerLen();
	total_byte_count = p.GetPacket().totalLen();
	total_bytes_this_interval = p.GetPacket().totalLen();

	avg_bps=0;

	finack_from_dst=0;
	finack_from_src=0;
	recvd_finack_from_src=false;
	recvd_finack_from_dst=false;

	endpts = new SocketPair( *srcaddr, srcport, *dstaddr, dstport);

	srcHost[0] = 0;
	dstHost[0] = 0;
	srcService[0] = 0;
	dstService[0] = 0;

	// Start thread to resolve addresses
	if ( app->names )
		startNameLookup();
}

void TCPConnection::purgeAvgStack()
{
	struct timeval now;
	gettimeofday(&now,NULL);

	// Keep 3 seconds of statistics
	uint64_t limit;
	limit = now.tv_sec * 1000000 + now.tv_usec;
	limit -= 3 * 1000000;

	list<struct avgstat>::iterator i;
	for( i=avgstack.begin(); i!=avgstack.end(); i++ )
	{
		if( i->ts <= limit )
		{
			avgstack.erase(i,avgstack.end());
			break;
		}
	}
}

// updates the byte counters
// must be called once per UI refresh interval
void TCPConnection::updateCounters()
{
	purgeAvgStack();

	struct timeval now;
	gettimeofday(&now,NULL);

	struct avgstat s;
	s.ts = now.tv_sec * 1000000 + now.tv_usec;
	s.size = total_bytes_this_interval;
	avgstack.push_front(s);

	total_bytes_this_interval = 0;
}

// recalculate packets/bytes per second counters
void TCPConnection::recalcAvg()
{
	unsigned int total_bytes = 0;
	uint64_t time1 = 0;
	uint64_t time2 = 0;

	list<struct avgstat>::iterator i;
	for( i=avgstack.begin(); i!=avgstack.end(); i++ )
	{
		total_bytes += i->size;
		time1 = i->ts;
		if (time2 == 0)
			time2 = i->ts;
	}

	uint64_t time_interval = time2 - time1 + app->refresh_intvl;
	if (time_interval > 0)
	{
		avg_bps = total_bytes / (time_interval / 1000000.0);
	}
	else
	{
		avg_bps = 0;
	}
}

time_t TCPConnection::getLastPktTimestamp()
{
	return last_pkt_ts;
}

bool TCPConnection::match( IPAddress &sa, IPAddress &da, portnum_t sp, portnum_t dp )
{
	if( ! (*srcaddr == sa) )
		return false;
	if( !( *dstaddr == da) )
		return false;
	if( dp != dstport  ||  sp != srcport )
		return false;

	return true;
}

time_t TCPConnection::getIdleSeconds()
{
	return time(NULL) - getLastPktTimestamp();
}

void TCPConnection::updateCountersForPacket( TCPCapture &p )
{
	// TODO add an option for payload-based counters
	//payload_bytes = p.GetPacket().payloadLen() - p.GetPacket().tcp().headerLen();
	total_bytes_this_interval += p.GetPacket().totalLen();
	total_byte_count += p.GetPacket().totalLen();
}

bool TCPConnection::acceptPacket( TCPCapture &cap )
{
	TCPPacket *p = &(cap.GetPacket());
	unsigned int payloadlen = p->payloadLen() - p->tcp().headerLen();

	if( state == TCP_STATE_CLOSED )
		return false;

	if(  match(p->srcAddr(), p->dstAddr(), p->tcp().srcPort(), p->tcp().dstPort())
		|| match(p->dstAddr(), p->srcAddr(), p->tcp().dstPort(), p->tcp().srcPort()) )
	{
		++packet_count;
		activity_toggle=true;

		// recalculate packets/bytes per second counters
		updateCountersForPacket(cap);

		if( p->tcp().fin() )
		{
			// if this is a fin going from cli->srv
			// expect an appropriate ack from server
			if( p->srcAddr() == *srcaddr )
			{
				if( payloadlen==0 )
					finack_from_dst = p->tcp().getSeq()+1;
				else
					finack_from_dst = p->tcp().getSeq()+payloadlen+1;
				recvd_finack_from_dst=false;
			}
			if( p->srcAddr() == *dstaddr )
			{
				if( payloadlen==0 )
					finack_from_src = p->tcp().getSeq()+1;
				else
					finack_from_src = p->tcp().getSeq()+payloadlen+1;
				recvd_finack_from_src=false;
			}
		}

		if( state == TCP_STATE_SYNACK_ACK )
		{
			if( p->tcp().ack() )
				state = TCP_STATE_UP; // connection up
		}
		else if( state == TCP_STATE_SYN_SYNACK )
		{
			if( p->tcp().syn() && p->tcp().ack() )
				state = TCP_STATE_SYNACK_ACK; // SYN|ACK sent, awaiting ACK
		}
		else if( state == TCP_STATE_UP )
		{
			if( p->tcp().fin() )
				state = TCP_STATE_FIN_FINACK; // FIN sent, awaiting FIN|ACK
		}
		else if( state == TCP_STATE_FIN_FINACK )
		{
			if( p->tcp().ack() )
			{
				if( p->srcAddr() == *srcaddr )
					if( p->tcp().getAck() == finack_from_src )
						recvd_finack_from_src=true;
				if( p->srcAddr() == *dstaddr )
					if( p->tcp().getAck() == finack_from_dst )
						recvd_finack_from_dst=true;
				if( recvd_finack_from_src && recvd_finack_from_dst )
					state=TCP_STATE_CLOSED;
			}
		}
		if( p->tcp().rst() )
			state = TCP_STATE_RESET;

		last_pkt_ts = time(NULL);

		return true;
	}
	// packet rejected because this connection is closed.
	return false;
}

int TCPConnection::getAllBytesPerSecond()
{
	return avg_bps;
}

// this implements an activity "light" for this connection... should work
// just like the send/receive light on a modem.
// needs to be called frequently (at least once per second) to be of any use.
bool TCPConnection::activityToggle()
{
	bool r = activity_toggle;
	activity_toggle=false;
	return r;
}

SocketPair & TCPConnection::getEndpoints()
{
	return *endpts;
}

void TCPConnection::startNameLookup()
{
	pthread_attr_t attr;

	if( pthread_attr_init( &attr ) != 0 )
		throw GenericError("pthread_attr_init() failed");

	//TODO why and how should this be set?
	//pthread_attr_setstacksize( &attr, SS_TCC );

	if( pthread_create(&lookup_thread_tid, &attr, NameLookup_thread, this) != 0 )
		throw GenericError("pthread_create() failed.");
}

void TCPConnection::doNameLookup()
{
	struct sockaddr_storage peer_addr;
	sockaddr *peer_addr_sa = (sockaddr *)&peer_addr;
	sockaddr_in *peer_addr_sin = (sockaddr_in *)&peer_addr;
	sockaddr_in6 *peer_addr_sin6 = (sockaddr_in6 *)&peer_addr;
	socklen_t peer_addr_len;

	int stat;

	peer_addr_len = sizeof(peer_addr);

	srcaddr->GetSockAddr( peer_addr_sa, &peer_addr_len );
	if ( peer_addr_sa->sa_family == AF_INET )
	{
		peer_addr_sin->sin_port = htons(srcport);
	}
	else if ( peer_addr_sa->sa_family == AF_INET6 )
	{
		peer_addr_sin6->sin6_port = htons(srcport);
	}

	stat = getnameinfo(
		(struct sockaddr *) &peer_addr, peer_addr_len,
		srcHost, NI_MAXHOST,
		srcService, NI_MAXSERV,
		NI_IDN);
	if (stat != 0)
	{
		// Ignore the error
		srcHost[0] = 0;
		srcService[0] = 0;
//		fprintf(stderr, "getnameinfo: %s\n", gai_strerror(stat));
	}

	peer_addr_len = sizeof(peer_addr);

	dstaddr->GetSockAddr( peer_addr_sa, &peer_addr_len );
	if ( peer_addr_sa->sa_family == AF_INET )
	{
		peer_addr_sin->sin_port = htons(dstport);
	}
	else if ( peer_addr_sa->sa_family == AF_INET6 )
	{
		peer_addr_sin6->sin6_port = htons(dstport);
	}

	stat = getnameinfo(
		(struct sockaddr *) &peer_addr, peer_addr_len,
		dstHost, NI_MAXHOST,
		dstService, NI_MAXSERV,
		NI_IDN);
	if (stat != 0)
	{
		// Ignore the error
		dstHost[0] = 0;
		dstService[0] = 0;
//		fprintf(stderr, "getnameinfo: %s\n", gai_strerror(stat));
	}
}

void *NameLookup_thread( void * arg )
{
	TCPConnection *c = (TCPConnection *) arg;
	c->doNameLookup();

	return NULL;
}
