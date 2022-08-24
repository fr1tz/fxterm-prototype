#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include <mouse.h>
#include <thread.h>
#include <geometry.h>
#include "../../../include/fxterm.h"
#include "common.h"

char *pname = "neon";

enum { /* Constants */
	Glowimagescale = 4,
};

Memimage   *overlayimage;
Memimage   *image1;
Memimage   *image2;
Memimage   *image3;
Memimage   *image4;
Memimage   *image5;
Memimage   *scalemap;

static Point
l2spt(Point pt)
{
	ulong off, *ps;
	int w, x, y;

	ps = (ulong*)byteaddr(scalemap, pt);
	off = *ps;
	w = Dx(image2->r);
	x = off % w;
	y = (off - x) / w;
//	fprint(2, "l2spt: %P -> offset %uld -> %P\n", pt, off, Pt(x,y));
	return Pt(x, y);
}

static Rectangle
l2srect(Rectangle r)
{
	Rectangle r2, rs;
	ulong *ps;

	rs = r;
	r.max.x--;
	r.max.y--;
	r2.min = l2spt(r.min);
	r2.max = l2spt(r.max);
	r2.max.x++;
	r2.max.y++;
//	fprint(2, "l2srect: %R -> %R\n", rs, r2);
	return r2;
}

static void
neonize(Memimage *s, Rectangle r, Memimage *i, Memimage *m, Memimage *f, Memimage *d)
{
	/* arguments / variable suffixes:
	 * s = source image
	 *  r = source rect
	 *  i = infoimage
	 *  m = scalemap
	 *  f = full-sized output
	 *  d = downscaled output
	*/
	ulong *dd;                      /* start of image2 pixel data */
	ulong *lss, *lsi, *lsm, *lsf;         /* line start pointers */
	ulong *ps, *pi, *pm, *pf, *pd;  /* pixel pointers */
	uchar *cs, *ci, *cf, *cd;       /* channel pointers */
	int w;                         /* source image width */
	int x, y;                     /* image1 current pixel x/y coordinates */
	int f0;                         /* from infoimage: flags */
	int id;                         /* from infoimage: window id */
	int npx;                        /* number of pixels to process */
	int v;                          /* pixel average value */
	int x0, r0, g0, b0;

	dd = (ulong*)byteaddr(image2, ZP);
	w = Dx(image1->r);
	x = r.min.x;
	y = r.min.y;
	lss = (ulong*)byteaddr(s, r.min);
	lsi = (ulong*)byteaddr(i, r.min);
	lsm = (ulong*)byteaddr(m, r.min);
	lsf = (ulong*)byteaddr(f, r.min);
	ps = lss;
	pi = lsi;
	pm = lsm;
	pf = lsf;
	npx = Dx(r)*Dy(r);
	while(npx--){
		cs = (uchar*)ps;
		ci = (uchar*)pi;
		cf = (uchar*)pf;

		id = (*pi >> 8);
		f0 = (*pi & 0xFF);

		if(f0 == 0)
			*pf = 0x000000;
		else if(f0 & Fborder && *ps == Crioborder)
			*pf = 0xffff00;					
		else if(f0 & Fborder && *ps == Criolightborder)
			*pf = 0x880000;
		else if(f0 & Fborder && *ps == Criomenuborder)
			*pf = 0xffff00;
		else if(f0 & Fborder && *ps == Crioselectborder)
			*pf = 0xff0000;
		else {
			cf[R] = cs[R];				
			cf[G] = cs[G];				
			cf[B] = cs[B];
			v = (cf[R]+cf[G]+cf[B])/3;
			if(v > 150){
				cf[R] = v;
				cf[G] = v/2;
				cf[B] = 0;
			}
			else if(v > 50){
				cf[R] = 0;
				cf[G] = v/2;
				cf[B] = v;
			}
			else{
				cf[R] = v;
				cf[G] = v;
				cf[B] = v;
			}
		}

		pd = dd + *pm;
		cd = (uchar*)pd;
		if(1){
			cd[X]++;
			r0 = cd[R] + cf[R]/2;
			if(r0 > 255)
				r0 = 255;
			cd[R] = r0;
			g0 = cd[G] + cf[G]/2;
			if(g0 > 255)
				g0 = 255;
			cd[G] = g0;
			b0 = cd[B] + cf[B]/2;
			if(b0 > 255)
				b0 = 255;
			cd[B] = b0;
		}
		if(0){
			cd[X]++;
			x0 = cd[X];
			r0 = cd[R]/x0*(x0-1) + cf[R]/x0;
			if(r0 > 255)
				r0 = 255;
			cd[R] = r0;
			g0 = cd[G]/x0*(x0-1) + cf[G]/x0;
			if(g0 > 255)
				g0 = 255;
			cd[G] = g0;
			b0 = cd[B]/x0*(x0-1) + cf[B]/x0;
			if(b0 > 255)
				b0 = 255;
			cd[B] = b0;
		}

		if(x == r.max.x-1){
			x = r.min.x;
			y++;
			lss += w;
			lsi += w;
			lsm += w;
			lsf += w;
			ps = lss;
			pi = lsi;
			pm = lsm;
			pf = lsf;
		} else {
			x++;
			ps++;
			pi++;
			pm++;
			pf++;
		}
	}
}

