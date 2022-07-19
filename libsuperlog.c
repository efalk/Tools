
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include "libsuperlog.h"
#include <sys/select.h>

static bool superlog_enabled = false;
static int log_fd = -1;
static FILE *ofile = NULL;

#pragma mark -- client utilities --

/**
 * Enable logging to specified file descriptor. fd must already
 * be open for output. The dup2() system call can be helpful.
 */
void
superlogInit(int fd)
{
	/* See if fd is open for output */
	if (fcntl(fd, F_GETFD) < 0) {
		superlog_enabled = false;
		fprintf(stderr, "fd %d not open, superlog not enabled\n", fd);
		return;
	}
	log_fd = fd;
	ofile = fdopen(fd, "w");
	fprintf(ofile, "Superlog output begins\n");
	fflush(ofile);
	superlog_enabled = true;
}

/**
 * Generate output to superlog fd. Arguments are the same as for printf()
 */
void
superlog(const char *fmt, ...)
{
	va_list ap;

	if (!superlog_enabled) return;

	va_start(ap, fmt);
	vfprintf(ofile, fmt, ap);
	fflush(ofile);
	va_end(ap);
}

/**
 * Generate output to superlog fd. Arguments are the same as for vprintf()
 */
void
vsuperlog(const char *fmt, va_list ap)
{
	if (!superlog_enabled) return;

	vfprintf(ofile, fmt, ap);
	fflush(ofile);
}

/**
 * Cause a log dump in the parent
 */
void
superlogDump()
{
	kill(getppid(), SIGUSR1);
}


#pragma mark -- superlog --


/* Definitions, typedefs, forward references, globals, macros */

#define	MAX_BUFFERS	8
#define	MAX_TRIGGERS 20

bool timestamps = false;
bool showfds = false;
bool verbose = false;
enum colorize showcolor = NONE;

struct LogMsg {
    struct LogMsg *next;
    long seq;
    time_t time;
    short linelen;
    short fd;
    char type;
    char line[1];
};

struct  LogBuffer {
    long limit;		/* Max allowed */
    const char *pat;	/* Pattern for logs in this buffer */
    LogMsg *first;	/* Start of linked list of messages */
    LogMsg **last;	/* End of the list */
    LogMsg *end;	/* Last one written */
    long allocated;	/* How much space consumed so far */
    bool full;		/* No more allocations */
    char type;
    LogMsg *iter;	/* Iterator */
};


typedef struct nbfile NBFile;

static int numTrigger = 0;

static void child(int fds[MAX_FDS], int pfds[MAX_FDS][2], int nfds,
  char **args, int argc, int (*func)(int argc, char **argv));
static const char *timeStr(time_t t);
static void LogBufferInit(LogBuffer *lb);
static LogBuffer * classify(const char *line);
static void LogBufferIterator(LogBuffer *lb);
static LogMsg *LogBufferNext(LogBuffer *lb);
static LogMsg * lmAlloc(size_t len);
static const char * colorStart(char type, int fd);
static const char * colorStop();
static void nonBlocking(int fd);
static NBFile * NBFileOpen(int fd);
static char *NBFileRead(NBFile *file);

#define	NA(a)	(sizeof(a)/sizeof(a[0]))


/**
 * This is the usual main entry point to superlog.
 *
 * @param fds - array of child output file descriptors to read from
 * @param nfds - number of file descriptors in fds
 * @param argv - arguments (NULL-terminated) to pass to child
 * @param func - main() function to call, or NULL
 * @param ofilename - name of file to receive logs, or NULL for stdout
 *
 * If func() is not specified, this function will execute the program
 * specified in argv[0]. If func() is specified, this function will fork
 * and then execute func(argc, argv)
 */
