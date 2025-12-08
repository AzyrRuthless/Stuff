/*
 *
 * Pipebench
 *
 * By Thomas Habets <thomas@habets.se>
 *
 * Measures the speed of stdin/stdout communication.
 *
 * TODO:
 * -  Variable update time  (now just updates once a second)
 */
/*
 * Copyright (C) 2002 Thomas Habets <thomas@habets.se>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#include <stdio.h>
#include <stdlib.h> /* Added for malloc, free, atoi */
#include <string.h> /* Added for strcpy, strlen */
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>

#ifdef sun
#define u_int8_t uint8_t
#define u_int16_t uint16_t
#define u_int32_t uint32_t
#define u_int64_t uint64_t
#endif

static float version = 0.40;

static volatile int done = 0;

/**
 * sigint
 *
 * Signal handler for SIGINT (Ctrl+C).
 * Sets the global 'done' flag to 1, indicating the program should terminate.
 *
 * @param n The signal number (unused).
 */
static void sigint(int n)
{
	(void)n;
	done = 1;
}

/**
 * unitify
 *
 * Turns a 64-bit integer into a human-readable string with SI or pseudo-SI units.
 * Supports units like k, M, G, T, P, E.
 *
 * @param _in The input number to convert.
 * @param buf The buffer to store the resulting string.
 * @param max The size of the buffer.
 * @param nunit The unit base (e.g., 1000 or 1024).
 * @param dounit Whether to use units (1) or raw numbers (0).
 * @return A pointer to the buffer containing the formatted string.
 */
static char *unitify(u_int64_t _in, char *buf, int max, unsigned long nunit,
		     int dounit)
{
	int e = 0;
	u_int64_t in;
	double inf;
	char *unit = "";
	char *units[] = {
		"",
		"k",
		"M",
		"G",
		"T",
		"P",
		"E",
	};
	int fra = 0;

	inf = in = _in;
	if (dounit) {
		if (in > (nunit*nunit)) {
			e++;
			in/=nunit;
		}
		in *= 100;
		while (in > (100*nunit)) {
			e++;
			in/=nunit;
		}
		/* Ha ha ha ha ha ha, oh my god... Yeah I wish I had the
		 * problem this part fixes.
		 */
		while (e && (e >= (int)(sizeof(units)/sizeof(char*)))) {
		e--;
		in*=nunit;
		}
		unit = units[e];
		inf = in / 100.0;
		fra = 2;
	}
	snprintf(buf, max, "%7.*f %s",fra,inf,unit);
	return buf;
}

/**
 * time_diff
 *
 * Calculates the time difference between two timeval structures and formats it as a string.
 *
 * @param start The start time.
 * @param end The end time.
 * @param buf The buffer to store the resulting string.
 * @param max The size of the buffer.
 * @return A pointer to the buffer containing the formatted string.
 */
static char *time_diff(struct timeval *start, struct timeval *end, char *buf,
		       int max)
{
	struct timeval diff;
	diff.tv_usec = end->tv_usec - start->tv_usec;
	diff.tv_sec = end->tv_sec - start->tv_sec;
	if (diff.tv_usec < 0) {
		diff.tv_usec += 1000000;
		diff.tv_sec--;
	}
	buf[max-1] = 0;
	snprintf(buf,max,"%.2dh%.2dm%.2d.%.2ds",
		 (int)(diff.tv_sec / 3600),
		 (int)((diff.tv_sec / 60) % 60),
		 (int)(diff.tv_sec % 60),
		 (int)(diff.tv_usec/10000));
	return buf;
}

/**
 * usage
 *
 * Prints the usage information for pipebench.
 */
static void usage(void)
{
	printf("Pipebench %1.2f, by Thomas Habets <thomas@habets.se>\n",
	       version);
	printf("usage: ... | pipebench [ -ehqQIoru ] [ -b <bufsize ] "
	       "[ -s <file> | -S <file> ]\\\n           | ...\n");
}

/**
 * main
 *
 * The main entry point of the pipebench utility.
 * Measures and reports the speed of data transfer through stdin/stdout.
 *
 * @param argc The argument count.
 * @param argv The argument vector.
 * @return Returns 0 on success, 1 on failure.
 */