static void
neonfinish(Memimage *neonized, Memimage *glow, Rectangle r, Memimage *dst1)
{
	/* variable suffixes:
	 *	n = neonized
	 *  g = glow
	 *  1 = dst1
	*/
	ulong *lsn, *lsg, *ls1;   /* line start pointers */
	ulong *pn, *pg, *p1;      /* pixel pointers */
	uchar *cn, *cg, *c1;       /* channel pointers */
	int w;                    /* image width */
	int x, y;                     /* image1 current pixel x/y coor1inates */
	int f;                          /* from infoimage: flags */
	int id;                         /* from infoimage: window id */
	int npx;                        /* number of pixels to process */
	int v;                          /* pixel average value */
	int r1, g1, b1;

	w = Dx(neonized->r);
	x = r.min.x;
	y = r.min.y;
	lsn = (ulong*)byteaddr(neonized, r.min);
	lsg = (ulong*)byteaddr(glow, r.min);
	ls1 = (ulong*)byteaddr(dst1, r.min);
	pn = lsn;
	pg = lsg;
	p1 = ls1;
	npx = Dx(r)*Dy(r);
	while(npx--){
		cn = (uchar*)pn;
		cg = (uchar*)pg;
		c1 = (uchar*)p1;

		r1 = cn[R]*2 + cg[R];
		g1 = cn[G]*2 + cg[G];
		b1 = cn[B]*2 + cg[B];
		if(r1 > 255)
			r1 = 255;
		if(g1 > 255)
			g1 = 255;
		if(b1 > 255)
			b1 = 255;

		c1[R] = r1;
		c1[G] = g1;
		c1[B] = b1;

		if(x == r.max.x-1){
			x = r.min.x;
			y++;
			lsn += w;
			lsg += w;
			ls1 += w;
			pn = lsn;
			pg = lsg;
			p1 = ls1;
		} else {
			x++;
			pn++;
			pg++;
			p1++;
		}
	}
}

static void
pass1(Rectangle rect)
{
	neonize(frameimage, rect, infoimage, scalemap, frameimage, image2);
}

static void
pass2(Rectangle rect)
{
	blur(image2, rect, image3, 0);
}

static void
pass3(Rectangle rect)
{
	blur(image3, rect, image4, 1);
}

static void
pass4(Rectangle rect)
{
	resize(image5, rect, image4, l2srect(rect));
}

static void
pass5(Rectangle rect)
{
	neonfinish(image1, image5, rect, frameimage);
}

void
processframe(void)
{
	Rectangle r, r2, r3;

	if(!eqrect(frame->dirtr, ZR)){
		frame->dirtr.min.x = frame->dirtr.min.x / Glowimagescale * Glowimagescale;
		frame->dirtr.min.y = frame->dirtr.min.y / Glowimagescale * Glowimagescale;
		frame->dirtr.max.x = (frame->dirtr.max.x/Glowimagescale+1) * Glowimagescale;
		frame->dirtr.max.y = (frame->dirtr.max.y/Glowimagescale+1) * Glowimagescale;
		rectclip(&frame->dirtr, frame->rect);
		r2 = l2srect(frame->dirtr);
		memimagedraw(image2, r2, memtransparent, r2.min, 0, ZP, S);
		work(frame->dirtr, pass1);
/*
		r2.min.x -= 3;
		r2.max.x += 3;
		rectclip(&r2, image2->r);	
		work(r2, pass2);	
		r2.min.y -= 3;
		r2.max.y += 3;
		rectclip(&r2, image2->r);
		work(r2, pass3);
		frame->dirtr = insetrect(frame->dirtr, -12);
		rectclip(&frame->dirtr, frame->rect);
		work(frame->dirtr, pass4);	
		work(frame->dirtr, pass5);	
		if(0){
			r = image2->r;
			memimagedraw(frameimage, r, image2, ZP, 0, ZP, S);
			r.min.y += Dy(image2->r); r.max.y += Dy(image2->r);
			memimagedraw(frameimage, r, image3, ZP, 0, ZP, S);
			r.min.y += Dy(image2->r); r.max.y += Dy(image2->r);
			memimagedraw(frameimage, r, image4, ZP, 0, ZP, S);
			combinerect(&frame->dirtr, Rect(0,0,r.max.x,r.max.y));
		} 
*/
	}
}