int
SuperLog(
    int *fds,
    int nfds,
    char **argv,
    int (*func)(int argc, char **argv),
    const char *ofilename
)
{
    int pfds[MAX_FDS][2];
    int ifds[MAX_FDS];
    int i;
    int pid;
    int argc;
    char **tmp;

    ofile = stdout;

    if (ofilename != NULL) {
	if ((ofile = fopen(ofilename, "w")) == NULL) {
	    perror(ofilename);
	    return 4;
	}
    }

    for (argc=0, tmp=argv; *tmp != NULL; ++tmp, ++argc);

    if (nfds > MAX_FDS) {
	fprintf(stderr, "Limit of %d output fds, extras ignored\n", MAX_FDS);
	nfds = MAX_FDS;
    }

    /* Set up the pipes */
    for (i=0; i<nfds; ++i) {
	if (pipe(pfds[i]) < 0) {
	    perror("pipe");
	    return 3;
	}
	ifds[i] = pfds[i][0];
    }

    if ((pid = fork()) < 0) {
	perror("fork");
	return 3;
    }

    if (pid == 0) {
	child(fds, pfds, nfds, argv, argc, func);
	fprintf(stderr, "exec failed\n");
	_exit(3);
    }

    /* Parent */
    LogParent(fds, ifds, nfds);
    printf("Finished, dumping logs\n");
    LogDump();

    return 0;
}


#pragma mark -- Child process --

static int
inList(int fd, int pfds[MAX_FDS][2], int nfds)
{
    int i;
    for (i=0; i<nfds; ++i)
	if (pfds[i][1] == fd)
	    return i;
    return -1;
}

/**
 * This is the child process. Exec the command.
 */
static void
child(int fds[MAX_FDS], int pfds[MAX_FDS][2], int nfds, char **args, int argc,
    int (*func)(int argc, char **argv))
{
    int i, j;
    int tmpfd;

    /* Don't need the input halves of the pipes */
    for (i=0; i<nfds; ++i) {
	close(pfds[i][0]);
    }

    /* Dupe the output halves to the designated fds */
    /* This is non-trivial since there might be collisions */
    for (i=0; i<nfds; ++i) {
	if (pfds[i][1] == fds[i]) {	/* happy coincidence */
	    continue;
	}
	if ((j = inList(fds[i], pfds, nfds)) >= 0) {
	    /* Collision */
	    tmpfd = dup(pfds[j][1]);
	    close(pfds[j][1]);
	    pfds[j][1] = tmpfd;
	}
	if (dup2(pfds[i][1], fds[i]) < 0) {
	    perror("dup2");
	    _exit(3);
	}
	close(pfds[i][1]);
	pfds[i][1] = -1;
    }

    if (func != NULL)
	func(argc, args);
    else
	execvp(args[0], args);
}


#pragma mark -- Parent process --

static int signalPipe[2];

static void
sigfunc(int signal)
{
    char val = signal & 0xff;
    write(signalPipe[1], &val, 1);
}

/**
 * After the fork, this function is called in the parent process.
 * This function watches for input on any of the fds in pfds[*][0],
 * classifies it and logs it.
 * @param ofds  list of fds the child will be writing to
 * @param ifds  list of fds the parent will be reading from.
 */
