#ifndef PACKET_H
#define PACKET_H 1
#define __FAVOR_BSD 1

#include <netinet/in.h> // needed 
#include "util.h"
#include "IPAddress.h"

class Packet
{
public:
	/* stuff to do in constructor:
	 *  ensure data_len >= ip header len field
	 *  ensure version is 4
	 *  ensure total len >= ip header len
	 *  verify checksum
	 */
	Packet( const u_char *data, unsigned int data_len, util_time_t ts );
	Packet( const Packet &orig );
	~Packet();
	unsigned int totalLen() const;
	unsigned long len() const { return total_len; };
	unsigned int payloadLen() const;
	IPAddress & srcAddr() const;
	IPAddress & dstAddr() const;
	static Packet * newPacket( const u_char *data, unsigned int data_len, util_time_t ts );

	unsigned short IP_protocol;
        unsigned int srcPort;
        unsigned int dstPort;
        util_time_t timeStamp;

private:
	unsigned int total_len;
	unsigned short header_len;

	// these are pointers because the IPAddress class is not modifiable
	// after initialization. The constructor of this class can not 
	// immediately set them.
	IPAddress *m_src;
	IPAddress *m_dst;
};


#endif