static void
createscalemap(void)
{
	ulong *ps, *p2;
	int x, y;
	int x2, y2;

	scalemap = allocmemimage(frame->rect, frame->chan);
	if(scalemap == nil){
		fprint(2, "Unable to allocate scalemap: %r\n");
		threadexitsall("allocmemimage");
	}
	for(y = 0; y < frame->rect.max.y; y++){
		for(x = 0; x < frame->rect.max.x; x++){
			ps = (ulong*)byteaddr(scalemap, Pt(x,y));
			x2 = x/Glowimagescale;
			y2 = y/Glowimagescale;
			p2 = (ulong*)byteaddr(image2, Pt(x2,y2));
			*ps = p2 - (ulong*)byteaddr(image2, ZP);
//			if(x < 10 && y < 10)
//				fprint(2, "%d %d -> %d %d %d\n", x, y, x2, y2, *ps);
		}
	}
}

void
shadersetup(void)
{
	Rectangle r;
	char *filenames[1];
	Memimage *images[1];
	Memimage *tmpimg;
	long l, m, n;
	int fd, i;

/*
	filenames[0] = overlayimagefile;
	for(i = 0; i < 1; i++){
		if(filenames[i] == nil)
			continue;
		fd = open(filenames[i], OREAD);
		if(fd < 0){
			fprint(2, "can't open %s: %r", filenames[i]);
			threadexitsall("open");
		}
		tmpimg = readmemimage(fd);
		if(tmpimg == nil){
			fprint(2, "can't read image %s: %r", filenames[i]);
			threadexitsall("readmemimage");
		}
		close(fd);
		if(tmpimg->chan != XRGB32){
			fprint(2, "%s not XRGB32 image\n", filenames[i]);
			threadexitsall("chan");
		}
		tmpimg->flags |= Frepl;
		tmpimg->clipr = Rect(-0x3FFFFFF, -0x3FFFFFF, 0x3FFFFFF, 0x3FFFFFF);
		images[i] = allocmemimage(frame->rect, frame->chan);
		memimagedraw(images[i], images[i]->r, tmpimg, tmpimg->r.min, nil, ZP, S);
		freememimage(tmpimg);
	}
	overlayimage = images[0];
*/

	image1 = allocmemimage(frame->rect, frame->chan);
	if(image1 == nil){
		fprint(2, "Unable to allocate image1: %r\n");
		threadexitsall("allocmemimage");
	}
	image5 = allocmemimage(frame->rect, frame->chan);
	if(image5 == nil){
		fprint(2, "Unable to allocate image5: %r\n");
		threadexitsall("allocmemimage");
	}
	r.min = frame->rect.min;
	if(frame->rect.max.x % Glowimagescale == 0)
		r.max.x = frame->rect.max.x/Glowimagescale;
	else
		r.max.x = frame->rect.max.x/Glowimagescale+1;
	if(frame->rect.max.y % Glowimagescale == 0)
		r.max.y = frame->rect.max.y/Glowimagescale;
	else
		r.max.y = frame->rect.max.y/Glowimagescale+1;
	image2 = allocmemimage(r, frame->chan);
	if(image2 == nil){
		fprint(2, "Unable to allocate image2: %r\n");
		threadexitsall("allocmemimage");
	}
	image3 = allocmemimage(r, frame->chan);
	if(image3 == nil){
		fprint(2, "Unable to allocate image3: %r\n");
		threadexitsall("allocmemimage");
	}
	image4 = allocmemimage(r, frame->chan);
	if(image4 == nil){
		fprint(2, "Unable to allocate image4: %r\n");
		threadexitsall("allocmemimage");
	}	
	createscalemap();
/*
	fprint(2, "image1 rect: %R\n", image1->r);
	fprint(2, "image2 rect: %R\n", image2->r);
	Point pt;
	pt = Pt(1463, 0); fprint(2, "%P -> %P\n", pt, l2spt(pt));
	pt = Pt(1463, 1); fprint(2, "%P -> %P\n", pt, l2spt(pt));
	pt = Pt(1463, 1041); fprint(2, "%P -> %P\n", pt, l2spt(pt));
*/
}

void
usage(void)
{
	fprint(2, "usage: neon bgimage\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char* argv[])
{
	ARGBEGIN{
	default:
		usage();
	}ARGEND
	mainloop();
	threadexitsall(nil);
}