int main(int argc, char **argv)
{
	int c;
	u_int64_t datalen = 0,last_datalen = 0,speed = 0;
	struct timeval start,tv,tv2;
	char tdbuf[64];
	char speedbuf[64];
	char datalenbuf[64];
	unsigned int bufsize = 819200;
	int summary = 1;
	int errout = 0;
	int quiet = 0;
	int fancy = 1;
	int dounit = 1;
	FILE *statusf;
	int statusf_append = 0;
	const char *statusfn = 0;
	int unit = 1024;
	char *buffer;

	statusf = stderr;

	while (EOF != (c = getopt(argc, argv, "ehqQb:ros:S:Iu"))) {
		switch(c) {
		case 'e':
			errout = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'Q':
			quiet = 1;
			summary = 0;
			break;
		case 'o':
			summary = 0;
			break;
		case 'b':
			bufsize = atoi(optarg);
			break;
		case 'h':
			usage();
			return 0;
		case 'r':
			fancy = 0;
			summary = 0;
			break;
		case 's':
			statusfn = optarg;
			statusf_append = 0;
			break;
		case 'S':
			statusfn = optarg;
			statusf_append = 1;
			break;
		case 'I':
			unit = 1000;
			break;
		case 'u':
			dounit = 0;
			break;
		default:
			usage();
			return 1;
		}
	}

	if (statusfn) {
		if (!(statusf = fopen(statusfn, statusf_append?"a":"w"))) {
			perror("pipebench: fopen(status file)");
			if (errout) {
				return 1;
			}
		}
	}

	if ((-1 == gettimeofday(&tv, NULL))
	    || (-1 == gettimeofday(&start, NULL))) {
		perror("pipebench: gettimeofday()");
		if (errout) {
			return 1;
		}
	}

	if ((SIG_ERR == signal(SIGINT, sigint))) {
		perror("pipebench: signal()");
		if (errout) {
			return 1;
		}
	}
	
	while (!(buffer = malloc(bufsize))) {
		perror("pipebench: malloc()");
		bufsize>>=1;
	}

	while (!feof(stdin) && !done) {
		int n;
		char ctimebuf[64];

		if (-1 == (n = fread(buffer, 1, bufsize, stdin))) {
			perror("pipebench: fread()");
			if (errout) {
				return 1;
			}
			continue;
		}
		datalen += n;
		while (-1 == fwrite(buffer, n, 1, stdout)) {
			perror("pipebench: fwrite()");
			if (errout) {
				return 1;
			}
		}
		if (0) {
			fflush(stdout);
		}

		if (-1 == gettimeofday(&tv2,NULL)) {
			perror("pipebench(): gettimeofday()");
			if (errout) {
				return 1;
			}
		}
		strcpy(ctimebuf,ctime(&tv2.tv_sec));
		if ((n=strlen(ctimebuf)) && ctimebuf[n-1] == '\n') {
			ctimebuf[n-1] = 0;
		}
		if (fancy && !quiet) {
			fprintf(statusf, "%s: %sB %sB/second (%s)%c",
				time_diff(&start,&tv2,tdbuf,sizeof(tdbuf)),
				unitify(datalen,datalenbuf,sizeof(datalenbuf),
					unit,dounit),
				unitify(speed,speedbuf,sizeof(speedbuf),
					unit,dounit),
				ctimebuf,
				statusfn?'\n':'\r');
		}
		if (tv.tv_sec != tv2.tv_sec) {
			speed = (datalen - last_datalen);
			last_datalen = datalen;
			if (-1 == gettimeofday(&tv,NULL)) {
				perror("pipebench(): gettimeofday()");
				if (errout) {
					return 1;
				}
			}
			if (!fancy) {
				fprintf(statusf, "%llu\n",speed);
			}
		}
	}
	free(buffer);
	if (summary) {
		float n;

		if (-1 == gettimeofday(&tv,NULL)) {
			perror("pipebench(): gettimeofday()");
			if (errout) {
				return 1;
			}
		}

		n = (tv2.tv_sec - start.tv_sec)
			+ (tv2.tv_usec - start.tv_usec) / 1000000.0;
		fprintf(statusf,"                                     "
			"            "
			"                              "
			"%c"
			"Summary:\nPiped %sB in %s: %sB/second\n",
			statusfn?'\n':'\r',
			unitify(datalen,datalenbuf,sizeof(datalenbuf),
				unit,dounit),
			time_diff(&start,&tv2,tdbuf,sizeof(tdbuf)),
			unitify(n?datalen/n:0,
				speedbuf,sizeof(speedbuf),unit,dounit));
	}
	return 0;
}
