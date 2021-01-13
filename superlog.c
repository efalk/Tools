
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>

#include "libsuperlog.h"

static const char *usage = "Collect output logs from another program\n\n"
"	usage: superlog [options] -- cmd [args]\n\n"
"	-h		this list\n"
"	1, 2, 3, ...	Collect output from specified fds\n"
"	-d N		Allocate N Mb for \"debug\" messages\n"
"	-i N		Allocate N Mb for \"info\" messages\n"
"	-b N		Allocate N Mb for all other messages\n"
"	-v		Also echo messages to stdout in real time\n"
"	-f		Add fd number to messages\n"
"	-t		Add timestamps to messages\n"
"	-c		Color messages by fd\n"
"	-C		Color messages by severity\n"
"	-Ts str		Add trigger; logging stops N events after the trigger\n"
"	-Tn N		Set N (default = 100)\n"
"	-Tc N		Number of times trigger needs to be seen (1)\n"
"	-dpat str	Set pattern that denotes a debug line\n"
"	-ipat str	Set pattern that denotes an info line\n"
"	-wpat str	Set pattern that denotes a warning line\n"
"	-epat str	Set pattern that denotes an error line\n"
"	-x str		Add str to ignore patterns\n"
"	-X file		Read ignore patterns from file, one per line\n"
"	-o file		output to file\n"
"\n"
"By default, allocates 2MB for each class of message.\n"
"By default, collects output on fd 2 (stderr)\n"
"When program exits, logs messages are dumped to stdout (or specified file)\n"
"If superlog receives SIGUSR1, it dumps the logs.\n"
"At present, the color options only work on ANSI terminals\n"
;

#define	NA(a)	(sizeof(a)/sizeof(a[0]))

static const char *dpat = " debug ";
static const char *ipat = " info ";
static const char *wpat = " warning ";
static const char *epat = " error ";

int
main(int argc, char **argv)
{
    int fds[MAX_FDS];
    int nfds = 0;
    const char *ofilename = NULL;
    int dMb = 2;
    int iMb = 2;
    int oMb = 2;
    LogBuffer *debug;
    LogBuffer *info;
    LogBuffer *other;
    int triggerN = 100;
    int triggerC = 1;

    for (++argv; --argc > 0; ++argv)
    {
	if (isdigit(**argv)) {
	    if (nfds < NA(fds)) {
		fds[nfds++] = atoi(*argv);
	    }
	} else if (strcmp(*argv, "-h") == 0) {
	    fputs(usage, stdout);
	    return 0;
	} else if (strcmp(*argv, "-d") == 0 && --argc > 0) {
	    dMb = atoi(*++argv);
	} else if (strcmp(*argv, "-i") == 0 && --argc > 0) {
	    iMb = atoi(*++argv);
	} else if (strcmp(*argv, "-b") == 0 && --argc > 0) {
	    oMb = atoi(*++argv);
	} else if (strcmp(*argv, "-o") == 0 && --argc > 0) {
	    ofilename = *++argv;
	} else if (strcmp(*argv, "-t") == 0) {
	    timestamps = true;
	} else if (strcmp(*argv, "-f") == 0) {
	    showfds = true;
	} else if (strcmp(*argv, "-v") == 0) {
	    verbose = true;
	} else if (strcmp(*argv, "-c") == 0) {
	    showcolor = FDS;
	} else if (strcmp(*argv, "-C") == 0) {
	    showcolor = SEVERITY;
	} else if (strcmp(*argv, "-Ts") == 0 && --argc > 0) {
	    TriggerAdd(*++argv);
	} else if (strcmp(*argv, "-Tn") == 0 && --argc > 0) {
	    triggerN = atoi(*++argv);
	} else if (strcmp(*argv, "-Tc") == 0 && --argc > 0) {
	    triggerC = atoi(*++argv);
	} else if (strcmp(*argv, "-dpat") == 0 && --argc > 0) {
	    dpat = *++argv;
	} else if (strcmp(*argv, "-ipat") == 0 && --argc > 0) {
	    ipat = *++argv;
	} else if (strcmp(*argv, "-wpat") == 0 && --argc > 0) {
	    wpat = *++argv;
	} else if (strcmp(*argv, "-epat") == 0 && --argc > 0) {
	    epat = *++argv;
	} else if (strcmp(*argv, "-x") == 0 && --argc > 0) {
	    ExcludeAdd(*++argv);
	} else if (strcmp(*argv, "-X") == 0 && --argc > 0) {
	    ExcludeAddFile(*++argv);
	} else if (strcmp(*argv, "--") == 0) {
	    ++argv;
	    --argc;
	    break;
	} else {
	    fprintf(stderr, "Unknown argument: %s\n", *argv);
	    fputs(usage, stderr);
	    return 2;
	}
    }

    if (argc < 1) {
	fprintf(stderr, "command is required\n");
	fputs(usage, stderr);
	return 2;
    }

    /* For testing purposes, the user can specify zero for a buffer
     * size, in which case we use 1000 bytes.
     */
    if (dMb > 20 || iMb > 20 || oMb > 20) {
	fprintf(stderr, "One or more buffer sizes out of range\n");
	fputs(usage, stderr);
	return 2;
    }

    debug = LogBufferAlloc(dpat, 'D', dMb);
    info = LogBufferAlloc(ipat, 'I', iMb);
    other = LogBufferAlloc(wpat, 'W', oMb);

    LogBufferAdd(debug);
    LogBufferAdd(info);
    LogBufferAdd(other);

    TriggerParams(triggerC, triggerN);

    return SuperLog(fds, nfds, argv, NULL, ofilename);
}
