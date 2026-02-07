#!/usr/bin/env python3
# -*- coding: utf8 -*-
#
# Copyright © Edward Falk, 2023
# Postscript prolog code by Reuben Thomas
# License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.
# This is free software: you are free to change and redistribute it.
# There is NO WARRANTY, to the extent permitted by law.


from __future__ import print_function

usage = """Rearrange and resize pages in a postscript document
to create book signatures.

Usage:  ps2book [options] infile.ps [outfile.ps]

        -h --help       This text
        -V --version    Version
        -s n            Pages/signature, (will rounded up to a multiple of 4)
        -Q              Quarto; 4 pages per side of each sheet (see below)
        -p paper        Specify output paper, see below; default is letter
        -r              Rotate pages for best fit as needed
        -S              Add staple marks on outermost page of each signature
        -C              Add centerline on innermost page of each signature
        -q              quiet; no status report

Default is to output to stdout.

Without -s, default is to generate a single signature for all pages.

Output paper can be a0-a10, b0-b10, letter, legal, tabloid, 11x17, or WxH.

W and H are postscript points, NNcm, NNmm, or NNin. E.g. "612x792"
or "8.5inx11in".

Folio: The default. Two pages per side of physical sheet. Output
pages will be landscape orientation; it is assumed your printer can
handle this and will print the duplex pages correctly.

(On a Mac, in the print dialog, select "Two-Sided", select the
appropriate page size, adjust scale if needed to fill the page.  Go
to the "Layout" dropdown and select "Two-Sided: Short-Edge binding".)

Quarto: Four pages per side of physical sheet; the two pages on
top are upside-down. First fold the page along the horizontal axis,
then again along the vertical axis, staple in the center, then
slit along the top edge to free the pages. For quarto, -s will be
rounded up to a multiple of 8.

Note that larger signatures can be unwieldy to fold and staple,
especially with quarto. You might consider consider using -s 1
so that each signature is a single sheet of paper, and then find
another way to bind them together; that's how e.g. Shakespeare's
quartos were done.

Exit codes:

        0 - command accepted, successful return
        2 - user error
        3 - error
"""

VERSION = "2.0.0"
COPYRIGHT = "ps2book " + VERSION + """
Copyright © Edward Falk, Reuben Thomas 2023,2026.
License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
"""

import sys
import os
import string
import errno
import signal
import getopt

IN2P = 72
CM2P = 72 * 0.393701
MM2P = 72 * 0.0393701

PY3 = sys.version_info[0] >= 3
if PY3:
    basestring = str

sigsize = None      # Pages per signature, must be multiple of 4
paper = 'letter'    # Eventually replaced with (w,h) in postscript points
autoRotate = False  # Rotate to fit
staples = False     # Add staple marks
centerline = False  # Add centerline marks
quiet = False
quarto = False      # Four pages per side

class Page(object):
    """Represents one page of the input."""
    def __init__(self, offset):
        self.offset = offset
        self.size = 0
        self.pageno = 0
        self.bbox = [0,0,0,0]
    def __repr__(self):
        return '<page %d [%d %d %d %d] %d:%d>' % \
            (self.pageno, self.bbox[0], self.bbox[1], self.bbox[2], self.bbox[3],
            self.offset, self.size)


