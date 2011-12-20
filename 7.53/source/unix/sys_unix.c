/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if defined HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#if defined HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#include "qcommon/qcommon.h"
#include "unix/rw_unix.h"

cvar_t *nostdout;

unsigned	sys_frame_time;

uid_t saved_euid;
qboolean stdin_active = true;

// attachment to statically linked game library
extern void *GetGameAPI ( void *import);


// =======================================================================
// General routines
// =======================================================================

// Added ANSI color-escape output based on/inspired by Warsow. -M
void Sys_ConsoleOutput (char *string)
{
#if defined ANSI_COLOR

	static int  q3ToAnsi[ 8 ] =
	{
		30, // COLOR_BLACK
		31, // COLOR_RED
		32, // COLOR_GREEN
		33, // COLOR_YELLOW
		34, // COLOR_BLUE
		36, // COLOR_CYAN
		35, // COLOR_MAGENTA
		0   // COLOR_WHITE
	};

	if ( nostdout && nostdout->integer )
		return;

    while (*string) {
        if (*string == '^' && string[1]) {
            int colornum = (string[1]-'0')&7;
            printf ("\033[%dm", q3ToAnsi[colornum]);
            string += 2;
            continue;
        }
        if (*string == '\n')
            printf ("\033[0m\n");
        else if (*string == ' ')
            printf ("\033[0m ");
        else
            printf ("%c", *string);
        string++;
    }

#else

    if ( nostdout == NULL || !nostdout->integer )
	{
		fputs( string, stdout );
	}

#endif
}

#if 0
// not currently used in Unix/Linux
void Sys_Printf (const char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];
	unsigned char		*p;

	va_start (argptr,fmt);
	vsnprintf (text,1024,fmt,argptr);
	va_end (argptr);

	if (nostdout && nostdout->value)
        return;

	for (p = (unsigned char *)text; *p; p++) {
		*p &= 0x7f;
		if ((*p > 128 || *p < 32) && *p != 10 && *p != 13 && *p != 9)
			printf("[%02x]", *p);
		else
			putc(*p, stdout);
	}
}
#endif

void Sys_Quit (void)
{
	fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
	CL_Shutdown ();
	Qcommon_Shutdown ();
	exit(0);
}

void Sys_Init(void)
{
}

//void Sys_Error (const char *error, ...)
void Sys_Error (char *error, ...)
{
	va_list     argptr;
	char        string[1024];

// change stdin to non blocking
	fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);

	CL_Shutdown ();
	Qcommon_Shutdown ();

	va_start (argptr,error);
	vsnprintf (string,1024,error,argptr);
	va_end (argptr);
	fprintf(stderr, "Error: %s\n", string);

	exit (1);

}

void Sys_Warn (char *warning, ...)
{
	va_list     argptr;
	char        string[1024];

	va_start (argptr,warning);
	vsnprintf (string,1024,warning,argptr);
	va_end (argptr);
	fprintf(stderr, "Warning: %s", string);
}

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int	Sys_FileTime (char *path)
{
	struct	stat	buf;

	if (stat (path,&buf) == -1)
		return -1;

	return buf.st_mtime;
}

void floating_point_exception_handler(int whatever)
{
//	Sys_Warn("floating point exception\n");
	signal(SIGFPE, floating_point_exception_handler);
}

