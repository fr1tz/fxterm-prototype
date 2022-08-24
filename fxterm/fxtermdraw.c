/* fxtermdraw.c - fxterm architecture independent draw layer */

#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "compat.h"
#include "error.h"
#include "screen.h"
#include "fxterm.h"

int			sniffctlfd; /*  fd for accessing/dev/draw/sniff (if it's there) */
char		*sniffsrc; /* filename used as source for sniff */

Memimage  *drawscreen;        /* the image used by devdraw as screen */
Rectangle  drawrect;          /* draw area on the actual screen */
Rectangle  drawflushrect;          /* draw area on the actual screen */
int        drawpixelscalex;   /* how much a pixel will be scaled horizontally */
int        drawpixelscaley;   /* how much a pixel will be scaled vertically */
Memimage  *drawcursorimg;     /* used if scaling is enabled and postfx is disabled */
Memimage  *pscreen;   
Memdata    pscreendata;        

int             postfxdebug = 0;
Gfxctl       gfxctl;
Postfxframehdr *postfxhdr;
uchar          *postfxusrdata;
int             postfxusrdatasize;

static Memimage *ascreen;  /* image from attachscreen() */
static Memimage *iscreen;  /* internal screen, used if we're scaling or doing postfx */
static Memimage *zscreen;  /* used if scaling is active */

static int hwscreen;    /* 1 if attachscreen() did not report a softscreen */
static int doscale;     /* scaling enabled? */
static int dopostfx;    /* postfx enabled? */

static void
postfxnote(void*, char *msg)
{
	fprint(2,"%d: got note: %s\n", getpid(), msg);
	exits(nil);
}

static int
null(void*)
{
	return 0;
}

static void
postfxgenerator(void)
{
	char buf[128];
	long l;
	int i, n, fd, y;

//	fprint(2,"postfx generator process pid is %d\n", getpid());

	for(;;){
		pfxlock();
		gfxctl.framecounter++;
		fd = gfxctl.pipe[0];
		dlock();
		if(gfxctl.continuous == 0
		&& gfxctl.framecounter > 1
		&& eqrect(drawflushrect, ZR) == 1
		&& eqpt(mousexy(), postfxhdr->mousexy) == 1){
			dunlock();
			gfxctl.timeout = nsec() + 1000000000;
			pfxunlock();
			sleep(8);
			continue;
		}
		postfxhdr->flags = 0;
		postfxhdr->nsec = nsec();
		if(gfxctl.framecounter == 1)
			postfxhdr->dirtr = drawscreen->r;
		else{
			postfxhdr->dirtr = drawflushrect;
			rectclip(&postfxhdr->dirtr, drawscreen->r);
		}
		postfxhdr->cursor = cursor;
		postfxhdr->mousexy = mousexy();
		postfxhdr->nwindows = drawgetriowindows(postfxhdr->windows, 1024, &postfxhdr->windowsdirty);
		memimagedraw(pscreen, pscreen->r, drawscreen, ZP, nil, ZP, S);
//		memcpy(postfxhdr->data, byteaddr(drawscreen, drawscreen->r.min), postfxhdr->ndata);
		drawflushrect = ZR;
		dunlock();
		pfxunlock();
		if(fprint(fd, "f %p\n", postfxhdr) < 0){
			fprint(2,"%d: write error: %r\n", getpid());
			exits("write");
		}
		if(read(fd, buf, sizeof(buf)) < 0){
			fprint(2,"%d: read error: %r\n", getpid());
			exits("read");
		}
		pfxlock();
		gfxctl.timeout = nsec() + 1000000000;
		flushpscreen(postfxhdr->dirtr);
		pfxunlock();
		sleep(8);
	}
}

static void
postfxtimeout(void)
{
	fprint(2,"postfx timeout process pid is ç%d\n", getpid());

	for(;;){
		pfxlock();
		if(gfxctl.enabled && nsec() >= gfxctl.timeout){
			gfxctl.enabled = 0;
			rendwakeup(&gfxctl.init);
		}
		pfxunlock();
		sleep(1000);
	}
}

static void
execpostfxshader(void)
{
	char *name, *argv[4];
	int fd;

	name = "/bin/rc";
	argv[0] = name;
	argv[1] = "-c";
	argv[2] = gfxctl.shader;
	argv[3] = nil;
	dup(gfxctl.pipe[1], 0);
	dup(gfxctl.pipe[1], 1);
	exec(name, argv);
}