def main():
    global sigsize, quarto, paper, autoRotate, staples, centerline, quiet


    # Get arguments with getopt
    long_opts = ['help', 'version']
    try:
        (optlist, args) = getopt.getopt(sys.argv[1:], 'hs:p:rVSCQ', long_opts)
        for flag, value in optlist:
            if flag in ('-h', "--help"):
                print(usage)
                return 0
            if flag in ('-V', "--version"):
                print(COPYRIGHT)
                return 0
            elif flag == '-s':
                sigsize = getInt(value)
            elif flag == '-Q':
                quarto = True
            elif flag == '-p':
                paper = value
            elif flag == '-r':
                autoRotate = True
            elif flag == '-S':
                staples = True
            elif flag == '-C':
                centerline = True
            elif flag == '-q':
                quiet = True
    except getopt.GetoptError as e:
        print(e)
        sys.exit(2)

    if not args:
        print("Input filename required; use --help for more info", file=sys.stderr)
        return 2
    ifilename = args[0]

    opaper = parsePaper(paper)
    if opaper is None:
        print("Paper size %s is invalid; use --help for more info" % paper, file=sys.stderr)
        return 2
    if not quarto:
        paper = opaper[::-1]   # Switch paper to landscape
    else:
        paper = opaper

    try:
        ifile = open(ifilename, "r")
    except Exception as e:
        print("Unable to open '%s', %s" % (ifilename, e), file=sys.stderr)
        return 3

    if len(args) < 2:
        ofile = sys.stdout
    else:
        try:
            ofile = open(args[1], "w")
        except Exception as e:
            print("Unable to open '%s', %s" % (ofilename, e), file=sys.stderr)
            return 3

    # Analyze the input file, get info on all pages
    pages, trailer = analyze(ifile)
    np = len(pages)
    #print(pages)

    if sigsize is None:
        sigsize = len(pages)
    sigsize = round_up(sigsize, 8 if quarto else 4)

    # Programming notes w.r.t. rotation:
    # For folio printing, we'll lay out the paper in landscape and
    # rotate the input pages as needed to fit their individual
    # orientations.
    # For quarto, landscape vs portrait input determines if the
    # output pages are landscape or portrait.
    if quarto and pagesAreLandscape(pages):
        paper = paper[::-1]   # Switch paper to landscape

    # Rearrange in book order, folio or quarto.
    # Obtain list of signatures, each of which is a list of
    # output pages.
    signatures = rearrange(pages, sigsize, quarto)

    # Output the modified file
    generate(ifile, ofile, signatures, trailer, paper)

    if not quiet:
        print(f"Done, {np} pages output to {len(signatures)} signatures", file=sys.stderr)

    return 0


def analyze(ifile):
    """Examine input file to determine how many pages it has and what
    their dimensions are. Returns a list of Page objects and the offset of
    the trailer, if any"""
    ifile.seek(0)
    npages = None
    pages = []
    # Read front matter
    bbox = None
    inprolog = True
    offset = 0
    page = None
    trailer = None

    def endPage():
        """Called when we know we've reached the end of a page"""
        if pages:
            pages[-1].size = offset - pages[-1].offset
        if pbbox is None and bbox is not None:
            page.bbox = bbox

    while True:
        offset = ifile.tell()
        line = ifile.readline()
        if not line: break
        line = line.rstrip()
        if inprolog:
            if line.startswith('%%BoundingBox'):
                bbox = parseNumbers(line)
            elif line.startswith('%%Pages:'):
                npages = parseNumbers(line)
                if npages is not None:
                    npages = int(npages[0])
            elif line.startswith('%%EndComments'):
                inprolog = False
        elif line.startswith('%%Page:'):
            if page:
                endPage()
                page = None
            pbbox = None
            page = Page(offset)
            pages.append(page)
            line = line.split()
            if len(line) >= 3:
                pageno = getInt(line[2])
                if pageno is not None:
                    page.pageno = pageno
        elif line.startswith('%%Trailer'):
            trailer = offset
            break
        elif line.startswith('%%EOF'):
            break
        elif page:
            if line.startswith('%%PageBoundingBox:'):
                pbbox = parseNumbers(line)
                if pbbox is not None:
                    page.bbox = pbbox
    if page:
        endPage()
        page = None
    return pages, trailer

def pagesAreLandscape(pages):
    nl = np = 0
    for page in pages:
        pwid = page.bbox[2] - page.bbox[0]
        phgt = page.bbox[3] - page.bbox[1]
        if pwid >= phgt: nl += 1
        else: np += 1
    return nl >= np

