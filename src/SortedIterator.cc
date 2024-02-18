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
#define _REENTRANT
#include <stdlib.h>
#include <string.h>
#include <list>
#include "SortedIterator.h"
#include "TCContainer.h"

SortedIterator::SortedIterator( TCContainer *c ) 
{
	numcons = c->numConnections();
	cons = (Connection **) malloc(numcons*sizeof(Connection *));

	// fill up the array with pointers to the connections
	int index=0;
	for( tcclist::iterator i=c->conhash2.begin(); i!=c->conhash2.end(); i++ )
	{
		cons[index]=(*i);
		++index;
	}

	cur=0;
}

// this method does the actual work of sorting
// it should be called from a thread that can afford to do that work.
void SortedIterator::sort( int sort_type )
{
	if( numcons > 1 ) {
                qsort(cons, numcons, sizeof(Connection *), compare);
	}
}

// get the next connection and advance our current location.
// returns NULL if there are no more connections.
Connection * SortedIterator::getNext()
{
	if( cur >= numcons ) 
		return NULL;
	else
		return cons[cur++];
}

void SortedIterator::rewind()
{
	cur=0;
}

SortedIterator::~SortedIterator()
{
	free(cons); 
}

/////////////////////////////////////////////

// this is a callback function used from qsort in sort(). 
// it performs the comparison of the Connection objects
int compare(const void *c1, const void *c2)
{
	Connection * con1;
	Connection * con2;

	con1=* (Connection **) c1;
	con2=* (Connection **) c2;

        int cmp;

        cmp = strcmp(con1->srcHost, con2->srcHost);
	if( cmp != 0 )
        {
		return cmp;
        }
	else
	{
                return compare_rate(c1, c2);
	}
}

// this is a callback function used from qsort in sort(). 
// it performs the comparison of the Connection objects based on current throughput.
int compare_rate(const void *c1, const void *c2)
{
	Connection * con1;
	Connection * con2;

	con1=* (Connection **) c1;
	con2=* (Connection **) c2;


	if( con1->getAllBytesPerSecond() > con2->getAllBytesPerSecond() )
		return -1;
	else if( con1->getAllBytesPerSecond() < con2->getAllBytesPerSecond() )
		return 1;
	else
	{
		if( con1->getIdleTimeNS() < con2->getIdleTimeNS() )
			return -1;
		else if( con1->getIdleTimeNS() > con2->getIdleTimeNS() )
			return 1;
		else
			return 0;
	}
}

// this is a callback function used from qsort in sort(). 
// it performs the comparison of the Connection objects based on total byte count.
int compare_bytes(const void *c1, const void *c2)
{
	Connection * con1;
	Connection * con2;

	con1=* (Connection **) c1;
	con2=* (Connection **) c2;


	if( con1->getTotalByteCount() > con2->getTotalByteCount() )
		return -1;
	else if( con1->getTotalByteCount() < con2->getTotalByteCount() )
		return 1;
	else
	{
		if( con1->getIdleTimeNS() < con2->getIdleTimeNS() )
			return -1;
		else if( con1->getIdleTimeNS() > con2->getIdleTimeNS() )
			return 1;
		else
			return 0;
	}
}

// this is a callback function used from qsort in sort().
// it performs the comparison of the Connection objects based on the idle time.
int compare_idle(const void *c1, const void *c2)
{
	Connection * con1;
	Connection * con2;

	con1=* (Connection **) c1;
	con2=* (Connection **) c2;

	if( con1->getIdleTimeNS() < con2->getIdleTimeNS() )
		return 1;
	else if( con1->getIdleTimeNS() > con2->getIdleTimeNS() )
		return -1;
	else
		return 0;
}

// this is a callback function used from qsort in sort().
// it performs the comparison of the Connection objects based on the inverse of idle time.
int compare_active(const void *c1, const void *c2)
{
	Connection * con1;
	Connection * con2;

	con1=* (Connection **) c1;
	con2=* (Connection **) c2;

	if( con1->getIdleTimeNS() < con2->getIdleTimeNS() )
		return -1;
	else if( con1->getIdleTimeNS() > con2->getIdleTimeNS() )
		return 1;
	else
		return 0;
}

// this is a callback function used from qsort in sort().
// it performs the comparison of the Connection objects based on the first time.
int compare_first(const void *c1, const void *c2)
{
	Connection * con1;
	Connection * con2;

	con1=* (Connection **) c1;
	con2=* (Connection **) c2;

	if( con1->getFirstPktTimestamp() < con2->getFirstPktTimestamp() )
		return 1;
	else if( con1->getFirstPktTimestamp() > con2->getFirstPktTimestamp() )
		return -1;
	else
		return 0;
}
