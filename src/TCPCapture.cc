#include <typeinfo>
#include <cassert>
#include <typeinfo>
#include <unistd.h>
#include <sys/time.h>
#include "Packet.h"
#include "TCPCapture.h"
#include "util.h"

TCPCapture::TCPCapture( Packet *tcp_packet,
		struct timeval nts )
{
	m_packet = tcp_packet;
	m_ts = nts;
}

TCPCapture::TCPCapture( const TCPCapture & orig )
{
	m_packet = new Packet( *orig.m_packet );
	m_ts = orig.m_ts;
}

TCPCapture::~TCPCapture()
{
	if( m_packet != NULL)
		delete m_packet;
}

Packet & TCPCapture::GetPacket() const
{
	return *m_packet;
}