static void
processgfxctl(void)
{
	Waitmsg *w;
	int pid;
	int i;
	long l;

	pfxlock();
	if(postfxhdr != nil){
		segdetach(postfxhdr->usrdata);
		segdetach(postfxhdr);
		postfxhdr = nil;
	}
	for(i = 0; i < 2; i++)
		if(gfxctl.pipe[i] > 0){
			close(gfxctl.pipe[i]);
			gfxctl.pipe[i] = -1;
		}
	if(gfxctl.gpid > 0){
//		fprint(2, "telling postfx generator process %d to die...\n", gfxctl.gpid);
		postnote(PNPROC, gfxctl.gpid, "die");
		gfxctl.gpid = -1;
	}
	if(gfxctl.tpid > 0){
//		fprint(2, "telling postfx timeout process %d to die...\n", gfxctl.tpid);
		postnote(PNPROC, gfxctl.tpid, "die");
		gfxctl.tpid = -1;
	}
	deletescreenimage();
	resetscreenimage();
	if(gfxctl.enabled == 0){
		pfxunlock();
		return;
	}
/*
	if(gfxctl.ppid > 0){
		fprint(2, "telling postfx processor process %d to die...\n", gfxctl.ppid);
		postnote(PNPROC, gfxctl.ppid, "die");
		gfxctl.ppid = -1;
	}
*/

	if(pipe(gfxctl.pipe) < 0)
		panic("unable to create postfx pipe");

	l = bytesperline(drawscreen->r, drawscreen->depth)*Dy(drawscreen->r);
	postfxhdr = (Postfxframehdr*)segattach(0, "shared", 0, sizeof(Postfxframehdr));
	postfxhdr->flags = 0;
	postfxhdr->chan = drawscreen->chan;
	postfxhdr->rect = drawscreen->r;
	postfxhdr->data = pscreendata.bdata;
	postfxhdr->ndata = l;
	postfxhdr->usrdata = (uchar*)segattach(0, "shared", 0, l);
	postfxhdr->nusrdata = l;


	gfxctl.framecounter = 0;
	gfxctl.timeout = nsec() + 15000000000;

	fprint(2, "%d: gfxctlproc: creating postfx generator process\n", getpid());
	pid = rfork(RFPROC|RFMEM);
	switch(pid){
	case -1:
		panic("can't spawn postfx generator: %r");
	case 0:
		notify(postfxnote);
		postfxgenerator();
		sysfatal("postfxgenerator returned");
	default:
		gfxctl.gpid = pid;
	}
//	fprint(2, "%d: gfxctlproc: creating postfx timeout process\n", getpid());
	pid = rfork(RFPROC|RFMEM);
	switch(pid){
	case -1:
		panic("can't spawn postfx timeout: %r");
	case 0:
		notify(postfxnote);
		postfxtimeout();
		sysfatal("postfxtimeout returned");
	default:
		gfxctl.tpid = pid;
	}
//	fprint(2, "%d: gfxctlproc: creating postfx processor process\n", getpid());
	pid = rfork(RFPROC|RFNAMEG|RFFDG);
	switch(pid){
	case -1:
		panic("can't spawn postfx processor: %r");
	case 0:
		close(gfxctl.pipe[0]);
		execpostfxshader();
		sysfatal("execpostfxshader returned");
	default:
		pid = pid;
	}
	pfxunlock();
}

void
fxtermdrawinit(void)
{
	gfxctl.shader = nil;
	gfxctl.gpid = -1;
	switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
	case -1:
		panic("can't create gfxctl process: %r");
	case 0:
		for(;;){
			rendsleep(&gfxctl.init, null, 0);
			processgfxctl();
		}
	}
}
 
static void
hscale(Memimage *src, Rectangle sr, Memimage *dst, Point dpt, int xscale)
{
	ulong *slp; /* pointer to line in src */
	ulong *dlp; /* pointer to line in dst */
	ulong *sp;  /* pointer to pixel in src */
	ulong *spe; /* pointer to last pixel in src line */
	ulong *dp;  /* pointer to pixel in dst */
	int i, n;

	for(i = 0; i < Dy(sr); i++){
		slp = wordaddr(src, Pt(sr.min.x, sr.min.y+i));
		dlp = wordaddr(dst, Pt(dpt.x, dpt.y+i));
		sp = slp;
		dp = dlp;
		spe = slp + Dx(sr);
		for(sp = slp; sp <= spe; sp++){
			n = xscale;
			while(n--) {
				*dp = *sp;
				dp++;
			}
		}
	}
}

