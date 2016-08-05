/*
	Daytime Service written to accomodate legacy client insfrastructure on private networks.
	Socket binds to all ip addresses on host (0.0.0.0) port 13. 

	Loren Card, loren.card@gmail.com August 2016
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#if defined(_WIN32)
#pragma comment(lib, "Ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#else
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define PORT			13
#define NB_CONNECTION	500
#define DAYTIME_MSG_LEN	51

static int server_socket = -1;

static void 
close_sigint(int dummy)
{
	if (server_socket != -1) {
		close(server_socket);
	}

	exit(dummy);
}

#if defined(_WIN32)
static int 
tcp_init_win32(void)
{
	/* Initialise Windows Socket API */
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		fprintf(stderr, "WSAStartup() returned error code %d\n",
			(unsigned int)GetLastError());
		errno = EIO;
		return -1;
	}
	return 0;
}
#endif

static SOCKET 
tcp_listen(int port, int nb_connection)
{
	SOCKET new_sock;
	int enable;
	struct sockaddr_in addr;
		
#if defined(_WIN32)
	if (tcp_init_win32() == -1) {
		return -1;
	}
#endif

	new_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (new_sock == -1) {
		return -1;
	}

	enable = 1;
	if (setsockopt(new_sock, SOL_SOCKET, SO_REUSEADDR,
		(char *)&enable, sizeof(enable)) == -1) {
		close(new_sock);
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	/* If the modbus port is < to 1024, we need the setuid root. */
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	/* Listening only on specified IP address would need below: */
	//	addr.sin_addr.s_addr = inet_addr(ip);
	
	if (bind(new_sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		close(new_sock);
		return -1;
	}

	if (listen(new_sock, nb_connection) == -1) {
		close(new_sock);
		return -1;
	}

	return new_sock;
}

/*
 * JJJJJ is the Modified Julian Date (MJD). The MJD has a starting point of midnight on November 17, 1858.
 */
int
MJD(const struct tm * t) {

	int mjd = 365 * (1900 + t->tm_year - 1858)	// non-leap days since 1858 and Jan 1 this year
		+ 36							// leap days between 1858 and 2000
		+ ((t->tm_year - 100) / 4)		// leap days since 2000.
		+ t->tm_yday					// days this year
		- 322;							// days in 1858 up to November 17	

	/*
	* May have screwed up by a day here.
	* Would like to confirm correct against NIST server in leap year
	* prior to leap day.
	*/

	return mjd;
}

/*
* TT is a two digit code(00 to 99) that indicates whether the United States is on Standard Time(ST) or Daylight Saving Time(DST).
* It also indicates when ST or DST is approaching. This code is set to 00 when ST is in effect, or to 50 when DST is in effect.
* During the month in which the time change actually occurs, this number will decrement every day until the change occurs.
* For example, during the month of November, the U.S.changes from DST to ST.
* On November 1, the number will change from 50 to the actual number of days until the time change.
* It will decrement by 1 every day until the change occurs at 2 a.m.local time when the value is 1.
* Likewise, the spring change is at 2 a.m.local time when the value reaches 51.
*	TT = 0 means standard time is currently in effect.
*	TT = 50 means daylight saving time is currently in effect.
*	TT = 51 means the transition from standard time to daylight time is at 2am local time today.
*	TT = 1 means the transition from daylight time to standard time is at 2am local time today.
*	TT > 51 gives advance notice of the number of days to the transition to daylight time.
*		The TT parameter is decremented at 00:00 every day during this advance notice period,
*		and the transition will occur when the parameter reaches 51 as discussed above.
*	1 < TT < 50 gives advance notice of the number of days to the transition to standard time.
*		The DST parameter is decremented at 00 : 00 every day during this advance notice period,
*		and the transition will occur when the parameter reaches 1 as discussed above.
*
* What are the current rules for daylight saving time ?
*
* The rules for DST changed in 2007 for the first time in more than 20 years.
* The new changes were enacted by the Energy Policy Act of 2005, which extended the length of DST
* in the interest of reducing energy consumption. The rules increased the duration of DST by about one month.
* DST is now in effect for 238 days, or about 65 % of the year, although Congress retained the right
* to revert to the prior law should the change prove unpopular or if energy savings are not significant.
* At present, daylight saving time in the United States
*
* begins at 2:00 a.m. on the second Sunday of March and
* ends at 2:00 a.m. on the first Sunday of November
*/

int
TT(const struct tm * t) {

	int tt = 0;

	/* December, January, February */
	if ((t->tm_mon == 11) || ((t->tm_mon >= 0) && (t->tm_mon < 2))) {
		tt = 0;
	}

	/* March - DST begins at 2:00 a.m. on the second Sunday */
	if (t->tm_mon == 2) {

		/* Are we pre-transition? Day of? Post-transition? */
		int sunday_count = 0;
		int wday = t->tm_wday;
		for (int mday = t->tm_mday; mday > 0; mday--) {
			if (wday == 0) sunday_count++;
			wday--;
			wday %= 7;
		}

		if (sunday_count <= 1) { // pre transistion

			switch (t->tm_wday) {
			case 0:		// Sunday. Must be first Sunday.
				tt = 51;
				break;
			case 1:		// Monday
				tt = 57;
				break;
			case 2:		// Tuesday
				tt = 56;
				break;
			case 3:		// Wednesday
				tt = 55;
				break;
			case 4:		//Thursday
				tt = 54;
				break;
			case 5:		// Friday
				tt = 53;
				break;
			case 6:		// Saturday
				tt = 52;
				break;
			default:	// WTF?
				fprintf(stderr, "gmtime() is a lie.\n");
			}

			if (sunday_count == 0) {
				tt += 7;
			}
		}
		else if ((sunday_count == 2) && (t->tm_wday == 0)) {
			//fprintf(stderr, "Today is transition day.\n");
			tt = 51;
		}
		else { tt = 50; } // post transition. DST in effect 		
	}

	/* April - October */
	if ((t->tm_mon >= 3) && (t->tm_mon < 10)) {
		tt = 50;
	}

	/* November - ends at 2:00 a.m. on the first Sunday */
	if (t->tm_mon == 10) {

		/* Are we pre-transition? Day of? Post-transition? */
		int sunday_count = 0;
		int wday = t->tm_wday;
		for (int mday = t->tm_mday; mday > 0; mday--) {
			if (wday == 0) sunday_count++;
			wday--;
			wday %= 7;
		}

		if (sunday_count == 0) { // pre transistion
			switch (t->tm_wday) {
			case 0:		// Sunday. Can't happen.
				tt = 1;
				fprintf(stderr, "TT() routine is bad and I feel bad.\n");
				break;
			case 1:		// Monday
				tt = 7;
				break;
			case 2:		// Tuesday
				tt = 6;
				break;
			case 3:		// Wednesday
				tt = 5;
				break;
			case 4:		//Thursday
				tt = 4;
				break;
			case 5:		// Friday
				tt = 3;
				break;
			case 6:		// Saturday
				tt = 2;
				break;
			default:	// WTF?
				fprintf(stderr, "gmtime() is a lie.\n");
			}
		}
		else if ((sunday_count == 1) && (t->tm_wday == 0)) {
			//fprintf(stderr, "Today is transition day.\n");
			tt = 1;
		}
		else { tt = 0; } // post transition. ST in effect 		
	}

	return(tt);
}

void
update_daytime_msg(char * daytime_msg)
{
	char time_str[DAYTIME_MSG_LEN+1];
	time_t rawtime;
	struct tm *info;

	time(&rawtime);
	info = gmtime(&rawtime);
		
	strftime(time_str, DAYTIME_MSG_LEN+1, "%y-%m-%d %H:%M:%S", info);
	sprintf_s(daytime_msg, DAYTIME_MSG_LEN+1, "\n%i %s %02i 0 0  00.0 UTC(NIST) * \n", MJD(info), time_str, TT(info));
}

int
main(int argc, char ** argv) 
{
	SOCKET sock;
	fd_set active_fd_set, read_fd_set;
	struct sockaddr_in clientname;
	int size;
	int fdmax;
	int sent_size;
	char daytime_msg[DAYTIME_MSG_LEN + 1];

	fprintf(stderr, "Daytime Service (Loren Card, loren.card@gmail.com, August 2016)\n");

	sock = tcp_listen(PORT, NB_CONNECTION);
	if (sock < 0) {
		fprintf(stderr, "Could not bind to socket.\n");
		exit(EXIT_FAILURE);
	}

	/* Initialize the set of active sockets. */
	FD_ZERO(&active_fd_set);
	FD_SET(sock, &active_fd_set);
	fdmax = sock;
	
	while (1)
	{
		/* Block until input arrives on one or more active sockets. */
		read_fd_set = active_fd_set;
		if (select(fdmax+1, &read_fd_set, NULL, NULL, NULL) < 0)
		{
			perror("select() failure.");
			exit(EXIT_FAILURE);
		}

		/* Connection request on original socket. */
		SOCKET new;
		size = sizeof(clientname);
		new = accept(sock, (struct sockaddr *) &clientname,	&size);
		if (new < 0)
		{
			perror("accept() failure.");
			exit(EXIT_FAILURE);
		}
		fprintf(stderr,
			"Connection from host %s, port %hu.\n",
			inet_ntoa(clientname.sin_addr),
			ntohs(clientname.sin_port));
		fflush(stderr);

		/* Write back and close. */
		update_daytime_msg(daytime_msg);

		sent_size = send(new, daytime_msg, DAYTIME_MSG_LEN, 0);
		if (DAYTIME_MSG_LEN != sent_size) {
			fprintf(stderr, "Sent %i instead of %i characters in replay.\n", sent_size, DAYTIME_MSG_LEN);
		}
		if (shutdown(new, SD_BOTH)) {
			fprintf(stderr, "socket error on shutdown().\n");
		}
		close(new);
	}

	/* We run forever. Any exit is a failure. */
	close(sock);
	return(EXIT_FAILURE);
}
