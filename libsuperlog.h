#ifndef _SUPERLOG_H
#define	_SUPERLOG_H

#include <stdarg.h>

typedef int bool;
#define false 0
#define true 1

#define	MAX_FDS	8


/**
 * This is the usual main entry point to superlog.
 * Caller should first create as many LogBuffers as are appropriate
 * for the task (e.g. "debug", "info", and "other") and register
 * them with LogBufferAdd(). Optionally add "Trigger" strings with
 * TriggerAdd() and TriggerParams(), add "Excluded" strings with
 * ExcludeAdd() and/or ExcludeAddFile(), and then call
 * this function.
 *
 * This function returns when the child exits or on error, it will
 * either exec the program of your choice with the arguments you
 * provide, or call a function you pass in, with the arguments argc,
 * argv.
 *
 * @param fds   Array of fds to which the child process can write log messages
 * @param nfd   Length of fds[]
 * @param argv  NULL-terminated array of arguments to the child process
 * @param func  Optional pointer to int (*func)(int argc, char **argv)
 * @param file  Name of file to write logs to. If NULL, stdout is used.
 * @return 0 on success, else:
 *	2 = argument error
 *	3 = system error
 *	4 = unable to open output file
 */
extern int SuperLog(int *fds, int nfd, char **argv,
    int (*func)(int argc, char **argv), const char *file);



/* Definitions for superlog child process */

typedef struct LogMsg LogMsg;
typedef struct LogBuffer LogBuffer;


extern void superlogInit(int fd);
extern void superlog(const char *fmt, ...);
extern void vsuperlog(const char *fmt, va_list ap);
extern void superlogDump();




/* Definitions for superlog parent process */

extern bool timestamps;
extern bool showfds;
extern bool verbose;
extern enum colorize {NONE, FDS, SEVERITY} showcolor;

/**
 * Dump logs and clear them
 */
extern void LogDump();

/**
 * Return a new LogBuffer object
 * @param pat    Pattern for lines that go into this LogBuffer
 * @param type   Character to mark the type, e.g. 'D', 'I', 'W', 'E'
 * @param limit  Amount of space, in MB to allocate for this buffer
 * @return Logbuffer object
 *
 * Note that types 'D', 'I', 'W', 'E' are recognized by the colorizing
 * code. Any other value will be rendered in black.
 */
extern LogBuffer *LogBufferAlloc(const char *pat, char type, long limit);

/**
 * Add this log buffer to the list.
 */
extern void LogBufferAdd(LogBuffer *lb);

/**
 * Add one line to this logbuffer
 */
extern void LogBufferAppend(LogBuffer *, long seq, const char *line, short fd);

/**
 * Clear this log buffer
 */
extern void LogBufferClear(LogBuffer *lb);

/**
 * Add this string to the exclusion patterns
 */
extern void ExcludeAdd(const char *pat);

/**
 * Add strings from a file to the exclusion patterns
 */
extern void ExcludeAddFile(const char *filename);

/**
 * Return true if this line matches any of the exclusion patterns.
 */
extern bool ExcludeTest(const char *line);


/**
 * Set the trigger parameters.
 * @param count      Number of times the trigger has to be seen
 * @param countdown  Number of log events recorded after the trigger
 *
 * Once the trigger is seen 'count' times, and then 'countdown' log
 * events have been seen, the logs are dumped and then disabled.
 *
 * Once a trigger has occurred, you need to call this again to
 * "re-arm" it.
 */
extern void TriggerParams(int count, int countdown);

/**
 * Add a string to the trigger list.
 */
extern void TriggerAdd(const char *trigger);

/**
 * Check this line against the trigger patterns. Counting matching
 * triggers and executing the countdown as appropriate. Return true if
 * all conditions satisfied and it's time to dump the logs.
 */
extern bool TriggerCheck(const char *str);

/**
 * Check this line against all patterns, return matching trigger or NULL
 * if no match. Not normally used. You normally use TriggerCheck().
 */
extern const char * TriggerTest(const char *line);



/**
 * After the fork, this function is called in the parent process.
 * This function watches for input on any of the fds in pfds[*][0],
 * classifies it and logs it.
 * @param ifds  list of fds the child will be writing to
 * @param ofds  list of fds the parent will be reading from.
 */
extern void LogParent(int ofds[MAX_FDS], int ifds[MAX_FDS], int nfds);


#endif	/* _SUPERLOG_H */