def rearrange(pages, sigsize, quarto):
    """Take the input pages, in batches of size sigsize,
    and re-arrange them in book signature order. If the last
    signature is short, reduce its size and pad with None
    as needed.
    Return list of signatures, each of which is a list of
    output pages."""
    signatures = []
    while pages:
        opages = []
        signature = pages[:sigsize]
        del pages[:sigsize]
        if len(signature) < sigsize:
            sigsize = round_up(len(signature), 8 if quarto else 4)
        if not quarto:
            for i in range(0, sigsize//2, 2):
                opages.append(getPage(signature, sigsize-1-i))
                opages.append(getPage(signature, i))
                opages.append(getPage(signature, i+1))
                opages.append(getPage(signature, sigsize-2-i))
        else:

# 1st quarter of pages go at bottom, starting at p1,lr, alternating
#   so p1.lr, p2.ll, p3.lr, p4.ll, etc.
# next quarter go at top, going back in the other direction, flipped
#   so p4.ul, p3.ur, p2.ul, p1.ur
# next quarter go at top, going forward again, flipped
#   so p1.ul,p2.ur,p3.ul,p4.ur
# final quarter back at the bottom, going backwards
#   so p4,lr, p3.ll, p2.lr, p1.ll

            s2 = sigsize//2
            for i in range(0, sigsize//4, 2):
                # front side, ll,lr,ul,ur
                opages.append(getPage(signature, sigsize-1-i))
                opages.append(getPage(signature, i))
                opages.append(getPage(signature, s2+i))
                opages.append(getPage(signature, s2-1-i))
                # back side, ll,lr,ul,ur
                opages.append(getPage(signature, i+1))
                opages.append(getPage(signature, sigsize-2-i))
                opages.append(getPage(signature, s2-2-i))
                opages.append(getPage(signature, s2+1+i))

        signatures.append(opages)
    return signatures

def getPage(pages, idx):
    """Return an item from the list, or None if out of bounds."""
    return pages[idx] if idx < len(pages) else None

PROLOG = """
% Begin material added by ps2book.py:

%%BeginProcSet: PS2Book 1 15

userdict begin

[ /showpage /erasepage /copypage ]
{
  dup where { pop dup load
           type /operatortype eq { /PS2BookEnablepage cvx 1 index
           load 1 array astore cvx {} bind /ifelse cvx 4 array
           astore cvx def} {pop} ifelse }
         { pop } ifelse
} forall

/PS2BookEnablepage true def

[ /letter /legal /executivepage /a4 /a4small /b5 /com10envelope
 /monarchenvelope /c5envelope /dlenvelope /lettersmall /note
 /folio /quarto /a5]
{
  dup where { dup wcheck { exch {} put } { pop {} def } ifelse }
        { pop } ifelse
} forall

/setpagedevice { pop } bind
1 index where{dup wcheck { 3 1 roll put } {pop def} ifelse } {def} ifelse

/PS2BookMatrix matrix currentmatrix def
/PS2BookXform matrix def
/PS2BookClip { clippath } def
/defaultmatrix {PS2BookMatrix exch PS2BookXform exch concatmatrix} bind def
/initmatrix { matrix defaultmatrix setmatrix } bind def
/initclip [{matrix currentmatrix PS2BookMatrix setmatrix
 [{currentpoint}stopped{$error/newerror false put{newpath}}
 {/newpath cvx 3 1 roll/moveto cvx 4 array astore cvx}ifelse]
 {[/newpath cvx{/moveto cvx}{/lineto cvx}
 {/curveto cvx}{/closepath cvx}pathforall]cvx exch pop}
 stopped{$error/errorname get/invalidaccess eq{cleartomark
 $error/newerror false put cvx exec}{stop}ifelse}if}bind aload pop
 /initclip dup load dup type dup/operatortype eq{pop exch pop}
 {dup/arraytype eq exch/packedarraytype eq or
  {dup xcheck{exch pop aload pop}{pop cvx}ifelse}
  {pop cvx}ifelse}ifelse
 {newpath PS2BookClip clip newpath exec setmatrix} bind aload pop] cvx def

/initgraphics { initmatrix newpath initclip 1 setlinewidth
 0 setlinecap 0 setlinejoin [] 0 setdash 0 setgray
 10 setmiterlimit } bind def
end

%%EndProcSet
"""

def generate(ifile, ofile, signatures, trailer, paper):
    generateProlog(ifile, ofile, signatures, paper)

    # Generate output 2-by-2
    for pages in signatures:
        np = len(pages)
        if not quarto:
            for i in range(0, np, 2):
                isfirst = i==0
                islast = i >= np-2
                generatePage(ifile, ofile, pages[i:i+2], i//2+1, paper,
                    isfirst, islast)
        else:
            for i in range(0, np, 4):
                isfirst = i==0
                generateQuarto(ifile, ofile, pages[i:i+4], i//4+1, paper,
                    isfirst)

    if trailer:
        copyTrailer(ifile, ofile, trailer)

    print("%%EOF", file=ofile)


def generateProlog(ifile, ofile, signatures, paper):
    print("%!PS-Adobe-3.0", file=ofile)
    print("%%%%DocumentMedia: plain %g %g 0 () ()" % paper, file=ofile)
    print("%%%%BoundingBox: 0 0 %d %d" % paper, file=ofile)
    print("%%%%HiResBoundingBox: 0 0 %.2f %.2f" % paper, file=ofile)
    print("%%Creator: ps2book.py", file=ofile)
    print("%%LanguageLevel: 2", file=ofile)
    np = sum(len(pages) for pages in signatures)    # number of logical pages
    pp = np//2 if not quarto else np//4             # number of physical pages
    print("%%%%Pages: %d 0" % pp, file=ofile)
    print("%%EndComments", file=ofile)
    print("%%BeginProlog", file=ofile)

    # Device specifications; we'll be rotating the page to landscape
    print("<< /PageSize [%g %g] /Duplex true>> setpagedevice" % \
        paper, file=ofile)
    print(PROLOG, file=ofile)
    copyProlog(ifile, ofile)

    print("%%EndProlog", file=ofile)


def copyProlog(ifile, ofile):
    """Copy the prolog from the input file to the output file"""
    ifile.seek(0)
    inProlog = False
    for line in ifile:
        line = line.rstrip()
        if line.startswith('%%BeginProlog'):
            inProlog = True
        elif line.startswith('%%EndProlog'):
            break
        elif inProlog:
            print(line, file=ofile)


def generatePage(ifile, ofile, pages, pageno, paper, isfirst, islast):
    p0 = getPage(pages, 0)
    p1 = getPage(pages, 1)
    print('%%%%Page: "(%d,%d)" %d' % \
        (p0.pageno if p0 else 0,
         p1.pageno if p1 else 0,
         pageno), file=ofile)
    #print("BeginEPSF", file=ofile)

    pwid = paper[0]
    phgt = paper[1]
    if p0:
        halfpage(ifile, ofile, p0, [0, 0, pwid/2, phgt])
    if p1:
        halfpage(ifile, ofile, p1, [pwid/2, 0, pwid, phgt])

    #print("EndEPSF", file=ofile)
    if staples and isfirst:
        lineInPage(ofile, (.5,.1,.5,.15), paper)
        lineInPage(ofile, (.5,.85,.5,.9), paper)
    if centerline and islast:
        lineInPage(ofile, (.5,.25,.5,.75), paper)
    print("showpage\n", file=ofile)


def halfpage(ifile, ofile, page, pbox, qrotate=False):
    """Display this input page in the given bounding box."""
    # We'll be rotating 90 degrees and also scaling down. Comparing
    # to the output of psnup, it looks like we can leave the original
    # %%PageBoundingBox intact.
    # qrotate says to rotate the page within it's 1/4 section.
    if qrotate:
        pbox = (pbox[2],pbox[3],pbox[0],pbox[1])
    pwid = pbox[2] - pbox[0]
    phgt = pbox[3] - pbox[1]
    bbox = page.bbox
    wid = bbox[2] - bbox[0]
    hgt = bbox[3] - bbox[1]
    # rotate to fit?
    doRotate = shouldRotate(pwid, phgt, wid, hgt)
    if doRotate:
        sx = pwid / hgt
        sy = phgt / wid
        scale = min(sx, sy)
        # In theory, one of these should be 0, and the other >0
        dx = pwid - (pwid - hgt*scale)/2
        dy = (phgt - wid*scale)/2
    else:
        sx = pwid / wid
        sy = phgt / hgt
        scale = min(sx, sy) if sx>=0 else max(sx,sy)
        # In theory, one of these should be 0, and the other >0
        dx = (pwid - wid*scale)/2
        dy = (phgt - hgt*scale)/2

    print("userdict /PS2BookSaved save put", file=ofile)
    print("PS2BookMatrix setmatrix", file=ofile)

    print("%.5f %.5f translate" % (dx+pbox[0],dy+pbox[1]), file=ofile)
    print("%.5f %.5f scale" % (scale, scale), file=ofile)
#    if qrotate:
#        print("120 rotate", file=ofile)
    if doRotate:
        print("90 rotate", file=ofile)

    # TODO: clip path
    print("/PS2BookEnablepage false def", file=ofile)
    print("PS2BookXform concat", file=ofile)

#    # test for now
#    print("newpath", file=ofile)
#    rect(0,0, wid, hgt, ofile)
#    rect(72,72, wid-72, hgt-72, ofile)
#    print("%g %g moveto %g %g lineto %g %g moveto %g %g lineto" % \
#        (0,0, wid,hgt, wid,0, 0,hgt), file=ofile)
#    print("stroke", file=ofile)
#    print("/Helvetica findfont 24 scalefont setfont", file=ofile)
#    print("%g %g moveto (page %d) show" % (wid/2, hgt/2, page.pageno), file=ofile)

    if page:
        print("% Begin original page content", file=ofile)
        copyPage(ifile, ofile, page)
        print("% End original page content", file=ofile)

    print("PS2BookSaved restore", file=ofile)
    print(file=ofile)

def generateQuarto(ifile, ofile, pages, pageno, paper, isfirst):
    p0 = getPage(pages, 0)
    p1 = getPage(pages, 1)
    p2 = getPage(pages, 2)
    p3 = getPage(pages, 3)
    print('%%%%Page: "(%d,%d,%d,%d)" %d' % \
        (p0.pageno if p0 else 0,
         p1.pageno if p1 else 0,
         p2.pageno if p2 else 0,
         p3.pageno if p3 else 0,
         pageno), file=ofile)
    #print("BeginEPSF", file=ofile)

    pwid = paper[0]
    phgt = paper[1]
    if p0:
        halfpage(ifile, ofile, p0, [0, 0, pwid/2, phgt/2], False)
    if p1:
        halfpage(ifile, ofile, p1, [pwid/2, 0, pwid, phgt/2], False)
    if p2:
        halfpage(ifile, ofile, p2, [0, phgt/2, pwid/2, phgt], True)
    if p3:
        halfpage(ifile, ofile, p3, [pwid/2, phgt/2, pwid, phgt], True)

    #print("EndEPSF", file=ofile)
    if isfirst:
        if staples:
            lineInPage(ofile, (.5,.1,.5,.15), paper)
            lineInPage(ofile, (.5,.35,.5,.4), paper)
        if centerline:
            lineInPage(ofile, (.5,.60,.5,.90), paper)
            lineInPage(ofile, (.1,.5,.9,.5), paper)
    print("showpage\n", file=ofile)


def lineInPage(ofile, endpoints, paper=None):
    """Draw a line between the given endpoints. Endpoints are in [0 1] and so
    are optionally scaled up to the dimensions of the paper first."""
    if paper is not None:
        endpoints = (paper[0] * endpoints[0], paper[1] * endpoints[1],
                     paper[0] * endpoints[2], paper[1] * endpoints[3])
    print("0 setlinewidth newpath %.1f %.1f moveto %.1f %.1f lineto stroke" % endpoints, file=ofile)

def rect(llx, lly, urx, ury, ofile):
    print("%g %g moveto %g %g lineto %g %g lineto %g %g lineto closepath" % \
        (llx,lly, urx,lly, urx,ury, llx,ury), file=ofile)


def copyPage(ifile, ofile, page):
    # Copy input to output, disregard first %%Page directive. Stop
    # at next %%Page or %%Trailer
    ifile.seek(page.offset)
    line = ifile.readline()     # Disregard first line
    for line in ifile:
        if line.startswith("%%Page:") or line.startswith("%%Trailer"):
            return
        print(line, file=ofile, end='')

def shouldRotate(pwid, phgt, wid, hgt):
    """True if rotating the page will help it fit the paper better."""
    phgt = abs(phgt)
    pwid = abs(pwid)
    return autoRotate and \
        (phgt >= pwid and wid > hgt or \
         pwid >= phgt and hgt > wid)


def copyTrailer(ifile, ofile, trailer):
    """Copy the trailer from the input file to the output file"""
    ifile.seek(trailer)
    inProlog = False
    for line in ifile:
        line = line.rstrip()
        if line.startswith('%%EOF'):
            return
        print(line, file=ofile)



# UTILITIES


def parseNumbers(line):
    """Extract a list of numbers from a line, e.g. "%%%BoundingBox 0 0 612 792"."""
    line = line.split()[1:]
    line = list(map(getFloat, line))
    if None in line:
        return None
    return line

PAGE_SIZES = { 'a0':(841*MM2P, 1189*MM2P),
    'a1':(594*MM2P, 841*MM2P),
    'a2':(420*MM2P, 594*MM2P),
    'a3':(297*MM2P, 420*MM2P),
    'a4':(210*MM2P, 297*MM2P),
    'a5':(148*MM2P, 210*MM2P),
    'a6':(105*MM2P, 148*MM2P),
    'a7':(74*MM2P, 105*MM2P),
    'a8':(52*MM2P, 74*MM2P),
    'a9':(37*MM2P, 52*MM2P),
    'a10':(26*MM2P, 37*MM2P),
    'b0':(1000*MM2P, 1414*MM2P),
    'b1':(707*MM2P, 1000*MM2P),
    'b2':(500*MM2P, 707*MM2P),
    'b3':(353*MM2P, 500*MM2P),
    'b4':(250*MM2P, 353*MM2P),
    'b5':(176*MM2P, 250*MM2P),
    'b6':(125*MM2P, 176*MM2P),
    'b7':(88*MM2P, 125*MM2P),
    'b8':(62*MM2P, 88*MM2P),
    'b9':(44*MM2P, 62*MM2P),
    'b10':(31*MM2P, 44*MM2P),
    'letter':(8.5 * IN2P, 11 * IN2P),
    'legal':(8.5 * IN2P, 14 * IN2P),
    'tabloid':(11 * IN2P, 17 * IN2P),
    '11x17':(11 * IN2P, 17 * IN2P),
}

def parsePaper(paper):
    """Read paper description, return paper size as (w,h) in ps points."""
    if paper.lower() in PAGE_SIZES:
        return PAGE_SIZES[paper.lower()]
    if 'x' not in paper:
        return None
    paper = paper.split('x')
    paper = (parseDim(paper[0]), parseDim(paper[1]))
    if None in paper:
        return None
    return paper

def parseDim(dim):
    """Parse a dimension, e.g. "11in" or "200mm"."""
    if dim.endswith('mm'):
        dim = getFloat(dim[:-2])
        return dim * MM2P if dim is not None else None
    if dim.endswith('cm'):
        dim = getFloat(dim[:-2])
        return dim * CM2P if dim is not None else None
    if dim.endswith('in'):
        dim = getFloat(dim[:-2])
        return dim * IN2P if dim is not None else None
    return getFloat(dim)

def getFloat(s):
    """Convert string to float, return None instead of exception on error."""
    try:
        return float(s)
    except:
        return None

def getInt(s):
    """Convert string to int, return None instead of exception on error."""
    try:
        return int(s)
    except:
        return None

def round_down(n,r):
    """Truncate 'n' to the next lower multiple of 'r', which must be a
    power of two."""
    return ((n) & ~((r)-1))

def round_up(n,r):
    """Round 'n' to the next higher multiple of 'r', which must be a
    power of two."""
    return round_down((n)+(r)-1,(r))


if __name__ == '__main__':
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    try:
        sys.exit(main())
    except KeyboardInterrupt as e:
        print()
        sys.exit(1)