static void
vscale(Memimage *src, Rectangle sr, Memimage *dst, Point dpt, int yscale)
{
	int linesize;
	ulong *slp; /* pointer to line in src */
	ulong *dlp; /* pointer to line in dst */
	ulong *dp;  /* pointer to pixel in dst */
	int i, n;

	linesize = Dx(sr)*sizeof(ulong);
	for(i = Dy(sr)-1; i >= 0; i--){
		slp = wordaddr(src, Pt(sr.min.x, sr.min.y+i));
		n = yscale;
		while(n--) {
			dlp = wordaddr(dst, Pt(dpt.x, dpt.y+i*yscale+n));
			memcpy(dlp, slp, linesize);
		}
	}
}

/* Called with drawlock unlocked */
void
setdrawpixelscale(int x, int y)
{
	dlock();
	drawpixelscalex = x;
	drawpixelscaley = y;
	dunlock();
	deletescreenimage();
	resetscreenimage();
}

void
setdrawcursor(Cursor *c)
{
	Rectangle r;
	uchar mask[2*16];
	Memimage* clrmask;
	Memimage* setmask;
	int i;

	dlock();
	memmove(&cursor, c, sizeof(Cursor));
	if(dopostfx){


	}
	else if(doscale){
		r = Rect(0, 0, 16, 16);
		clrmask = allocmemimage(r, GREY1);
		loadmemimage(clrmask, r, c->clr, 2*16);
		setmask = allocmemimage(r, GREY1);
		loadmemimage(setmask, r, c->set, 2*16);
	
		memfillcolor(drawcursorimg, 0x00000000);
		memimagedraw(drawcursorimg, r, memwhite, ZP, clrmask, ZP, SoverD);
		memimagedraw(drawcursorimg, r, memblack, ZP, setmask, ZP, SoverD);
		freememimage(clrmask);
		freememimage(setmask);
	}
	else
		gsetcursor(c);
	dunlock();
	renderdrawcursor();
}

static Rectangle
drawcursorrect(void)
{
	Point xy;
	Rectangle r;

	xy = mousexy();
	r.min.x = xy.x + cursor.offset.x;
	r.min.y = xy.y + cursor.offset.y;
	r.max.x = r.min.x + 16;
	r.max.y = r.min.y + 16;
	return r;
}

/* Called with drawlock unlocked */
void 
renderdrawcursor(void)
{
	dlock();
	if(waserror()){
		dunlock();
		nexterror();
	}
	if(!doscale && !dopostfx){
		cursoroff();
		cursoron();
	}
	else if(doscale){
		flushdrawscreen(drawcursorrect());
	}
	dunlock();
	poperror();
}


/* Called with drawlock locked, gfxctl locked */
void
deletedrawscreen(void)
{
	/* devdraw has freed drawscreen */
	if(drawscreen == ascreen)
		freememimage(iscreen);
	else
		freememimage(ascreen);
	if(pscreendata.base != nil){
		segdetach(pscreendata.base);
		pscreendata.base = nil;
		pscreendata.bdata = nil;
	}
	freememimage(pscreen);
	freememimage(zscreen);
	drawscreen = nil;
	ascreen = nil;
	iscreen = nil;
	pscreen = nil;
	zscreen = nil;
	freememimage(drawcursorimg);
	drawcursorimg = nil;
}

/* Called with drawlock locked, gfxctl locked */
void
resetdrawscreen(void)
{
	int mod, pad1, pad2, w, h;
	int depth, width, softscreen;
	ulong chan, l;
	Memdata *md;
	int pid;
	Rectangle r;

	if(drawpixelscalex <= 0)
		drawpixelscalex = 1;
	if(drawpixelscaley <= 0)
		drawpixelscaley = 1;

	if((md = attachscreen(&r, &chan, &depth, &width, &softscreen)) == nil){
		error("attachscreen");
		return;
	}
	assert(md->ref > 0);
	if((ascreen = allocmemimaged(r, chan, md)) == nil){
		error("failed to alloc ascreen");
		if(--md->ref == 0 && md->allocd)
			free(md);
		return;
	}
	ascreen->width = width;
	ascreen->clipr = r;
	hwscreen = (softscreen == 0);
	doscale = (drawpixelscalex != 1 || drawpixelscaley != 1);
	dopostfx = gfxctl.enabled;
	if(doscale || dopostfx){
		cursoroff();
		if(drawpixelscalex == 1){
			w = Dx(r);
			drawrect.min.x = r.min.x;
			drawrect.max.x = r.max.x;
		}
		else{
			mod = Dx(r) % drawpixelscalex;
			w = (Dx(r)-mod)/drawpixelscalex;
			if(mod & 1){
				pad1 = (mod-1)/2;
				pad2 = pad1 + 1;
			}
			else
				pad1 = pad2 = mod/2;
			drawrect.min.x = r.min.x + pad1;
			drawrect.max.x = r.max.x - pad2;
		}
		if(drawpixelscaley == 1){
			h = Dy(r);
			drawrect.min.y = r.min.y;
			drawrect.max.y = r.max.y;
		}
		else{
			mod = Dy(r) % drawpixelscaley;
			h = (Dy(r)-mod)/drawpixelscaley;
			if(mod & 1){
				pad1 = (mod-1)/2;
				pad2 = pad1 + 1;
			}
			else
				pad1 = pad2 = mod/2;
			drawrect.min.y = r.min.y + pad1;
			drawrect.max.y = r.max.y - pad2;
		}
		iscreen = allocmemimage(Rect(0,0,w,h), XRGB32);
		if(iscreen == nil)
			error("no memory for iscreen memimage");
		l = bytesperline(iscreen->r, iscreen->depth)*Dy(iscreen->r);
		pscreendata.base = (ulong*)segattach(0, "shared", 0, l);
		pscreendata.bdata = (uchar*)pscreendata.base;
		pscreendata.ref = 0;
		pscreendata.imref = nil;
		pscreendata.allocd = 0;
		pscreen = allocmemimaged(iscreen->r, XRGB32, &pscreendata);
		if(pscreen == nil)
			error("no memory for pscreen memimage");	
		if(doscale){
			zscreen = allocmemimage(Rect(0, 0, Dx(drawrect), Dy(drawrect)), XRGB32);
			if(zscreen == nil)
				error("no memory for zscreen memimage");
			if(!dopostfx)
				drawcursorimg = allocmemimage(Rect(0,0,16,16), XRGB32);
		}
		drawscreen = iscreen;
	}
	else{
		drawscreen = ascreen;
		drawrect = r;
	}
}