void
LogParent(int ofds[MAX_FDS], int ifds[MAX_FDS], int nfds)
{
    LogBuffer *lb;
    int i, j, fd;
    fd_set readfds;
    int maxfd;
    int signalfd;
    char *line;
    NBFile *files[MAX_FDS];
    long seq = 0;
    bool triggered = false;

    fprintf(stderr, "Begin monitoring, superlog pid = %d\n", getpid());

    /* Open the ifds as NBFile objects. */
    maxfd = 0;
    for (i=0; i<nfds; ++i)
    {
	fd = ifds[i];
	if (fd > maxfd) maxfd = fd;
	nonBlocking(fd);
	files[i] = NBFileOpen(fd);
    }

    /* Signals we care about */
    signal(SIGCHLD, sigfunc);
    signal(SIGUSR1, sigfunc);
    signal(SIGINT, sigfunc);
    signal(SIGTERM, sigfunc);
    pipe(signalPipe);
    nonBlocking(signalPipe[0]);
    nonBlocking(signalPipe[1]);
    signalfd = signalPipe[0];
    if (signalfd > maxfd) maxfd = signalfd;

    maxfd++;

    /* And now the main loop */
    for(;;)
    {
	FD_ZERO(&readfds);
	FD_SET(signalfd, &readfds);
	for (i=0; i<nfds; ++i) {
	    FD_SET(ifds[i], &readfds);
	}
	j = select(maxfd, &readfds, NULL, NULL, NULL);
	if (j < 0) {
	    if (errno == EINTR)		/* Let it go, sigfunc will get it */
		continue;
	    perror("select");
	    break;
	}
	if (FD_ISSET(signalfd, &readfds))
	{
	    char signum;
	    while (read(signalfd, &signum, 1) == 1) {
		switch (signum) {
		  case SIGCHLD:
		    printf("Child process has exited\n");
		    return;
		  case SIGUSR1:
		    printf("Sigusr1, dumping logs\n");
		    LogDump();
		    break;
		  case SIGINT:
		  case SIGTERM:
		    printf("Caught signal, exiting\n");
		    return;
		}
	    }
	}
	for (i=0; i<nfds; ++i) {
	    fd = ifds[i];
	    if (FD_ISSET(fd, &readfds)) {
		while ((line = NBFileRead(files[i])) != NULL) {
		    lb = classify(line);
		    if (verbose) {
			printf("%s%s%s\n",
			    colorStart(lb->type, fd), line, colorStop());
		    }
		    if (ExcludeTest(line)) {
			continue;
		    }
		    if (triggered) {
			continue;
		    }
		    if (numTrigger > 0 && TriggerCheck(line)) {
			triggered = true;
			fprintf(stderr, "Triggered, dumping logs\n");
			LogDump();
			continue;
		    }
		    LogBufferAppend(lb, ++seq, line, ofds[i]);
		}
	    }
	}
    }
}

/**
 * Make a file descriptor non-blocking.
 */
