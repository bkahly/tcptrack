#ifndef TCPCAPTURE_H
#define TCPCAPTURE_H 1

#include <sys/time.h>
#include "Packet.h"

/* An TCPCapture is a packet captured off the wire that is known to be
 * an TCP packet
 */
class TCPCapture
{
public:
	TCPCapture( Packet* tcp_packet,
			struct timeval nts );
	TCPCapture( const TCPCapture &orig );
	~TCPCapture();
	Packet & GetPacket() const;
	struct timeval timestamp() const { return m_ts; };
private:
	Packet *m_packet;
	struct timeval m_ts;
};

#endif
