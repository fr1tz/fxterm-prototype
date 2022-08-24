#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "compat.h"
#include "error.h"
#include "screen.h"
#include "fxterm.h"

enum
{
	CURSORDIM = 16
};

Memimage			*gscreen;
Memimage			*dmemscreen;
Image			*dscreen;
Point				ZP;
Point				cursorpos;

static Memimage		*back;
static Memimage		*conscol;
static Memimage		*curscol;
static Point			curpos;
static Memsubfont	*memdefont;
static Rectangle		flushr;
static Rectangle		window;
static int			fontheight;

static Rectangle		cursorr;
static Point			offscreen;
static int			cursdrawvers = -1;
static Memimage		*cursorset;
static Memimage		*cursorclear;
static Image		*cursorimg; 
static Image 		*cursormask; 

static uchar			*pixelbuf;			/* buffer big enough to hold raw pixel data for entire screen */
static ulong			pixelbufsize; 

static void
scroll(void)
{
	int o;
	Point p;
	Rectangle r;

	o = 8*fontheight;
	r = Rpt(window.min, Pt(window.max.x, window.max.y-o));
	p = Pt(window.min.x, window.min.y+o);
	memimagedraw(gscreen, r, gscreen, p, nil, p, S);
	r = Rpt(Pt(window.min.x, window.max.y-o), window.max);
	memimagedraw(gscreen, r, back, ZP, nil, ZP, S);
	flushmemscreen(gscreen->clipr);

	curpos.y -= o;
}

static void
screenputc(char *buf)
{
	Point p;
	int w, pos;
	Rectangle r;
	static int *xp;
	static int xbuf[256];

	if(xp < xbuf || xp >= &xbuf[sizeof(xbuf)])
		xp = xbuf;

	switch(buf[0]){
	case '\n':
		if(curpos.y+fontheight >= window.max.y)
			scroll();
		curpos.y += fontheight;
		screenputc("\r");
		break;
	case '\r':
		xp = xbuf;
		curpos.x = window.min.x;
		break;
	case '\t':
		p = memsubfontwidth(memdefont, " ");
		w = p.x;
		*xp++ = curpos.x;
		pos = (curpos.x-window.min.x)/w;
		pos = 8-(pos%8);
		r = Rect(curpos.x, curpos.y, curpos.x+pos*w, curpos.y + fontheight);
		memimagedraw(gscreen, r, back, back->r.min, memopaque, ZP, S);
		screenaddflush(r);
		curpos.x += pos*w;
		break;
	case '\b':
		if(xp <= xbuf)
			break;
		xp--;
		r = Rect(*xp, curpos.y, curpos.x, curpos.y + fontheight);
		memimagedraw(gscreen, r, back, back->r.min, memopaque, ZP, S);
		screenaddflush(r);
		curpos.x = *xp;
		break;
	case '\0':
		break;
	default:
		p = memsubfontwidth(memdefont, buf);
		w = p.x;

		if(curpos.x >= window.max.x-w)
			screenputc("\n");

		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x+w, curpos.y + fontheight);
		memimagedraw(gscreen, r, back, back->r.min, memopaque, ZP, S);
		memimagestring(gscreen, curpos, conscol, ZP, memdefont, buf);
		screenaddflush(r);
		curpos.x += w;
	}
}

static void
emuscreenputs(char *s, int n)
{
	static char rb[UTFmax+1];
	static int nrb;
	char *e;

	dlock();
	e = s + n;
	while(s < e){
		rb[nrb++] = *s++;
		if(nrb >= UTFmax || fullrune(rb, nrb)){
			rb[nrb] = 0;
			screenputc(rb);
			nrb = 0;
		}
	}
	screenflush();
	dunlock();
}

static int
pxquantize(int x, int depth, int high)
{
	int m;

	switch(depth){
	case 1: m = 8; break;
	case 2: m = 4; break;
	case 3: m = 24; break;
	case 4: m = 2; break;
	case 5: m = 40; break;
	case 6: m = 24; break;
	case 7: m = 56; break;
	}
	if(x % m > 0){
		if(high)
			x = x + m - (x % m);
		else
			x = x - (x % m);
	}
	return x;
}