static void
nonBlocking(int fd)
{
    int flags;
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static const char *
timeStr(time_t t)
{
    static char buffer[30];
    struct tm *tm = localtime(&t);
    strftime(buffer, sizeof(buffer), "%F %T ", tm);
    return buffer;
}



#pragma mark -- Logging --

static LogBuffer *logbuffers[MAX_BUFFERS];
static int nLogBuffer = 0;

#if 0
void
AddToLog(const char *line, int fd, long seq, char type)
{
    int i;
    if (nLogBuffer <= 0) return;

    for (i=0; i<nLogBuffer; ++i)
	if (logbuffers[i]->type == type) {
	    LogBufferAppend(logbuffers[i], seq, line, fd, type);
	    return;
	}
    /* No match, use the last one as the "catch-all" */
    LogBufferAppend(logbuffers[nLogBuffer-1], seq, line, fd, type);
}
#endif

static inline bool
haveMsg(LogMsg **msgs, int n)
{
    for (;--n >= 0; ++msgs)
	if (*msgs != NULL) return true;
    return false;
}

static inline int
oldestMsg(LogMsg **msgs, int n)
{
    long seq = 0;
    int i, oldest = -1;
    for (i=0; i<n; ++i, ++msgs) {
	if (*msgs != NULL && (oldest < 0 || (*msgs)->seq < seq))
	    oldest = i;
    }
    return oldest;
}

/**
 * Dump logs and clear them
 * Log collection continues. Normally called from LogParent() when
 * the child exits, a trigger string is seen in the logs, or SIGUSR1 received.
 */
void
LogDump()
{
    /* Go through each buffer, selecting the oldest entry from each,
     * until they're all exhausted.
     */
    LogMsg *msgs[MAX_BUFFERS];
    int i;

    fprintf(ofile, "\nLog dump at %s\n\n", timeStr(time(NULL)));

    for (i=0; i<nLogBuffer; ++i) {
	LogBufferIterator(logbuffers[i]);
	msgs[i] = LogBufferNext(logbuffers[i]);
    }

    while (haveMsg(msgs, nLogBuffer))
    {
	LogMsg *lm = NULL;
	i = oldestMsg(msgs, nLogBuffer);
	lm = msgs[i];
	msgs[i] = LogBufferNext(logbuffers[i]);
	fputs(colorStart(lm->type, lm->fd), ofile);
	if (showfds) fprintf(ofile, "%d ", lm->fd);
	if (timestamps) fputs(timeStr(lm->time), ofile);
	fputs(lm->line, ofile);
	fputs(colorStop(lm->type, lm->fd), ofile);
	fputc('\n', ofile);
    }
    fflush(ofile);

    for (i=0; i<nLogBuffer; ++i) {
	LogBufferClear(logbuffers[i]);
    }
}

static LogBuffer *
classify(const char *line)
{
    LogBuffer **lb = logbuffers;
    int i;
    for (i=0; i < nLogBuffer; ++i, ++lb)
	if ((*lb)->pat == NULL || strstr(line, (*lb)->pat) != NULL)
	    return *lb;
    /* Fell through, use the last one as a "catch-all" */
    return logbuffers[nLogBuffer-1];
}

#pragma mark -- LogBuffer management --

/**
 * Create one circular buffer for holding logs
 * @param pat - string, which when seen in a log message, causes it
 * to be added to this buffer
 * @param type - single character name for this buffer, e.g. 'D', 'I', 'W', 'E'
 * @param limit — max buffer size in MB
 *
 * If 'pat' is NULL, then all log messages match.
 *
 * The 'type' value may be any character, but 'D', 'I', 'W', 'E' are
 * special when combined with colorization.
 */
LogBuffer *
LogBufferAlloc(const char *pat, char type, long limit)
{
    LogBuffer *lb = malloc(sizeof(*lb));
    if (lb == NULL) return lb;
    if (limit < 1000)
	limit = limit > 0 ? limit * 1024*1024 : 1000;
    lb->limit = limit;
    lb->pat = pat;
    lb->type = type;
    LogBufferInit(lb);
    return lb;
}

/**
 * Add this log buffer to the list.
 *
 * Log messages are matched against logbuffer patterns in the order
 * in which they were added until a match is found, a logbuffer with
 * a NULL pattern is found, or the end of the list is reached, in
 * which case the message will go into the last buffer.
 */
void
LogBufferAdd(LogBuffer *lb)
{
    if (nLogBuffer >= NA(logbuffers)) {
	fprintf(stderr,
	    "Too many log buffers (limit %zd), ignored\n",
	    NA(logbuffers));
	return;
    }
    logbuffers[nLogBuffer++] = lb;
    LogBufferInit(lb);
}

static void
LogBufferInit(LogBuffer *lb)
{
    lb->first = NULL;
    lb->last = &lb->first;
    lb->end = NULL;
    lb->allocated = 0;
    lb->full = false;
}

/**
 * Add one line to this logbuffer
 */
void
LogBufferAppend(LogBuffer *lb, long seq, const char *line, short fd)
{
    size_t len = strlen(line);
    LogMsg *msg;

    if (!lb->full)
    {
	/* If the buffer is not full, allocate a new LogMsg object for it */
	msg = lmAlloc(len);
	msg->next = NULL;
	*lb->last = msg;
	lb->last = &msg->next;
	lb->allocated += sizeof(*msg) + len + 1;
	if (lb->allocated >= lb->limit) {
	    lb->full = true;
	}
    } else {
	/* Buffer is full, recycle lb->end->next */
	LogMsg *prev = lb->end, *next;
	msg = prev->next != NULL ? prev->next : lb->first;
	if (len > msg->linelen) {
	    /* Ooops, need to allocate a bigger one */
	    next = msg->next;
	    free(msg);
	    msg = lmAlloc(len);
	    msg->next = next;
	    if (lb->end->next == NULL) lb->first = msg;
	    else lb->end->next = msg;
	}
    }
    lb->end = msg;
    msg->seq = seq;
    msg->time = time(NULL);
    msg->fd = fd;
    msg->type = lb->type;
    memcpy(msg->line, line, len+1);
}

/**
 * Empty a log buffer.
 */
void
LogBufferClear(LogBuffer *lb)
{
    LogMsg *msg, *next;
    for (msg = lb->first; msg != NULL; msg = next)
    {
	next = msg->next;
	free(msg);
    }
    LogBufferInit(lb);
}

/**
 * Set up to iterate
 */
static void
LogBufferIterator(LogBuffer *lb)
{
    /* Iteration starts at itEnd->next and loops around until
     * we're back at itEnd
     */
    lb->iter = lb->end;
}

/**
 * Return next logmessage, or NULL if exhausted
 */
static LogMsg *
LogBufferNext(LogBuffer *lb)
{
    LogMsg *next;
    if (lb->iter == NULL) return NULL;
    next = lb->iter->next;
    if (next == 0) next = lb->first;
    if (next == lb->end) lb->iter =  NULL;
    else lb->iter = next;
    return next;
}

static LogMsg *
lmAlloc(size_t len)
{
    LogMsg *msg = malloc(sizeof(*msg) + len + 1);
    msg->linelen = len;
    return msg;
}

#if 0
static void
LogBufferDump(LogBuffer *lb)
{
    LogMsg *msg;
    for (msg = lb->first; msg != NULL; msg = msg->next) {
	printf("%ld %c %s %s\n",
	  msg->seq, msg->type, timeStr(msg->time), msg->line);
    }
}

static void
LogBufferIterate(LogBuffer *lb)
{
    LogMsg *msg;
    lbIterator(lb);
    while ((msg = lbNext(lb)) != NULL) {
	printf("%ld %c %s %s\n",
	  msg->seq, msg->type, timeStr(msg->time), msg->line);
    }
}
#endif

#pragma mark -- Exclusion patterns --

static const char *excludePats[100];
static int numExclude = 0;

static void
rstrip(char *buffer)
{
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len-1] == '\n')
	buffer[len-1] = '\0';
}

