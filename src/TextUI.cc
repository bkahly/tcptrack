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
#include "TextUI.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <signal.h>
#include <iostream>
#include "util.h"
#include "defs.h"
#include "TCPTrack.h"
#include "GenericError.h"

extern TCPTrack *app;

TextUI::TextUI( TCContainer *c )
{
	container = c;
	iter=NULL;

	doffset=0;

	state=USTATE_IDLE;

	paused=false;
	sort_type=SORT_UN;

	pthread_mutex_init( &state_mutex, NULL );
}

void TextUI::init()
{
	//
	// Initialize ncurses.
	// TODO: make these exceptions more specific & meaningful.
	//

	w=initscr();
	if( w==NULL )
		throw GenericError("Unable to initialize ncurses display: initscr returned NULL.");

	keypad(w,TRUE);
	int x,y;
	getmaxyx(w,y,x); // this is an ncurses macro

	if( x<69 )
		throw GenericError("tcptrack requires a screen at least 69 columns wide to run.");

	if( y<4 )
		throw GenericError("tcptrack requires a screen at least 4 rows tall to run.");

	size_x=x;
	size_y=y;
	bottom=y;


	//
	// Set up and run the displayer thread.
	//

	run_displayer = true;

	pthread_attr_t attr;
	if( pthread_attr_init( &attr ) != 0 )
		throw GenericError("pthread_attr_init() failed");

	pthread_attr_setstacksize( &attr, SS_TUI );

	if( pthread_create(&displayer_tid,&attr,displayer_thread_func,this) != 0 )
		throw GenericError("pthread_create() returned an error.");


	state=USTATE_RUNNING;
}

void TextUI::stop()
{
	pthread_mutex_lock(&state_mutex);
	if( state != USTATE_RUNNING )
	{
		pthread_mutex_unlock(&state_mutex);
		return;
	}
	state=USTATE_STOPPING;
	pthread_mutex_unlock(&state_mutex);	

	// now that state is set to USTATE_STOPPING,
	// the display draw loop will see this and exit. just wait for it.
	pthread_join(displayer_tid,NULL);

	state=USTATE_DONE;

}

TextUI::~TextUI()
{
	stop();
}

// main drawer thread loop
void TextUI::displayer_run()
{
	// used for select(), to grab keyboard input when there is some.
	fd_set fdset;
	struct timeval tv;
	int rv;
	struct timeval now;

	uint64_t tmp1;
	uint32_t tmp2;

	iter=container->getSortedIteratorPtr();

	while( state==USTATE_RUNNING || state==USTATE_IDLE )
	{
		gettimeofday(&now,NULL);
		tmp1 = now.tv_sec * 1000000 + now.tv_usec;
		tmp2 = app->refresh_intvl - (tmp1 % app->refresh_intvl);

		FD_ZERO(&fdset);
		FD_SET(0,&fdset);
		tv.tv_sec=tmp2 / 1000000;
		tv.tv_usec=tmp2 % 1000000;

		rv=select(1,&fdset,NULL,NULL,&tv);
		if( rv )
		{
			int c = getch();
			if( c==KEY_DOWN )
			{
				++doffset;
				// this is checked for sanity later
			}
			else if( c==KEY_NPAGE )
			{
				doffset += (size_y - 4);
				// this is checked for sanity later
			}
			else if( c==KEY_UP )
			{
				if( doffset>0 )
					--doffset;
			}
			else if( c==KEY_PPAGE )
			{
				if( (int)doffset > (size_y - 4))
					doffset -= (size_y - 4);
				else
					doffset = 0;
			}
			else if( c=='+' )
			{
				app->refresh_intvl /= 2;
				if (app->refresh_intvl < 31250)
					app->refresh_intvl = 31250;
			}
			else if( c=='-' )
			{
				app->refresh_intvl *= 2;
				if (app->refresh_intvl > 32000000)
					app->refresh_intvl = 32000000;
			}
			else if( c=='q' )
			{
				app->shutdown();
			}
			else if( c=='s' )
			{
				switch( sort_type )
				{
					case SORT_UN:
						sort_type=SORT_RATE;
						break;
					case SORT_RATE:
						sort_type=SORT_BYTES;
						break;
					case SORT_BYTES:
						sort_type=SORT_IDLE;
						break;
					case SORT_IDLE:
						sort_type=SORT_UN;
						break;
				}
			}
			else if( c=='p' )
			{
				if( paused==true )
				{
					// going from paused to unpaused
					paused=false;
					container->purge(true);
				}
				else
				{
					// going from unpaused to paused
					paused=true;
					container->purge(false);
				}
			}
		}

		container->lock();

		// check the offset into the container for sanity.
		if( doffset>0 )
		{
			if( container->numConnections()>0 )
			{
				if( doffset >= container->numConnections() )
					doffset = container->numConnections()-1;
			}
			else
			{
				doffset=0;
			}
		}

		// if we aren't reusing an old iterator (ie, if unpaused),
		// get a fresh one.		
		if( iter==NULL )
			iter=container->getSortedIteratorPtr();

		drawui();

		if( paused==false )
		{
			// gonna get a new one next time if not paused...
			// so can this one.
			delete iter;
			iter=NULL;
		}

		container->unlock();


	}
	endwin();
}