static
int
sniffinit(void)
{
	static int screennum;

	char buf[128];
	int fd, n;
	char *s;
	Rectangle r;

	if(sniffctlfd >= 0){
		fprint(2, "fxterm %d closing sniffctlfd (%d)\n", getpid(), sniffctlfd);
		fprint(sniffctlfd, "stop\n");
		close(sniffctlfd);
	}

	/* Need /dev/draw/sniff */
	sniffctlfd = open("/dev/draw/sniff", OWRITE);
	if(sniffctlfd < 0)
		goto fail;

	/* Need access to an xfer server */
	fd = open("/mnt/xfer/sid", OREAD);
	if(fd < 0)
		goto fail;
	n = read(fd, buf, 128);
	if(n < 0)
		goto fail;

	close(fd);
	buf[n] = 0;
	fprint(2, "xfer SID is %s\n", buf);
	if(sniffsrc)
		free(sniffsrc);
	sniffsrc = smprint("/mnt/xfer/%s/fxterm.%d.screen.%d.bit", buf, getpid(), ++screennum);

	fd = create(sniffsrc, OWRITE, 0660);
	if(fd < 0)
		goto fail;
	close(fd);

	sprint(buf, "fxterm.%d.screen", getpid());
	nameimage(screen, buf, 1);
	r = screen->r;
	if(fprint(sniffctlfd, "dst %s %d %d %d %d\n",  buf, r.min.x, r.min.y, r.max.x, r.max.y) < 0)
		goto fail;
	if(fprint(sniffctlfd, "src %s\n", sniffsrc) < 0)
		goto fail;
	if(fprint(sniffctlfd, "start\n") < 0)
		goto fail;
	return 0;

fail:
	fprint(2, "sniff  init failed (%r), not using sniff\n");
	if(sniffctlfd >= 0)
		close(sniffctlfd);
	sniffctlfd = -1;
	free(sniffsrc);
	sniffsrc = nil;
	return -1;
}

static void
makescreen(int x, int y, int inchan, int outchan)
{
	char buf[128];

	if(sniffinit() < 0)
		fprint(2, "sniff init failed\n");

	freememimage(gscreen);
	freememimage(dmemscreen);
	freeimage(dscreen);
	gscreen = allocmemimage(Rect(0,0,x,y), inchan);
	if(gscreen == nil){
		print(buf, sizeof buf, "can't allocate gscreen image: %r");
		error(buf);
	}
	if(chantodepth(outchan) < 8)
		x = pxquantize(x, chantodepth(outchan), 1);
	dmemscreen = allocmemimage(Rect(0,0,x,y), outchan);
	if(dmemscreen == nil){
		print(buf, sizeof buf, "can't allocate dmemscreen image: %r");
		error(buf);
	}
	dscreen = allocimage(display, dmemscreen->r, outchan, 0, 0x000000ff);
	if(dscreen == nil){
		print(buf, sizeof buf, "can't allocate dscreen image: %r");
		error(buf);
	}
	pixelbufsize = bytesperline(gscreen->r, gscreen->depth)*Dy(gscreen->r);
	pixelbuf = realloc(pixelbuf, pixelbufsize);

/*
	fprint(2, "gscreen rect: %R\n", gscreen->r);
	fprint(2, "gscreen width: %d\n", Dx(gscreen->r));
	
	fprint(2, "dmemscreen rect: %R\n", dmemscreen->r);
	fprint(2, "dmemscreen width: %d\n", Dx(dmemscreen->r));
	fprint(2, "dmemscreen height: %d\n", Dy(dmemscreen->r));
	fprint(2, "dmemscreen depth: %d\n", dmemscreen->depth);
	fprint(2, "dmemscreen bpl: %d\n", bytesperline(dmemscreen->r, dmemscreen->depth));


	fprint(2, "dscreen rect: %R\n", dscreen->r);
	fprint(2, "dscreen width: %d\n", Dx(dscreen->r));
	fprint(2, "dscreen height: %d\n", Dy(dscreen->r));
	fprint(2, "dscreen depth: %d\n", dscreen->depth);
	fprint(2, "dscreen bpl: %d\n", bytesperline(dscreen->r, dscreen->depth));
*/
}

void
screeninit(int x, int y, int inchan, int outchan)
{
	char buf[128];

	memimageinit();
	makescreen(x, y, inchan, outchan);
	offscreen = Pt(x + 100, y + 100);
	cursorr = Rect(0, 0, CURSORDIM, CURSORDIM);
	cursorset = allocmemimage(cursorr, GREY8);
	cursorclear = allocmemimage(cursorr, GREY1);
	if(cursorset == nil || cursorclear == nil){
		freememimage(gscreen);
		freememimage(cursorset);
		freememimage(cursorclear);
		gscreen = nil;
		cursorset = nil;
		cursorclear = nil;
		snprint(buf, sizeof buf, "can't allocate cursor images: %r");
		error(buf);
	}

	curscol = allocmemimage(Rect(0,0,1,1), outchan);
	curscol->flags |= Frepl;
	curscol->clipr = gscreen->r;
	memfillcolor(curscol, 0x674343FF);
	gsetcursor(&arrow);
	screenwin();
}