/**
 * Add this string to the exclusion patterns. If 'pat' is encountered
 * in a log message, the message is discarded.
 */
void
ExcludeAdd(const char *pat)
{
    if (numExclude >= NA(excludePats)) {
	fprintf(stderr,
	    "Too many exclude patterns (limit %zd), \"%s\" ignored\n",
	    NA(excludePats), pat);
	return;
    }
    excludePats[numExclude++] = pat;
}

/**
 * Add strings from a file to the exclusion patterns
 */
void
ExcludeAddFile(const char *filename)
{
    FILE *xfile = fopen(filename, "r");
    char pat[1024];
    if (xfile == NULL) {
	perror(filename);
	return;
    }
    while (fgets(pat, sizeof(pat), xfile) != NULL)
    {
	rstrip(pat);
	ExcludeAdd(strdup(pat));
    }
    fclose(xfile);
}

/**
 * Check this line against all patterns, return true if exlcuded.
 */
bool
ExcludeTest(const char *line)
{
    int i;
    for (i=0; i<numExclude; ++i) {
	if (strstr(line, excludePats[i]) != NULL)
	    return true;
    }
    return false;
}


#pragma mark -- Triggers --

static int triggerCount, tcontext;
static const char *triggers[MAX_TRIGGERS];

/**
 * Set the trigger parameters.
 * @param count      Number of times the trigger has to be seen
 * @param context    Number of log events recorded after the trigger
 *
 * Once the trigger is seen 'count' times, and then 'context' log
 * events have been seen, the logs are dumped and then disabled.
 *
 * Once a trigger has occurred, you need to call this again to
 * "re-arm" it.
 */
void
TriggerParams(int count, int context)
{
    triggerCount = count;
    tcontext = context;
}

/**
 * Add a string to the trigger list.
 */
void
TriggerAdd(const char *trigger)
{
    if (numTrigger >= NA(triggers)) {
	fprintf(stderr,
	    "Too many trigger patterns (limit %zd), \"%s\" ignored\n",
	    NA(triggers), trigger);
	return;
    }
    triggers[numTrigger++] = trigger;
}

/**
 * Check this line against all patterns, return matching trigger or NULL
 * if no match.
 */
const char *
TriggerTest(const char *line)
{
    int i;
    for (i=0; i<numTrigger; ++i) {
	if (strstr(line, triggers[i]) != NULL)
	    return triggers[i];
    }
    return NULL;
}

/**
 * Check this line against the trigger patterns. Counting matching
 * triggers and executing the context countdown as appropriate. Return true if
 * all conditions satisfied and it's time to dump the logs.
 */
