#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include <mouse.h>
#include <thread.h>
#include "../../../include/fxterm.h"
#include "common.h"

typedef struct Job Job;
typedef struct Worker Worker;
typedef struct PSample PSample;

struct Job {
	Rectangle rect;
	Jobfunc   func;
};

struct Worker {
	int       id;
	Channel  *cjob;  /* chan(Job) */
};

struct PSample {
	int d;    /* pixel delta */
	int w;    /* weight */
};

PSample blurweights[5] = {
  {-2,  16 },
  {-1,  8 },
  { 0,  4 },
  {+1,  8 },
  {+2,  16 },
};

int          debug;
Postfxframe *frame;          /* post fx data to be processed */
Memdata      frameimagedata; /* points to frame->data */
Memimage    *frameimage;     /* allocated using frameimagedata */
Memdata      infoimagedata;  /* points to frame->usrdata */
Memimage    *infoimage;      /* associates pixels with windows and flags */
Memimage    *brushimg;       /* 1x1 replicated image for memdraw calls */
ulong       *brushcol;       /* points to the pixel data of brushimg */
Worker      *workers;        
int          nworkers;       /* number of worker procs */
Channel     *cdone;          /* chan(ulong) */
int          activewindowid;
int          menuwindowid;

double
clampd(double val, double min, double max)
{
	if(val > max)
		return max;
	else if(val < min)
		return min;
	return val;
}

int
clampi(int val, int min, int max)
{
	if(val > max)
		return max;
	else if(val < min)
		return min;
	return val;
}

ulong
inputpixel(Point pt)
{
	ulong c, *cp;

	cp = (ulong*)frame->data;
	c = cp[pt.y*Dx(frame->rect)+pt.x];

	return c;
}

ulong
luma(uint p)
{
	uchar *cp; /* pixel channel pointer */

	cp = (uchar*)&p;
	return (cp[R]<<1+cp[R]+cp[G]<<2+cp[B])>>3;
}

void
resize(Memimage *dst, Rectangle r, Memimage *src, Rectangle sr)
{
	int nflag;
	Point sp, dp;
	Point _sp, qp;
	Point ssize, dsize;
	uchar *pdst0, *pdst, *psrc0, *psrc;
	ulong s00, s01, s10, s11;
	int tx, ty, bpp, bpl;

	nflag = 0;
	ssize = subpt(subpt(sr.max, sr.min), Pt(1,1));
	dsize = subpt(subpt(r.max, r.min), Pt(1,1));
	pdst0 = byteaddr(dst, r.min);
	bpp = src->depth/8;
	bpl = src->width*sizeof(int);

	qp = Pt(0, 0);
	if(dsize.x > 0)
		qp.x = (ssize.x<<12)/dsize.x;
	if(dsize.y > 0)
		qp.y = (ssize.y<<12)/dsize.y;

	_sp.y = sr.min.y<<12;
	for(dp.y=0; dp.y<=dsize.y; dp.y++){
		sp.y = _sp.y>>12;
		ty = _sp.y&0xFFF;
		if(nflag)
			ty = ty << 1 & 0x1000;
		pdst = pdst0;
		sp.x = sr.min.x;
		psrc0 = byteaddr(src, sp);
		_sp.x = 0;
		for(dp.x=0; dp.x<=dsize.x; dp.x++){
			sp.x = _sp.x>>12;
			tx = _sp.x&0xFFF;
			if(nflag)
				tx = tx << 1 & 0x1000;
			psrc = psrc0 + sp.x*bpp;
			s00 = (0x1000-tx)*(0x1000-ty);
			s01 = tx*(0x1000-ty);
			s10 = (0x1000-tx)*ty;
			s11 = tx*ty;
			switch(bpp){
			case 4:
				pdst[3] = (s11*psrc[bpl+bpp+3] + 
					   s10*psrc[bpl+3] + 
					   s01*psrc[bpp+3] +
					   s00*psrc[3]) >>24;
			case 3:
				pdst[2] = (s11*psrc[bpl+bpp+2] + 
					   s10*psrc[bpl+2] + 
					   s01*psrc[bpp+2] +
					   s00*psrc[2]) >>24;
				pdst[1] = (s11*psrc[bpl+bpp+1] + 
					   s10*psrc[bpl+1] + 
					   s01*psrc[bpp+1] +
					   s00*psrc[1]) >>24;
			case 1:
				pdst[0] = (s11*psrc[bpl+bpp] + 
					   s10*psrc[bpl] + 
					   s01*psrc[bpp] +
					   s00*psrc[0]) >>24;
			}
			pdst += bpp;
			_sp.x += qp.x;
		}
		pdst0 += dst->width*sizeof(int);
		_sp.y += qp.y;
	}
}

