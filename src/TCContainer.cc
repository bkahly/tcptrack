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
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "Connection.h"
#include "Collector.h"
#include "TCContainer.h"
#include "SortedIterator.h"
#include "defs.h"
#include "util.h"
//#include "IPv4Packet.h"
#include "TCPTrack.h"
#include "GenericError.h"

extern TCPTrack *app;

TCContainer::TCContainer()
{
	//
	// Start up maintenence thread
	//
	state=TSTATE_IDLE;

	pthread_attr_t attr;

	pthread_mutex_init( &conlist_lock, NULL );
	pthread_mutex_init( &state_mutex, NULL );

	if( pthread_attr_init( &attr ) != 0 )
		throw GenericError("pthread_attr_init() failed");

	pthread_attr_setstacksize( &attr, SS_TCC );

	if( pthread_create(&maint_thread_tid,&attr,maint_thread_func,this) != 0 )
		throw GenericError("pthread_create() failed.");

	state=TSTATE_RUNNING;
	purgeflag=true;
}

// remove closed connections?
void TCContainer::purge( bool npurgeflag )
{
	purgeflag=npurgeflag;
}

// shut down the maintenence thread. prepare to delete this object.
void TCContainer::stop()
{
	pthread_mutex_lock(&state_mutex);
	if( state!=TSTATE_RUNNING )
	{
		pthread_mutex_unlock(&state_mutex);
		return;
	}
	state=TSTATE_STOPPING;
	pthread_mutex_unlock(&state_mutex);

	// maint thread will notice that state is no longer RUNNING and
	// will exit. just wait for it...
	pthread_join(maint_thread_tid,NULL);	

	state=TSTATE_DONE;
}

TCContainer::~TCContainer()
{
	stop();
	for( tcclist::iterator i=conhash2.begin(); i!=conhash2.end(); )
	{
		Connection *rm = (*i);
		tcclist::iterator tmp_i = i;
		i++;
		conhash2.erase(tmp_i);
		collector.collect(rm);
	}
}

SortedIterator * TCContainer::getSortedIteratorPtr()
{
	return new SortedIterator(this);
}

// the sniffer (or PacketBuffer rather) hands us packets via this method.
bool TCContainer::processPacket( Packet &p )
{
	lock();
	bool found = false;

	// iterate over all connections and see if they'll take the packet.
	for( tcclist::const_iterator i = conhash2.begin();
            i!=conhash2.end() && found==false;
            i++ )
	{
		Connection *ic = (*i);
		if( ic->acceptPacket( p ) )
		{
			found=true;
		}
	}

	// is this a new connection?
	if( found==false )
	{
		Connection *newcon = new Connection( p );
		found = true;
		conhash2.push_back(newcon);
	}

	unlock();

	return found;
}

unsigned int TCContainer::numConnections()
{
	return conhash2.size();
}

// the maintenence thread updates statistics and cleans up expired connections
void TCContainer::maint_thread_run()
{
	while( state==TSTATE_RUNNING || state==TSTATE_IDLE )
	{
		uint64_t tmp1 = util_get_current_time() / 1000;
		uint32_t tmp2 = app->refresh_intvl - (tmp1 % app->refresh_intvl);

		struct timespec ts;
		ts.tv_sec=tmp2 / 1000000;
		ts.tv_nsec= (tmp2 % 1000000) * 1000;

		nanosleep(&ts,NULL);

		lock();

		for( tcclist::iterator i=conhash2.begin(); i!=conhash2.end(); )
		{
			Connection *ic=(*i);
				ic->updateCounters();

			// remove closed or stale connections.
			if( purgeflag==true )
			{
				if( ic->getIdleSeconds() > app->remto )
				{
					Connection *rm = ic;
					tcclist::iterator tmp_i = i;
					i++;
					conhash2.erase(tmp_i);
					collector.collect(rm);
				}
				else
					i++;
			}
			else
			{
				i++;
			}
		}

		unlock();		
	}
}


void TCContainer::lock()
{
	// If we can't get the lock in a few seconds, something is wrong.
	struct timespec timeout;
	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 2;

	if ( pthread_mutex_timedlock(&conlist_lock, &timeout) != 0 )
	{
		throw GenericError("pthread_mutex_timedlock() failed");
	}
	// TODO -- replace all pthread_mutex_lock() with this.
}

void TCContainer::unlock()
{
	pthread_mutex_unlock(&conlist_lock);
}


///////////////////

void *maint_thread_func( void * arg )
{
	TCContainer *c = (TCContainer *) arg;
	try
	{
		c->maint_thread_run();
	}
	catch( const AppError &e )
	{
		app->fatal(e.msg());
	}
	return NULL;
}

