/*
Copyright (C) 2011 COR Entertainment

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

//Account server used for working with Alien Arena and Alien Arena's Statsgen programs

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <malloc.h>
#include <stdio.h>
#include <winsock.h>
#include <time.h>
#else
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define SOCKET int
#define SOCKET_ERROR -1
#define TIMEVAL struct timeval
#define WSAGetLastError() errno
#define strnicmp strncasecmp
#endif

#include "accountserv.h"

#ifndef _DEBUG
#define dprintf printf
#else
#define dprintf printf
#endif

player_t players;

SYSTEMTIME st;

struct sockaddr_in listenaddress;
SOCKET out;
SOCKET listener;
TIMEVAL delay;
#ifdef WIN32
WSADATA ws;
#endif
fd_set set;
char incoming[150000];
int retval;
int timeCount;
int dumpCount;

//this called only when a packet of "logout" is received
//we might also want to timeout a player, so maybe a timestamp of when a player is added would be prudent
void DropPlayer (player_t *player)
{
	//unlink
	if (player->next)
		player->next->prev = player->prev;

	if (player->prev)
		player->prev->next = player->next;

	//free
	free (player);
}

//check list for possible expired players(5 hour window)
void CheckPlayers (void)
{
	player_t	*player = &players;
	int	curTime;

	while (player->next)
	{
		player = player->next;
		
		GetSystemTime(&st);
		curTime = st.wHour;
		if(player->time > curTime)
			curTime += 24;
		if(curTime - player->time > 5)
			DropPlayer(player);
	}
}

//this called only when a packet of "login" is received, and the player is validated
void AddPlayer (char name[32])
{
	player_t	*player = &players;

	//check if this player is already in the list
	while (player->next)
	{
		player = player->next;
		if (!_stricmp(player->name, name))
		{
			return;
		}
	}

	player->next = (player_t *)malloc(sizeof(player_t));

	player->next->prev = player;
	player = player->next;

	//copy name
	strcpy_s(player->name, name);
	player->next = NULL;

	//timestamp
	GetSystemTime(&st);
	player->time = st.wHour;

	dprintf ("[I] player %s added to queue!\n", name);
}

void SendValidationToClient (struct sockaddr_in *from)
{
	int				buflen;
	char			buff[0xFFFF];

	buflen = 0;
	memset (buff, 0, sizeof(buff));

	memcpy (buff, "validated", 9);
	buflen += 9;
	
	if ((sendto (listener, buff, buflen, 0, (struct sockaddr *)from, sizeof(*from))) == SOCKET_ERROR)
	{
		dprintf ("[E] socket error on send! code %d.\n", WSAGetLastError());
	}
}

void ParseResponse (struct sockaddr_in *from, char *data, int dglen)
{
	char *cmd = data;
	char *token;
	char seps[] = "\\";
	char name[32];
	char password[32];

	if (_strnicmp (data, "����login", 9) == 0)
	{		
		//parse string, etc validate or create new profile file
		token = strtok( cmd, seps );

		token = strtok( NULL, seps );
		if(strlen(token) > 32) 
			token[32] = 0; //don't allow overflows from malicious hackers
		strcpy_s(name, token);

		token = strtok( NULL, seps );
		if(strlen(token) > 32) 
			token[32] = 0; //don't allow overflows from malicious hackers
		strcpy_s(password, token);

		printf ("[W] Login command from %s:%s!\n", name, password);

		if(ValidatePlayer(name, password))
		{
			AddPlayer(name);

			//let the client know he was validated
			SendValidationToClient (from);
		}
	}
	else if (_strnicmp (data, "logout", 6) == 0)
	{
		//remove from stack
	}
	else
	{
		printf ("[W] Unknown command %s from %s!\n", cmd, inet_ntoa (from->sin_addr));
	}

}

int main (int argc, char argv[])
{
	int len;
	int fromlen;
	struct sockaddr_in from;
		
	printf ("Alien Arena Account Server 0.01\n(c) 2011 COR Entertainment\n\n");

#ifdef WIN32
	WSAStartup ((WORD)MAKEWORD (1,1), &ws);
#endif

	listener = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	out = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	memset (&listenaddress, 0, sizeof(listenaddress));

	listenaddress.sin_family = AF_INET;
	listenaddress.sin_port = htons(27902);
	listenaddress.sin_addr.s_addr = INADDR_ANY; 

	if ((bind (listener, (struct sockaddr *)&listenaddress, sizeof(listenaddress))) == SOCKET_ERROR)
	{
		printf ("[E] Couldn't bind to port 27902 UDP (something is probably using it)\n");
		return 1;
	}

	delay.tv_sec = 1;
	delay.tv_usec = 0;

	FD_SET (listener, &set);

	fromlen = sizeof(from);

	memset (&players, 0, sizeof(players));

	printf ("listening on port 27902 (UDP)\n");

	timeCount = dumpCount = 0;

	while (1)
	{
		FD_SET (listener, &set);
		delay.tv_sec = 1;
		delay.tv_usec = 0;
				
		retval = select(listener+1, &set, NULL, NULL, &delay);
		if (retval == 1)
		{
			len = recvfrom (listener, incoming, sizeof(incoming), 0, (struct sockaddr *)&from, &fromlen);
			if (len != SOCKET_ERROR)
			{
				if (len > 4)
				{
					//parse this packet
					ParseResponse (&from, incoming, len);
				}
				else
				{
					dprintf ("[W] runt packet from %s:%d\n", inet_ntoa (from.sin_addr), ntohs(from.sin_port));
				}

				//reset for next packet
				memset (incoming, 0, sizeof(incoming));
			} else {
				dprintf ("[E] socket error during select from %s:%d (%d)\n", inet_ntoa (from.sin_addr), ntohs(from.sin_port), WSAGetLastError());
			}
		}

		//add a counter, when reaching a certain amount, write out file of current validated player list
		timeCount++;
		dumpCount++;

		if(dumpCount > 20000) //maybe do something using time instead
		{
			//dump list to file
			dumpCount = 0;

			//check for expired players
			CheckPlayers();
		}
	}
}