void
screenresize(int x, int y)
{
	dlock();
	makescreen(x, y, gscreen->chan, dmemscreen->chan);
	offscreen = Pt(x + 100, y + 100);
	dunlock();
	screenwin();
}

void
screenwin(void)
{	
	dlock();
	back = memblack;
	conscol = memwhite;
	
	memdefont = getmemdefont();
	fontheight = memdefont->height;

	memfillcolor(gscreen, 0x000000FF);
	window = insetrect(gscreen->clipr, 10);
	memimagedraw(gscreen, window, memwhite, ZP, memopaque, ZP, S);
	window = insetrect(window, 4);
	memimagedraw(gscreen, window, memblack, ZP, memopaque, ZP, S);
	memimagedraw(gscreen, Rect(window.min.x, window.min.y,
			window.max.x, window.min.y+fontheight+5+6), memwhite, ZP, nil, ZP, S);
	window = insetrect(window, 5);
	memimagestring(gscreen, window.min, memblack, ZP, memdefont, "FXTerm Console");
	window.min.y += fontheight+8;
	curpos = window.min;
	window.max.y = window.min.y+((window.max.y-window.min.y)/fontheight)*fontheight;
	flushmemscreen(gscreen->r);

	dunlock();
	screenputs = emuscreenputs;
}

Memdata*
attachscreen(Rectangle* r, ulong* chan, int* d, int* width, int *softscreen)
{
	*r = gscreen->clipr;
	*d = gscreen->depth;
	*chan = gscreen->chan;
	*width = gscreen->width;
	*softscreen = 1;

	gscreen->data->ref++;
	return gscreen->data;
}

void
getcolor(ulong , ulong* pr, ulong* pg, ulong* pb)
{
	*pr = 0;
	*pg = 0;
	*pb = 0;
}

int
setcolor(ulong , ulong , ulong , ulong )
{
	return 0;
}

/* called with cursor locked, drawlock possibly unlocked */
Rectangle
cursorrect(void)
{
	Rectangle r;

	r.min.x = cursorpos.x + cursor.offset.x;
	r.min.y = cursorpos.y + cursor.offset.y;
	r.max.x = r.min.x + CURSORDIM;
	r.max.y = r.min.y + CURSORDIM;
	return r;
}

/* called with drawlock locked */
void
gsetcursor(Cursor* curs)
{
	uchar mask[2*16];
	Image* clrmask;
	Image* setmask;
	int i;

	clrmask = allocimage(display, cursorr, GREY1, 0, DTransparent);
	loadimage(clrmask, cursorr, curs->clr, 2*16);
	setmask = allocimage(display, cursorr, GREY1, 0, DTransparent);
	loadimage(setmask, cursorr, curs->set, 2*16);

	freeimage(cursorimg);
	cursorimg = allocimage(display, cursorr, screen->chan, 0, DTransparent);
	draw(cursorimg, cursorr, display->white, clrmask, ZP);
	draw(cursorimg, cursorr, display->black, setmask, ZP);

	freeimage(cursormask);
	cursormask = allocimage(display, cursorr, GREY1, 0, DBlack);
	memmove(mask, curs->clr, 2*16);
	for(i = 0; i < 2*16; i++)
		mask[i] |= curs->set[i];
	loadimage(cursormask, cursorr, mask, 2*16);

	freeimage(clrmask);
	freeimage(setmask);
}

/* called with drawlock locked */
void
cursoron(void)
{
	cursorpos = mousexy();
//	flushmemscreen(cursorrect());
}

/* called with drawlock locked */
void
cursoroff(void)
{
	cursorpos = offscreen;
//	flushmemscreen(cursorrect());
}

void
blankscreen(int blank)
{
	USED(blank);
}

void
screenflush(void)
{
	flushmemscreen(flushr);
	flushr = Rect(10000, 10000, -10000, -10000);
}

void
screenaddflush(Rectangle r)
{	
	if(flushr.min.x >= flushr.max.x)
		flushr = r;
	else
		combinerect(&flushr, r);
}