void
blur(Memimage *src, Rectangle rect, Memimage *dst, int vertical)
{
	/* variable suffixes:
	 *  d = dst
	 *  s = src
	*/
	ulong *dss, *dsd;     /* data start pointers */
	ulong *des, *ded;     /* data end pointers */
	ulong *rss, *rsd;     /* rect line start pointers */
	ulong *lss;           /* line start pointers */
	ulong *les;           /* line end pointers */
	ulong *ps, *pd, *px;  /* pixel pointers */
	uchar *cs, *cd, *cx;  /* channel pointers */
	int wd, hd;           /* image width / height */
	int xd, yd;           /* current pixel x/y coordinates */
	int npx;              /* number of pixels to process */
	int rd, gd, bd;
	int d;
	int w;
	int i;

	wd = Dx(dst->r);
	hd = Dy(dst->r);
	xd = rect.min.x;
	yd = rect.min.y;
	dsd = (ulong*)byteaddr(dst, ZP);
	dss = (ulong*)byteaddr(src, ZP);
	ded = dsd + wd*hd - 1;
	des = dss + wd*hd - 1;
	rsd = (ulong*)byteaddr(dst, rect.min);
	rss = (ulong*)byteaddr(src, rect.min);
	lss = (ulong*)byteaddr(src, Pt(0, rect.min.y));
	les = (ulong*)byteaddr(src, Pt(rect.max.x-1, rect.min.y));
	ps = rss;
	pd = rsd;
	npx = Dx(rect)*Dy(rect);
	while(npx--){
		cs = (uchar*)ps;
		cd = (uchar*)pd;

		rd = gd = bd = 0;
		for(i = 0; i < nelem(blurweights); i++){
			d = blurweights[i].d;
			w = blurweights[i].w;
			if(vertical){
				px = ps + d*wd;
				if(px < dss || px > des)
					continue;
			} else {
				px = ps + d;
				if(px < lss || px > les)
					continue;
			}
			cx = (uchar*)px;
			rd += cx[R] / w;
			gd += cx[G] / w;
			bd += cx[B] / w;
		}
		if(rd > 255)
			rd = 255;
		if(gd > 255)
			gd = 255;
		if(bd > 255)
			bd = 255;
		cd[R] = rd;
		cd[G] = gd;
		cd[B] = bd;

		if(xd == rect.max.x-1){
			xd = rect.min.x;
			yd++;
			rss += wd;
			rsd += wd;
			lss += wd;
			les += wd;
			ps = rss;
			pd = rsd;
		} else {
			xd++;
			ps++;
			pd++;
		}
	}
}

static void
workerproc(void *arg)
{
	Worker *self, *w;
	Job j;
	vlong t1, t2;

	self = arg;
	threadsetname("workerproc %d", self->id);
	for(;;){
		t1 = nsec();
		recv(self->cjob, &j);
		t2 = nsec();
//		fprint(2, "%s: worker %d was idle for %d ms\n", pname, self->id, (t2-t1)/1000/1000);
		j.func(j.rect);
		sendul(cdone, self->id);
	}
}

void 
work(Rectangle r, Jobfunc func)
{
	Job job;
	int nj; /* number of jobs */
	int dh;
	int i, m, n;

	nj = 0;
	job.func = func;
	n = Dy(r) / nworkers;
	m = Dy(r) % nworkers;
	job.rect = r;
	job.rect.max.y = job.rect.min.y;
	for(i = 0; i < nworkers; i++){
		dh = n;
		if(m > 0){
			dh++;
			m--;
		}
		if(dh == 0)
			break;
		job.rect.min.y = job.rect.max.y;
		job.rect.max.y = job.rect.min.y + dh;
		send(workers[i].cjob, &job);
		nj++;
	}
	while(nj--)
		recvul(cdone);
}