/* called with drawlock possibly unlocked */
void
flushdrawscreen(Rectangle r)
{
	static Rectangle cldr;    /* Cursor Last Draw Rect */

	char buf[128];
	Memimage* img;            /* multi-purpose image */
	Rectangle cr;             /* cursor rect */
	int nbits;

	if(hwscreen && !doscale && !dopostfx)
		return;

	if(rectclip(&r, drawscreen->r) == 0)
		return;

	img = drawscreen;
	if(dopostfx){
		if(eqrect(drawflushrect, ZR) == 1)
			drawflushrect = r;
		else
			combinerect(&drawflushrect, r);
		return;
	}
	else if(doscale)
	{
		memimagedraw(pscreen, pscreen->r, drawscreen, ZP, nil, ZP, S);

		cr = drawcursorrect();
		if(rectXrect(cr, r) == 1){
			if(!eqrect(cldr, ZR)){
				memimagedraw(pscreen, cldr, img, cldr.min, nil, ZP, S);
				combinerect(&r, cldr);
			}
			memimagedraw(pscreen, cr, drawcursorimg, ZP, nil, ZP, SoverD);
			combinerect(&r, cr);
			cldr = cr;
		}
		flushpscreen(r);
	}
	else{
		r = rectaddpt(r, drawrect.min);
		memimagedraw(gscreen, r, img, r.min, nil, ZP, S); 
		flushmemscreen(r);	
	}
}

/* called with drawlock possibly unlocked */
void
flushpscreen(Rectangle r)
{
	Memimage *img;
	Rectangle ar;             /* multi-purpose rect */
	int i, n;

	img = pscreen;
	if(doscale){
		rectclip(&r, img->r);
		ar.min.x = r.min.x * drawpixelscalex;
		ar.min.y = r.min.y * drawpixelscaley;
		ar.max.x = ar.min.x + Dx(r) * drawpixelscalex;
		ar.max.y = ar.min.y + Dy(r) * drawpixelscaley;
		if(drawpixelscalex > 1){
			hscale(img, r, zscreen, ar.min, drawpixelscalex);
			i = Dy(r);
			r = ar;
			r.max.y = r.min.y + i;
			img = zscreen;
		}
		if(drawpixelscaley > 1){
			vscale(img, r, zscreen, ar.min, drawpixelscaley);
			r = ar;
		}
		img = zscreen;
	}
	else {
		ar = rectaddpt(r, drawrect.min);
		rectclip(&ar, drawrect);
		r = rectsubpt(ar, drawrect.min);
	}

	r = rectaddpt(r, drawrect.min);
	memimagedraw(gscreen, r, img, r.min, nil, ZP, S); 
	flushmemscreen(r);	
}

void
dlock(void)
{
	qlock(&drawlock);
}

int
candlock(void)
{
	return canqlock(&drawlock);
}

void
dunlock(void)
{
	qunlock(&drawlock);
}

void
pfxlock(void)
{
	lock(&gfxctl);
}

void
pfxunlock(void)
{
	unlock(&gfxctl);
}
