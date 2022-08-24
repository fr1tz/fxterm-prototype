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

char *pname = "1bpp";

Memimage* image1;
int threshold[4][4];
uint darkcol;
uint lightcol;

/* deaa = de-anti-alias */
static void
deaa(Memimage *S, Memimage *I, Rectangle r, Memimage *D)
{
	/* arguments / variable suffixes:
	*	S = source
	*	I = info image
	*	D= destination
	*/
	ulong *lsS, *lsI, *lsD;	/* line start pointers */
	ulong *pS, *pI, *pD;	/* pixel pointers */
	uchar *cS, *cD;		/* channel pointers */
	int w;				/* image width */
	int x, y;			/* image1 current pixel x/y coor1inates */
	int f;			/* from infoimage: flags */
	int id;				/* from infoimage: window id */
	int v;			/* pixel average value */
	int b;
	int d;

	w = Dx(S->r);
	x = r.min.x;
	y = r.min.y;
	lsS = (ulong*)byteaddr(S, r.min);
	lsI = (ulong*)byteaddr(I, r.min);
	lsD = (ulong*)byteaddr(D, r.min);
	pS = lsS;
	pI = lsI;
	pD = lsD;
	while(y < r.max.y){
		cS = (uchar*)pS;
		cD = (uchar*)pD;

		id = (*pI >> 8);
		f = (*pI & 0xFF);

		if(menuwindowid != 0 && id == menuwindowid){
			v = (cS[R]+cS[G]+cS[B])/3;
			if(v > 170)
				*pD = lightcol;
			else
				*pD = darkcol;
		}
		else if(f & Fborder && *pS == Crioborder)
			*pD = darkcol;					
		else if(f & Fborder && *pS == Criolightborder){
			if(x & 1 || y & 1)
				*pD = darkcol;
			else
				*pD = lightcol;;
		}
		else{
			v = (cS[R]+cS[G]+cS[B])/3;

			if(v < threshold[x%4][y%4])
				b = 0;
			else
				b = 255;
/*
			if(f & Fwindow){
				d = b - v;
				if(d > 50)
					b = 0;
//				else if(d < 0 && d > -50)
//					b = 255;

			}
*/
			if(b > 128)
				*pD = lightcol;
			else
				*pD = darkcol;
		}

		if(x == r.max.x-1){
			x = r.min.x;
			y++;
			lsS += w;
			lsI += w;
			lsD += w;
			pS = lsS;
			pI = lsI;
			pD = lsD;
		} else {
			x++;
			pS++;
			pI++;
			pD++;
		}
	}
}

static void
mkbinary(Memimage *S, Memimage *I, Rectangle r, Memimage *D)
{
	/* arguments / variable suffixes:
	*	S = source
	*	I = info image
	*	D= destination
	*/
	ulong *lsS, *lsI, *lsD;	/* line start pointers */
	ulong *pS, *pI, *pD;	/* pixel pointers */
	uchar *cS, *cD;		/* channel pointers */
	int w;				/* image width */
	int x, y;			/* current pixel x/y coordinates */
	int f;			/* from infoimage: flags */
	int id;				/* from infoimage: window id */
	int v;			/* pixel average value */
	int b;
	int d;

	w = Dx(S->r);
	x = r.min.x;
	y = r.min.y;
	lsS = (ulong*)byteaddr(S, r.min);
	lsI = (ulong*)byteaddr(I, r.min);
	lsD = (ulong*)byteaddr(D, r.min);
	pS = lsS;
	pI = lsI;
	pD = lsD;
	while(y < r.max.y){
		cS = (uchar*)pS;
		cD = (uchar*)pD;

		id = (*pI >> 8);
		f = (*pI & 0xFF);

		if(menuwindowid != 0 && id == menuwindowid){
			v = (cS[R]+cS[G]+cS[B])/3;
			if(v > 170)
				*pD = lightcol;
			else
				*pD = darkcol;
		}
		else if(f & Fborder && *pS == Crioborder)
			*pD = darkcol;					
		else if(f & Fborder && *pS == Criolightborder){
			if(x & 1 || y & 1)
				*pD = darkcol;
			else
				*pD = lightcol;;
		}
		else{
			v = (cS[R]+cS[G]+cS[B])/3;

			if(v < threshold[x%4][y%4])
				b = 0;
			else
				b = 255;
/*
			if(f & Fwindow){
				d = b - v;
				if(d > 50)
					b = 0;
//				else if(d < 0 && d > -50)
//					b = 255;

			}
*/
			if(b > 128)
				*pD = lightcol;
			else
				*pD = darkcol;
		}

		if(x == r.max.x-1){
			x = r.min.x;
			y++;
			lsS += w;
			lsI += w;
			lsD += w;
			pS = lsS;
			pI = lsI;
			pD = lsD;
		} else {
			x++;
			pS++;
			pI++;
			pD++;
		}
	}
}

static void
pass1(Rectangle rect)
{
	deaa(frameimage, infoimage, rect, frameimage);
}

static void
pass2(Rectangle rect)
{
	mkbinary(image1, infoimage, rect, frameimage);
}

void
processframe(void)
{
	if(!eqrect(frame->dirtr, ZR)){
		if(frame->windowsdirty)
			findactivewindow();
		work(frame->dirtr, pass1);
	}
}

void
shadersetup(void)
{
	static int t[4][4] = {
		{ 0, 8, 2, 10 },
		{ 12, 4, 14, 6 },
		{ 3, 11, 1, 9 },
		{ 15, 7, 13, 5 },
	};
	int *p1, *p2;
	int n;

	p1 = (int*)threshold;
	p2 = (int*)t;
	n = 16;
	while(n--){
		*p1 = (*p2*255+255) / (4*4);
		p1++;
		p2++;
	}

	image1 = allocmemimage(frame->rect, frame->chan);
	if(image1 == nil){
		fprint(2, "%s: unable to allocate image1: %r\n", pname);
		threadexitsall("allocmemimage");
	}
}

void
usage(void)
{
	fprint(2, "usage: binary [-d color] [-l color]\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char* argv[])
{
	darkcol = 0x000000;
	lightcol = 0xffffff;
	ARGBEGIN{
	case 'd':
		darkcol = strtoul(EARGF(usage()), nil, 0);
		break;
	case 'l':
		lightcol = strtoul(EARGF(usage()), nil, 0);
		break;
	default:
		usage();
	}ARGEND
	mainloop();
	threadexitsall(nil);
}