char *Sys_ConsoleInput(void)
{
    static char text[256];
    int     len;
	fd_set	fdset;
    struct timeval timeout;

	if (!dedicated || !dedicated->value)
		return NULL;

	if (!stdin_active)
		return NULL;

	FD_ZERO(&fdset);
	FD_SET(0, &fdset); // stdin
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	if (select (1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(0, &fdset))
		return NULL;

	len = read (0, text, sizeof(text));
	if (len == 0) { // eof!
		stdin_active = false;
		return NULL;
	}

	if (len < 1)
		return NULL;
	text[len-1] = 0;    // rip off the /n and terminate

	return text;
}

/*****************************************************************************/

static void *game_library = NULL;

/*
=================
Sys_UnloadGame
=================
*/
void Sys_UnloadGame (void)
{
	if ( game_library != NULL )
		dlclose (game_library);
	game_library = NULL;
}

/*
=================
Sys_GetGameAPI

Loads the game module

2010-08 : Implements a statically linked game module. Loading a game module lib
  is supported if it exists.
  To prevent problems with attempting to load an older, incompatible version,
    a lib will not be loaded from arena/ nor data1/
=================
*/
void *Sys_GetGameAPI (void *parms)
{
	void	*(*ptrGetGameAPI) (void *) = NULL;

	FILE	*fp;
	char	name[MAX_OSPATH];
	char	*path;
	size_t  pathlen;
	char	*str_p;
	const char *gamename = "game.so";

	setreuid(getuid(), getuid());
	setegid(getgid());

	if (game_library != NULL)
		Com_Error (ERR_FATAL, "Sys_GetGameAPI without Sys_UnloadingGame");

	// now run through the search paths
	path = NULL;
	while (1)
	{
		path = FS_NextPath (path);
		if (!path)
			break; // Search did not turn up a game shared library

		pathlen = strlen( path );
		// old game lib in data1 is a problem
		if ( !Q_strncasecmp( "data1", &path[ pathlen-5 ], 5 ) )
			continue;
		// may want to have a game lib in arena, but disable for now
		if ( !Q_strncasecmp( "arena", &path[ pathlen-5 ], 5 ) )
			continue;

		snprintf (name, MAX_OSPATH, "%s/%s", path, gamename);

		/* skip it if it just doesn't exist */
		fp = fopen(name, "rb");
		if (fp == NULL)
			continue;
		fclose(fp);

		game_library = dlopen (name, RTLD_NOW);
		if (game_library != NULL )
		{
			Com_Printf("------- Loading %s -------\n", gamename);
			break;
		}
		else
		{
			Com_DPrintf ("LoadLibrary (%s):", name);

			path = dlerror();
			str_p = strchr(path, ':'); // skip the path (already shown)
			if (str_p == NULL)
				str_p = path;
			else
				str_p++;

			Com_DPrintf ("%s\n", str_p);

			break; // file opened but did not dlopen
		}
	}

	if ( game_library != NULL )
	{ // game module from dlopen'd shared library
		ptrGetGameAPI = (void *)dlsym (game_library, "GetGameAPI");
	}

	/*
	 * No game shared library found, use statically linked game
	 */
	if ( ptrGetGameAPI == NULL )
	{
		ptrGetGameAPI = &GetGameAPI;
	}

	if ( ptrGetGameAPI == NULL )
	{ // program error
		Sys_UnloadGame ();
		return NULL;
	}

	return ptrGetGameAPI (parms);
}

/*****************************************************************************/

void Sys_AppActivate (void)
{
}

void HandleEvents(void);

void Sys_SendKeyEvents (void)
{
#ifndef DEDICATED_ONLY
		HandleEvents();
#endif

	// grab frame time
	sys_frame_time = Sys_Milliseconds();
}

/*****************************************************************************/

int main (int argc, char **argv)
{
	int 	time, oldtime, newtime;

	// go back to real user for config loads
	saved_euid = geteuid();
	seteuid(getuid());

	Qcommon_Init(argc, argv);

	fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);

	nostdout = Cvar_Get("nostdout", "0", 0);
	if (!nostdout->value) {
		fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);
	}

    oldtime = Sys_Milliseconds ();
    while (1)
    {
// find time spent rendering last frame
		do {
			newtime = Sys_Milliseconds ();
			time = newtime - oldtime;
		} while (time < 1);
		// curtime setting moved from Sys_Milliseconds()
		//   so it consistent for entire frame
		curtime = newtime;

        Qcommon_Frame (time);
		oldtime = newtime;
    }

}

