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
#include "Connection.h"
#include "util.h"
#include "TCPTrack.h"
#include "Packet.h"
#include "GenericError.h"

extern TCPTrack *app;

Connection::~Connection()
{
	pthread_join(lookup_thread_tid, NULL);

	delete srcaddr;
	delete dstaddr;
}

IPAddress & Connection::srcAddr()
{
	return *srcaddr;
}

portnum_t Connection::srcPort()
{
	return srcport;
}

IPAddress & Connection::dstAddr()
{
	return *dstaddr;
}

portnum_t Connection::dstPort()
{
	return dstport;
}

int Connection::getPacketCount()
{
	return packet_count;
}

long Connection::getTotalByteCount()
{
	return total_byte_count;
}

Connection::Connection( TCPCapture &p )
{
        char *tmpsrc;
        char *tmpdst;
        int swap = 0;

        // local network is 192.168.0.0/16
        // set local end as src
        tmpsrc = p.GetPacket().srcAddr().ptr();
        tmpdst = p.GetPacket().dstAddr().ptr();
        if (0 == strcmp( tmpdst, "192.168.3.1")) {
                swap = 0;
        } else if (0 == strcmp( tmpsrc, "192.168.3.1")) {
                swap = 1;
        } else if (0 == strcmp( tmpsrc, "192.168.3.255")) {
                swap = 0;
        } else if (0 == strncmp( tmpdst, "192.168.3.", 10)) {
                swap = 1;
        }

        if (swap > 0) {
                srcaddr = p.GetPacket().dstAddr().Clone();
                dstaddr = p.GetPacket().srcAddr().Clone();
                if ( p.GetPacket().IP_protocol == IPPROTO_TCP ) {
                        srcport = p.GetPacket().tcp().dstPort();
                        dstport = p.GetPacket().tcp().srcPort();
                } else {
                        srcport = 0;
                        dstport = 0;
                }
        } else {
                srcaddr = p.GetPacket().srcAddr().Clone();
                dstaddr = p.GetPacket().dstAddr().Clone();
                if ( p.GetPacket().IP_protocol == IPPROTO_TCP ) {
                        srcport = p.GetPacket().tcp().srcPort();
                        dstport = p.GetPacket().tcp().dstPort();
                } else {
                        srcport = 0;
                        dstport = 0;
                }
        }

	packet_count=1;

	// init per-second stats counters
	last_pkt_ts = time(NULL);
	activity_toggle=true;

	//payload_byte_count = p.GetPacket().payloadLen()-p.GetPacket().tcp().headerLen();
	total_byte_count = p.GetPacket().totalLen();
	total_bytes_this_interval = p.GetPacket().totalLen();

        IP_protocol = p.GetPacket().IP_protocol;

	avg_bps=0;

	finack_from_dst=0;
	finack_from_src=0;
	recvd_finack_from_src=false;
	recvd_finack_from_dst=false;

	srcHost[0] = 0;
	dstHost[0] = 0;
	srcService[0] = 0;
	dstService[0] = 0;

	// Start thread to resolve addresses
	if ( app->names )
		startNameLookup();
}

void Connection::purgeAvgStack()
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
void Connection::updateCounters()
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
void Connection::recalcAvg()
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

time_t Connection::getLastPktTimestamp()
{
	return last_pkt_ts;
}

bool Connection::match( IPAddress &sa, IPAddress &da, portnum_t sp, portnum_t dp )
{
        if( ! (*srcaddr == sa) )
		return false;
	if( !( *dstaddr == da) )
		return false;
        //TODO option to ignore port
	if( dp != dstport  ||  sp != srcport )
		return false;

	return true;
}

time_t Connection::getIdleSeconds()
{
	return time(NULL) - getLastPktTimestamp();
}

void Connection::updateCountersForPacket( TCPCapture &p )
{
	// TODO add an option for payload-based counters
	//payload_bytes = p.GetPacket().payloadLen() - p.GetPacket().tcp().headerLen();
	total_bytes_this_interval += p.GetPacket().totalLen();
	total_byte_count += p.GetPacket().totalLen();
}

bool Connection::acceptPacket( TCPCapture &cap )
{
	Packet *p = &(cap.GetPacket());

        portnum_t tmp_src, tmp_dst;
        if ( p->IP_protocol == IPPROTO_TCP ) {
                tmp_src = p->tcp().srcPort();
                tmp_dst = p->tcp().dstPort();
        } else {
                tmp_src = 0;
                tmp_dst = 0;
        }

	if( match(p->srcAddr(), p->dstAddr(), tmp_src, tmp_dst)
		|| match(p->dstAddr(), p->srcAddr(), tmp_dst, tmp_src) )
	{
		++packet_count;
		activity_toggle=true;

		// recalculate packets/bytes per second counters
		updateCountersForPacket(cap);

		last_pkt_ts = time(NULL);

		return true;
	}
	// packet rejected because addr/port doesn't match
	return false;
}

int Connection::getAllBytesPerSecond()
{
	return avg_bps;
}

// this implements an activity "light" for this connection... should work
// just like the send/receive light on a modem.
// needs to be called frequently (at least once per second) to be of any use.
bool Connection::activityToggle()
{
	bool r = activity_toggle;
	activity_toggle=false;
	return r;
}

void Connection::startNameLookup()
{
	pthread_attr_t attr;

	if( pthread_attr_init( &attr ) != 0 )
		throw GenericError("pthread_attr_init() failed");

	//TODO why and how should this be set?
	//pthread_attr_setstacksize( &attr, SS_TCC );

	if( pthread_create(&lookup_thread_tid, &attr, NameLookup_thread, this) != 0 )
		throw GenericError("pthread_create() failed.");
}

void Connection::doNameLookup()
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
	Connection *c = (Connection *) arg;
	c->doNameLookup();

	return NULL;
}
