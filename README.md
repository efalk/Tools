# A collection of small (and tiny) useful scripts and tools, by Ed Falk

A collection of very trivial apps that I find handy. Here so I can
access them from anywhere, and to share with interested parties.

Name | What it is
---- | ----
[ps2book](#ps2book) | Re-arrange a postscript file into signatures for printing
[toboxes](#toboxes) | Convert ascii-art boxes to unicode art
[totree](#totree) | Add leader lines to indented text for readability
[tree](#tree) | tree(1) command for operating systems that don't have it
[alldone](#alldone) | Generate an "All done" popup on your screen.
[notrail](#notrail) | Strip trailing blanks from file(s).
[superlog](#superlog) | Filtering and processing of verbose log output

## ps2book

Accepts a postscript file and re-arranges the pages to output four
pages per physical sheet of paper (2 pages/side). The output can then
be folded in half "folio" style to make a booklet.

This is equivalent to combining the psutils "psbook" and "psnup"
programs, but is simpler and works with different page sizes (see
https://github.com/rrthomas/psutils/issues/59).

It can also auto-rotate pages to fit if that is called for.

By default, it generates a single signature which is a stack
of papers you fold together to make a booklet. If there are very
many pages, you can use the '-s' option to batch pages together,
e.g. "-s 32" would batch 32 pages at a time into 8-sheet signatures.

Run with "--help" for full options.

## toboxes

Convert

    +-----+
    | foo |
    +-----+

to

    ┌─────┐
    │ foo │
    └─────┘

Pipe your crude ascii art through this script to convert to Unicode
box-drawing graphics. Does not take any command line arguments.


## totree

Add decorations to an indented tree.

    foo
       bar
          fee
            fie
              foe
          quux
       blah
          bing

becomes

    foo
    ├─ bar
    │  ├─ fee
    │  │  └─fie
    │  │    └─foe
    │  └─ quux
    └─ blah
       └─ bing

  Pipe your text through this script to add graphics.

  Options:

    -A		Generate ascii instead of Unicode.

## tree

Like the tree(1) command if your operating system doesn't have it.
Written in Python, should work on all Unix variants and maybe
Windows as well. Generates Unicode box drawing artwork instead of
crude ascii art.

    .
    ├── .README.md.swp
    ├── .git
    │   ├── HEAD
    │   ├── config
    │   ├── description
    │   ├── hooks
    │   │   ├── applypatch-msg.sample*
    │   │   ╰── update.sample*
    │   ├── index
    │   ├── info
    │   │   ╰── exclude
    │   ╰── packed-refs
    ├── LICENSE
    ├── README.md
    ├── toboxes*
    ├── totree*
    ╰── tree*

Options:

    -a	show hidden files
    -A	Ascii instead of Unicode


## alldone

Pops up a tiny dialog box with the text of your choice (default is
"all done"). On systems that support it, also says "Done" on the
audio. A lovely little tool to get your attention when a long compile
is finished.

Unix and Mac.

    make myhugeproject; alldone
    make myhugeproject; alldone Build is finished

Options:

    -q    quiet; suppress the audible "Done"
    str   alternate message other than "all done"

## notrail

Strip trailing blanks from files. With no arguments, pipes stdin => stdout.

    notrail [files...]



## Superlog
### When you absolutely, positively, need to generate a buttload of logs.

### Synopsis
Superlog is a command-line tool and a library to provide a circular log buffer for any application.

Say, for instance, that your application will be logging every byte it transmits over the network, and it can take days to generate a failure. You don't want to slow your application down with logging, and you don't want to fill up your disk space with logs either. **superlog** will collect those logs for you for as long as needed, and then dump the last 2 Mb worth when the program finally exits.

While the simplest use case is to use **superlog** to collect logs from stderr, there are options to collect logs from any open file descriptor, to separate logs by severity, define triggers, and even color-code the log output.
## Examples
    superlog -- myprogram -v
This very simple example executes `myprogram -v`, collecting the output of stderr into a circular buffer. When `myprogram` finishes (or is terminated), the last 2Mb of log data will be output to the terminal.

    superlog 1,2 -t -C -Ts "FATAL" -Tn 5 -- myprogram -v
Collect logs from both stdout and stderr. Add timestamps. Colorize
the output based on severity. If "FATAL" is spotted in the logs,
collect five more lines of output then dump the logs.


**superlog** scans incoming messages to classify their severity.
By default, the severities are "debug", "info", and everything else.
No more than 2Mb of each severity are kept. This keeps highly
verbose, but not very important messages from pushing more important
messages out of the buffer. Messages may optionally be color-coded
when finally output. Timestamps may be added. Triggers may be
specified to cause the logs to be dumped immediately instead of
waiting for the program to terminate. Filters can be added to ignore
some log messages.

By default, collects stderr from the program under test, leaving
stdout alone. Command line options allow other file descriptors to
be collected as well. File descriptor 1 is stdout. You can use other
file descriptors as well, allowing your test program to use dedicated
logging on those descriptors.

## Options
* **-h** — Help
* **-v** — Echo log messages to stdout in real time as well as buffering them.
* **-o** *file* — output to file instead of stdout
* **1, 2, 3…** — numbers are the file descriptors of the child
program which will be collected into logs. If no file descriptors
are specified, by default, file descriptor 2 (stderr) is collected.
Note that if you use file descriptors other than 1 and 2 (stdout
and stderr) the child program can then open those fds for dedicated
logging output. libsuperlog has utility functions to facilitate
this.
* **-t** — Add timestamps
* **-f** — Add fd number to log messages
* **-c** — Color log messages according to fd number
* **-C** — Color log messages according to severity
* **-d** *N* — Allocate *N* Mb for "debug" messages
* **-i** *N* — Allocate *N* Mb for "info" messages
* **-b** *N* — Allocate *N* Mb for all other messages
* **-Ts** *str* — Add trigger; logging stops and logs are dumped after this string is seen
* **-Tn** *N* — Include **N** lines of context after the trigger
* **-Tc** *N* — Trigger must be seen **N** times before triggering
* **-dpat** *str* — Set pattern that denotes a debug line
* **-ipat** *str* — Set pattern that denotes an info line
* **-wpat** *str* — Set pattern that denotes a warning line
* **-epat** *str* — Set pattern that denotes an error line
* **-x** *str* — Add *str* to list of ignored patterns
* **-X** *file* — Read ignored patterns from file

Send SIGUSR1 to **superlog** to cause it to dump the logs.

## libsuperlog

libsuperlog.[ch] is a support library which can be used in several ways:

* Utilities that help the child program generate log messages on an
alternate file descriptor, for superlog to receive.
* Allows you to write your own front end program if superlog doesn't do what you want
* Allows you fork the program under test into the front end and child process

### Logging utilities for child program

See the comments in superlog.h for more detailed documentation.

* `superlogInit(int fd)` — enable log output on specified fd.
* `superlog(const char *format, ...)`
* `vsuperlog(const char *format, va_list)`
* `superlogDump()` — trigger superlog to dump the logs

### Advanced usage

Libsuperlog allows you to write your own version of the superlog
program for more flexibility. superlog.c is not much more than a
convenience wrapper around libsuperlog. Feel free to use superlog.c
as a guide to writing your own custom logging.

You can configure logging and then call `SuperLog()` to fork the
program under test for fine-grained control over the logging process.
You must call `LogBufferAlloc()` and `LogBufferAdd()` to configure
at least one log buffer before calling `SuperLog()`.

* `LogBufferAlloc(const char *pat, char type, long limit)` — Create a buffer to hold logs
* `LogBufferAdd(LogBuffer *)` — Add a log buffer
* `ExcludeAdd(const char *pat)` — Add a string to the exclusion list
* `ExcludeAddFile(const char *filename)` — Add all strings in file (one per line) to the exclusion list
* `TriggerAdd(const char *trigger)` — Add string to trigger list
* `TriggerParams(int count, int contet)` — Set trigger count and context lines
* `extern bool timestamps` — set to true to enable timestamps
* `extern bool showfds` — set to true to include fds in log messages
* `extern bool verbose` — set to true to echo log messages to stdout
* `extern enum colorize showcolor` — how to colorize log messages: NONE, FDS, or SEVERITY
* `SuperLog(int *fds, int nfds, char **argv, int (*func)(int argc, char **argv, const char *ofilename)` — Main entry point.
Child process is forked and log collection begins.
* `LogParent(int *ofds, int *ifds, int nfds)` — Main loop of parent process. Normally invoked from `Superlog()`
* `LogDump()` — Output the logs collected so far and clear the buffers. Log collection continues. Normally called
from `LogParent()` when the child exits, a trigger string is seen in the logs, or SIGUSR1 received.

General notes: Patterns here are simple strings; superlog does not use globs or regexes.