bool
TriggerCheck(const char *str)
{
    const char *match;
    if (numTrigger <= 0) return false;
    if (tcontext <= 0) return true;
    if (triggerCount <= 0) return --tcontext <= 0;
    if ((match = TriggerTest(str)) != NULL) {
	fprintf(stderr, "log triggered, pattern \"%s\"\n", match);
	--triggerCount;
    }
    return false;
}




#pragma mark -- NBFile module --

/**
 * Like a stdio FILE, but doesn't return partial lines
 */
struct nbfile {
    int fd;
    int ptr;	/* pointer to next char to return */
    int len;	/* total chars in buffer */
    char buffer[2048];
};

static NBFile *
NBFileOpen(int fd)
{
    NBFile *file = malloc(sizeof(*file));
    if (file == NULL) return NULL;
    file->fd = fd;
    file->ptr = file->len = 0;
    return file;
}

static char *
NBFileRead(NBFile *file)
{
    ssize_t len;
    char *rval, *ptr;
    /* Read from fd until no more or buffer is full */
    for(;;) {
	int iptr = file->ptr + file->len;
	int maxread = sizeof(file->buffer) - iptr - 1;
	if (maxread <= 0) break;
	len = read(file->fd, file->buffer + iptr, maxread);
	if (len <= 0) break;
	file->len += len;
    }
    file->buffer[file->ptr + file->len] = '\0';
    if (file->len <= 0) {
	file->ptr = file->len = 0;
	return NULL;
    }
    rval = file->buffer + file->ptr;
    ptr = strchr(rval, '\n');
    if (ptr != NULL) {
	/* Have a full line, terminate and return it */
	len = ptr - rval;
	*ptr = '\0';
	file->ptr += len + 1;
	file->len -= len + 1;
	return rval;
    } else {
	/* partial line */
	if (file->ptr + file->len >= sizeof(file->buffer) - 100) {
	    /* Partial line and low on room */
	    memmove(file->buffer, file->buffer + file->ptr, file->len);
	    file->ptr = 0;
	}
	return NULL;
    }
}



#pragma mark -- ANSI colors --

static char const * const colors[] = {
 "\x1b[30m",	/* fg black */
 "\x1b[31m",	/* fg red */
 "\x1b[32m",	/* fg green */
 "\x1b[33m",	/* fg yellow */
 "\x1b[34m",	/* fg blue */
 "\x1b[35m",	/* fg magenta */
 "\x1b[36m",	/* fg cyan */
};

static const char *normal = "\x1b[m";

/**
 * Return color escape code
 */
static const char *
ansiColor(int n)
{
    return colors[n%NA(colors)];
}

/**
 * Return color escape code
 */
static const char *
colorStart(char type, int fd)
{
    switch (showcolor) {
      case NONE: return "";
      case FDS: return ansiColor(fd-1);
      case SEVERITY:
	  switch (type) {
	    case 'D': return ansiColor(4);
	    case 'I': return ansiColor(2);
	    case 'W': return ansiColor(3);
	    case 'E': return ansiColor(1);
	    default: return ansiColor(0);
	  }
    }
}

/**
 * Return color escape code
 */
static const char *
colorStop()
{
    switch (showcolor) {
	case NONE: return "";
	default: return normal;
    }
}


#pragma mark foo

#if 0

static const char *dpat = " debug ";
static const char *ipat = " info ";
static const char *wpat = " warning ";
static const char *epat = " error ";
static int triggerN = 100;
static int triggerC = 1;

static FILE *ofile;

static void child(int fds[MAX_FDS], int pfds[MAX_FDS][2],
	  int nfds, char **args, int argc);
static void parent(int fds[MAX_FDS], int pfds[MAX_FDS][2], int nfds);


#if 0
static void lbDump(LogBuffer *lb);
static void lbIterate(LogBuffer *lb);
#endif

static void addExclude(const char *pat);
static void addExcludeFile(const char *filename);
static bool testExclude(const char *line);

static void triggerAdd(const char *trigger);
static void triggerInit();
static bool triggerCheck(const char *str);

static const char * ansiColor(int n);

#endif