int
zloadimage(Image *i, Rectangle r, uchar *data, int ndata)
{
	static vlong seq;

	char *sn; /* sysname */
	char fn[128]; /* filename */
	char cbuf[20]; /* chan string buf */
	int fd, n;
	uchar *a;

//	sn = getenv("sysname");
	sprint(fn, "/mnt/xfer/fxterm.%d.loadimage.%lld.bit", getpid(), ++seq);
//	free(sn);

	a = bufimage(i->display, 29+strlen(fn)+1);
	if(a == nil){
//		fprint(2, "bufimage failed: %r  -> using default loadimage\n");
		goto Fail;
	}

	fd = create(fn, OWRITE, 0660);
	if(fd < 0){
//		fprint(2, "unable to create %s: %r -> using default loadimage\n", fn);
		goto Fail;
	}
	n = fprint(fd, "%11s %11d %11d %11d %11d ",
		chantostr(cbuf, dmemscreen->chan), r.min.x, r.min.y, r.max.x, r.max.y);
	if(n != 60){
//		fprint(2, "error writing %s: %r -> using default loadimage\n", fn);
		goto Fail;
	}
	n = write(fd, data, ndata);
	if(n != ndata){
//		fprint(2, "error writing %s: %r -> using default loadimage\n", fn);
		goto Fail;
	}
	close(fd);

	a[0] = 'z';
	BPLONG(a+1, i->id);
	BPLONG(a+5, r.min.x);
	BPLONG(a+9, r.min.y);
	BPLONG(a+13, r.max.x);
	BPLONG(a+17, r.max.y);
	BPLONG(a+21, ndata);
	BPLONG(a+25, 60);
	memmove(a+29, fn, strlen(fn));
	a[29+strlen(fn)] = 0;
	return ndata;

Fail:
	close(fd);
	return loadimage(i, r, data, ndata);
}


int
updatesniffsrc(Rectangle r, uchar *data, int ndata)
{
	char cbuf[20]; /* chan string buf */
	Image *ximg; 
	char *s;
	int fd, m, n;
//	vlong t1, t2, t3, t4, t5;


//	fprint(2, "fxterm [%d]: opening %s for writing\n", getpid(), sniffsrc);	
	fd = open(sniffsrc, OWRITE);
	if(fd < 0){
		fprint(2, "error opening %s for writing: %r\n", sniffsrc);	
		return -1;
	}

//	t2 = nsec();
	n = fprint(fd, "%11s %11d %11d %11d %11d ",
		chantostr(cbuf, dmemscreen->chan), r.min.x, r.min.y, r.max.x, r.max.y);
	if(n != 60){
		fprint(2, "error writing %s: %r\n", sniffsrc);	
		goto Fail;
	}
//	t3 = nsec();
	n = write(fd, data, ndata);
	if(n != ndata){
		fprint(2, "error writing %s: %r\n", sniffsrc);	
		goto Fail;
	}
//	fprint(2, "fxterm [%d]: closing %s\n", getpid(), sniffsrc);	
	close(fd);
	fd = -1;
	
//	fprint(2, "%dx%d mkxfer %,ld imghdr %,ld imgwrite %,ld xdraw %,ld  total %,ld\n",
//		Dx(r), Dy(r), t2-t1, t3-t2, t4-t3, t5-t4, t5-t1);
	return 0;

Fail:
	close(fd);
	return -1;
}

/* called with drawlock locked */
static void
flushgscreen(Rectangle r)
{
	int pid;
	long n;

//	fprint(2, "fxterm: flushgscreen(): %R\n", r);

	if(!rectinrect(r, gscreen->r)){
		werrstr("flushgscreen: bad rectangle");
		return;
	}
	memimagedraw(dmemscreen, r, gscreen, r.min, nil, ZP, S);
	if(dmemscreen->depth % 8 != 0){
		r.min.x = pxquantize(r.min.x, dmemscreen->depth, 0);
		r.max.x = pxquantize(r.max.x, dmemscreen->depth, 1);
	}
	if(sniffctlfd >= 0 && sniffsrc != nil){
		n = unloadmemimage(dmemscreen, dmemscreen->r, pixelbuf, pixelbufsize);
		if(updatesniffsrc(dscreen->r, pixelbuf, n) == 0)
			return;
	}
	n = unloadmemimage(dmemscreen, r, pixelbuf, pixelbufsize);
	loadimage(dscreen, r, pixelbuf, n);
	draw(screen, rectaddpt(r, screen->r.min), dscreen, nil, r.min);
}

/* called with drawlock locked */
void
flushmemscreen(Rectangle r)
{
	static Rectangle cldr;	/* Cursor Last Draw Rect */
	Rectangle cr;		/* cursor rect */
	Rectangle dr;

	if(rectXrect(r, gscreen->r) == 1)
		flushgscreen(r);

/*
	cr = cursorrect();
	if(rectXrect(cr, r) == 1){
		if(!eqrect(cldr, ZR))
			flushgscreen(cldr);
		if(!eqpt(cursorpos, offscreen)){
			rectclip(&cr, gscreen->r);
			draw(screen, rectaddpt(cr, screen->r.min), cursorimg, cursormask, cursorimg->r.min);
			cldr = cr;
		}
	}
*/
	flushimage(display, 1);
}