static void
commonsetup(void)
{
	char* nproc;
	Worker *worker;
	int i;

	nproc = getenv("NPROC");
	nworkers = atoi(nproc);
	free(nproc);
	nworkers = 4;
	frameimagedata.base = (ulong*)frame->data;
	frameimagedata.bdata = (uchar*)frame->data;
	frameimagedata.ref = 0;
	frameimagedata.imref = nil;
	frameimagedata.allocd = 0;
	frameimage = allocmemimaged(frame->rect, XRGB32, &frameimagedata);
	infoimagedata.base = (ulong*)frame->usrdata;
	infoimagedata.bdata = (uchar*)frame->usrdata;
	infoimagedata.ref = 0;
	infoimagedata.imref = nil;
	infoimagedata.allocd = 0;
	infoimage = allocmemimaged(frame->rect, XRGB32, &infoimagedata);
	brushimg = allocmemimage(Rect(0,0,1,1), XRGB32);
	if(brushimg == nil){
		fprint(2, "%s: unable to allocate brushimg: %r\n", pname);
		threadexitsall("allocmemimage");
	}
	brushimg->flags |= Frepl;
	brushimg->clipr = Rect(-0x3FFFFFF, -0x3FFFFFF, 0x3FFFFFF, 0x3FFFFFF);
	brushcol = wordaddr(brushimg, ZP);
	workers = malloc(nworkers * sizeof(Worker));
	for(i = 0; i < nworkers; i++){
		worker = &workers[i];
		worker->id = i;
		worker->cjob = chancreate(sizeof(Job), 1);
		proccreate(workerproc, worker, STACK);
	}
	cdone = chancreate(sizeof(ulong), 1);
}

static ulong
metapixel(Point pt)
{
	ulong c, *cp;

	cp = (ulong*)byteaddr(infoimage, ZP);
	c = cp[pt.y*Dx(infoimage->r)+pt.x];

	return c;
}

void
findactivewindow(void)
{
	Rectangle r;
	ulong c;
	int i;
	Point cp[2];

	if(frame->nwindows == 0){
//		fprint(2, "%s: findactivewindow: no windows\n", pname);
		activewindowid = 0;
		menuwindowid = 0;
		return;
	}
	c = inputpixel(frame->windows[0].min);
	if(c == Criomenuborder){ /* rio menu open */
//		fprint(2, "%s: findactivewindow: menu open...\n", pname);
		menuwindowid = (metapixel(frame->windows[0].min) >> 8);
//		fprint(2, "%s: findactivewindow: menuwindowid: %x\n", pname, menuwindowid);
		if(frame->nwindows > 1){
			r = frame->windows[1];
			cp[0] = r.min;
			cp[1] = r.max;
			for(i = 0; i < 2; i++){
				c = inputpixel(cp[i]);
				if(c == Crioborder){
					activewindowid = (metapixel(cp[i]) >> 8);
//					fprint(2, "%s: findactivewindow: activewindowid: %x\n", pname, activewindowid);
					return;
				}
			}
		}
	}
	else if(c == Crioborder){
		activewindowid = (metapixel(frame->windows[0].min) >> 8);
//		fprint(2, "%s: findactivewindow: menu closed. activewindowid: %x\n", pname, activewindowid);
		menuwindowid = 0;
	}
	else if(c == Criolightborder){
//		fprint(2, "%s: findactivewindow: no current\n", pname);
		activewindowid = 0;
		menuwindowid = 0;
	}
}

void
mainloop(void)
{
	char buf[4096];
	int sd; /* setup done */
	int n;
	vlong t1, t2;

	if(memimageinit() != 0)
		sysfatal("memimageinit: %r");
	sd = 0;
	for(;;){
		n = read(0, buf, sizeof(buf));
		if(n < 0){
			fprint(2, "%s: read error: %r\n", pname);
			threadexitsall("read");
		}
		if(n == 0){
			fprint(2, "%s: read eof\n", pname);
			threadexitsall("eof");
		}
		if(buf[0] == 'f'){
			frame = (Postfxframe*)strtoll(buf+2, nil, 16);
			if(sd == 0){
				frame->windowsdirty = 1;
				commonsetup();
				shadersetup();
				sd = 1;
			}
			t1 = nsec();
			processframe();
			t2 = nsec();
//			fprint(2, "%s: processframe time: %,d\n", pname, t2-t1);
		}
		write(1, buf, n);
	}
}