void TextUI::drawui()
{
	int row=1;

	int c_client = 1;
	int c_client_l = 21;
	int c_server = c_client + c_client_l + 1;
	int c_server_l = 21;
	int c_state = c_server + c_server_l + 1;
	int c_state_l = 7;
	int c_idle = c_state + c_state_l + 1;
	int c_idle_l = 4;
	int c_act = c_idle + c_idle_l + 1;
	int c_act_l = 1;
	int c_speed = c_act + c_act_l + 1;
	int c_speed_l = 8;
	int c_bytes = c_speed + c_speed_l + 1;
	int c_bytes_l = 8;

	int x,y;
	getmaxyx(w,y,x); // this is an ncurses macro

	if ((x != size_x) || (y != size_y))
	{
		if( x<69 )
		{
			endwin();
			throw GenericError("tcptrack requires a screen at least 69 columns wide to run.");
		}

		if( y<4 )
		{
			endwin();
			throw GenericError("tcptrack requires a screen at least 4 rows tall to run.");
		}

		size_x = x;
		size_y = y;

		bottom = y;
	}

	erase();

	attron(A_REVERSE);
	move(0,0);
	printw("%*.*s", size_x, size_x, " ");

	move(0,c_client);
	printw("Client");
	move(0,c_server);
	printw("Server");
	move(0,c_state);
	printw("%-*.*s", c_state_l, c_state_l, "State");
	move(0,c_idle);
	printw("Idle");
	move(0,c_act);
	printw("A");
	move(0,c_speed);
	printw("Speed");
	if (size_x >= c_bytes + c_bytes_l)
	{
		move(0,c_bytes);
		printw("%-*.*s", c_bytes_l, c_bytes_l, "Bytes");
	}

	attroff(A_REVERSE);



	move(1,0);

	SortedIterator * i=iter;

	i->rewind();

	int Bps_total=0; // the total speed
	int Byt_total=0; // the total bytes
	while( TCPConnection *ic=i->getNext() )
	{
		ic->recalcAvg();
		Bps_total+=ic->getAllBytesPerSecond();
		Byt_total+=ic->getTotalByteCount();
	}

	i->rewind();

	if( sort_type != SORT_UN )
		i->sort( sort_type );

	unsigned int ic_i=0; // for scrolling
	while( TCPConnection *ic=i->getNext() )
	{

		if( row == size_y-2 )
			break;

		++ic_i;
		if( ic_i <= doffset )
			continue;


		if( paused==false &&  (ic->getState() == TCP_STATE_CLOSED || ic->getState() == TCP_STATE_RESET)
				&& time(NULL) - ic->getLastPktTimestamp() > app->remto )
		{
			continue;
		}

		move(row,c_client);
		printw("%-15s %5d", ic->srcAddr().ptr(), ic->srcPort() );
		if( ic->srcAddr().GetType() == 6 )
			row++;
		move(row,c_server);
		printw("%-15s %5d", ic->dstAddr().ptr(), ic->dstPort());
		if( ic->srcAddr().GetType() == 6 )
			row--;

		move(row,c_state);
		printw("             ");
		move(row,c_state);
		if( ic->getState() == TCP_STATE_SYN_SYNACK )
			printw("%-*.*s", c_state_l, c_state_l, "SYN_SNT");
		else if( ic->getState() == TCP_STATE_SYNACK_ACK )
			printw("%-*.*s", c_state_l, c_state_l, "SYNAKAK");
		else if( ic->getState() == TCP_STATE_UP )
			printw("%-*.*s", c_state_l, c_state_l, "ESTABLI");
		else if( ic->getState() == TCP_STATE_FIN_FINACK )
			printw("%-*.*s", c_state_l, c_state_l, "CLOSING");
		else if( ic->getState() == TCP_STATE_CLOSED )
			printw("%-*.*s", c_state_l, c_state_l, "CLOSED");
		else if( ic->getState() == TCP_STATE_RESET )
			printw("%-*.*s", c_state_l, c_state_l, "RESET");

		move(row,c_idle);
		if( ic->getIdleSeconds() < 60 )
			printw("%2ds",(int)ic->getIdleSeconds());
		else if( ic->getIdleSeconds() < 3600 )
			printw("%2dm",(int)(ic->getIdleSeconds()/60));
		else
			printw("%2dh",(int)(ic->getIdleSeconds()/3600));

		move(row,c_act);
		if( ic->activityToggle() )
			printw("*");
		else
			printw(" ");

		move(row,c_speed);
		unsigned int Bps = ic->getAllBytesPerSecond();
		print_bps(Bps);

		if (size_x >= c_bytes + c_bytes_l)
		{
			move(row,c_bytes);
			unsigned int Bytes = ic->getTotalByteCount();
			print_bps(Bytes);
		}

		if( ic->srcAddr().GetType() == 6 )
			row++;
		row++;
	}

	attron(A_REVERSE);

	move(bottom-2,0);
	printw("%*.*s", size_x, size_x, " ");

	move(bottom-2,1);
	printw("Refresh %5.3f sec", app->refresh_intvl/1000000.0);

	move(bottom-2,c_speed-6);
	printw("TOTAL");
	move(bottom-2,c_speed);
	print_bps(Bps_total);
	if (size_x >= c_bytes + c_bytes_l)
	{
		move(bottom-2,c_bytes);
		print_bps(Byt_total);
	}

	move(bottom-1,0);
	printw("%*.*s", size_x, size_x, " ");

	move(bottom-1,1);
	if( container->numConnections() > 0 )
		printw("Connections %d-%d of %d",doffset+1,ic_i,container->numConnections());
	else
		printw("Connections 0-0 of 0");

	move(bottom-1,46);
	if( paused==true )
	{
		attron(A_UNDERLINE);
		printw("P");
		attroff(A_UNDERLINE);
		printw("aused");
	}
	else
	{
		printw("Un");
		attron(A_UNDERLINE);
		printw("p");
		attroff(A_UNDERLINE);
		printw("aused");
	}


	move(bottom-1,56);
	switch( sort_type )
	{
		case SORT_UN:
			printw("Un");
			attron(A_UNDERLINE);
			printw("s");
			attroff(A_UNDERLINE);
			printw("orted");
			break;
		case SORT_RATE:
			attron(A_UNDERLINE);
			printw("S");
			attroff(A_UNDERLINE);
			printw("orted by rate");
			break;
		case SORT_BYTES:
			attron(A_UNDERLINE);
			printw("S");
			attroff(A_UNDERLINE);
			printw("orted by bytes");
			break;
		case SORT_IDLE:
			attron(A_UNDERLINE);
			printw("S");
			attroff(A_UNDERLINE);
			printw("orted by idle");
			break;
	}

	attroff(A_REVERSE);
	move(size_y-1, size_x-1);
	refresh();
}

// display the speed with the right format
void TextUI::print_bps(unsigned int Bps)
{
	if( Bps < 1000 )
		printw(" %4d  B",Bps);
	else if(Bps < 1024*10 )
		printw(" %4.2f kB",Bps/1024.0);
	else if(Bps < 1024*100 )
		printw(" %4.1f kB",Bps/1024.0);
	else if(Bps < 1024*1000 )
		printw(" %4.0f kB",Bps/1024.0);
	else if(Bps < 1024*1024*10 )
		printw("%4.2f  MB",Bps/(1024*1024.0));
	else if(Bps < 1024*1024*100 )
		printw("%4.1f  MB",Bps/(1024*1024.0));
	else if(Bps < 1024*1024*1000 )
		printw("%4.0f  MB",Bps/(1024*1024.0));
	else
		printw("%4.2f  GB",Bps/(1024*1024*1024.0));
}

// reset the terminal. used only for unclean exits.
void TextUI::reset()
{
	endwin();
}

/////////

void *displayer_thread_func( void *arg )
{
	TextUI *ui = (TextUI *) arg;
	try
	{
		ui->displayer_run();
	}
	catch( const AppError &e )
	{
		app->fatal(e.msg());
	}
	return NULL;
}
