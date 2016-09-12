# A collection of small (and tiny) useful scripts and tools, by Ed Falk

A collection of very trivial apps that I find handy. Here so I can
access them from anywhere, and to share with interested parties.

Name | What it is
---- | ----
[toboxes](#toboxes) | Convert ascii-art boxes to unicode art
[totree](#totree) | Add leader lines to indented text for readability
[tree](#tree) | tree(1) command for operating systems that don't have it
[alldone](#alldone) | Generate an "All done" popup on your screen.
[notrail](#notrail) | Strip trailing blanks from file(s).

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


