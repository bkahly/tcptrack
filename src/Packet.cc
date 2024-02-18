#include <cassert>
#include <iostream>
#include <unistd.h>
#include "IPv4Address.h"
#include "IPv6Address.h"
#include "Packet.h"
#include "headers.h"
#include "util.h"

Packet::Packet( const u_char *data, unsigned int data_len, util_time_t ts )
{
	struct sniff_ip *ip = (struct sniff_ip *)data;

	if( ip->ip_v == 4)
	{
		// make sure that the various length fields are long enough to contain
		// an IPv4 header (at least 20 bytes).
		assert( data_len >= 20 );
		assert( ntohs(ip->ip_len) >= 20 ); // TODO: is this right?
		assert( ip->ip_hl >= 5 );

		total_len=ntohs(ip->ip_len);

		m_src = new IPv4Address(ip->ip_src);
		m_dst = new IPv4Address(ip->ip_dst);
		header_len=ip->ip_hl*4;
                IP_protocol = ip->ip_p;
	}
	else if( ip->ip_v == 6 )
	{
		struct sniff_ip6 *ip6 = (struct sniff_ip6 *)data;

		total_len = htons(ip6->ip_len);
		header_len = IP6_HEADER_LEN;
                IP_protocol = ip6->ip_next;
		m_src = new IPv6Address(ip6->ip_src);
		m_dst = new IPv6Address(ip6->ip_dst);
	}
	else
	{
		// Unknown protocol. The sniffer should have protected us.
		assert( false );
	}

        if ( IP_protocol == IPPROTO_TCP )
        {
                struct sniff_tcp *tcp = (struct sniff_tcp *)(data + header_len);

                srcPort = ntohs(tcp->th_sport);
                dstPort = ntohs(tcp->th_dport);
        }
        else if ( IP_protocol == IPPROTO_UDP )
        {
                struct sniff_udp *udp = (struct sniff_udp *)(data + header_len);

                srcPort = ntohs(udp->uh_sport);
                dstPort = ntohs(udp->uh_dport);
        }
        else
        {
                srcPort = 0;
                dstPort = 0;
        }

        timeStamp = ts;
}

Packet::Packet( const Packet &orig )
{
	m_src = orig.srcAddr().Clone();
	m_dst = orig.dstAddr().Clone();
	total_len = orig.total_len;
	header_len = orig.header_len;
	IP_protocol = orig.IP_protocol;
        timeStamp = orig.timeStamp;
}

Packet::~Packet()
{
	delete m_src;
	delete m_dst;
}

unsigned int Packet::totalLen() const { return total_len; }
IPAddress & Packet::srcAddr() const { return *m_src; }
IPAddress & Packet::dstAddr() const { return *m_dst; }

std::ostream & operator<<( std::ostream &out, const Packet &ip )
{
	out << "IP: ";
	out << "src=" << ip.srcAddr();
	out << " dst=" << ip.dstAddr();
	out << " len=" << ip.totalLen();
	return out;
}

unsigned int Packet::payloadLen() const
{
	return total_len-header_len;
}

Packet* Packet::newPacket( const u_char *data, unsigned int data_len, util_time_t ts )
{
	return new Packet(data, data_len, ts);
}

