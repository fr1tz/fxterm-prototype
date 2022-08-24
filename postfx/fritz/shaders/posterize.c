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

char *pname = "posterize";

uchar map[256]; /* pixel channel value -> quantized value */

static void
posterize(Memimage *S, Memimage *I, Rectangle r, Memimage *D)
{
	/* arguments / variable suffixes:
	*	S = source
	*	I = info image
	*	D = destination
	*/
	ulong *lsS, *lsI, *lsD;	/* line start pointers */
	ulong *pS, *pI, *pD;	/* pixel pointers */
	uchar *cS, *cD;		/* channel pointers */
	int w;				/* image width */
	int x, y;			/* image1 current pixel x/y coor1inates */
	int f;			/* from infoimage: flags */
	int id;				/* from infoimage: window id */

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

		if(f & Fwindow && (f & Fborder) == 0) {
			cD[R] = map[cS[R]];
			cD[G] = map[cS[G]];
			cD[B] = map[cS[B]];
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
	posterize(frameimage, infoimage, rect, frameimage);
}

void
processframe(void)
{
	if(!eqrect(frame->dirtr, ZR)){
		work(frame->dirtr, pass1);
	}
}

void
shadersetup(void)
{
}

static void
usage(void)
{
	fprint(2, "usage: posterize number\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char* argv[])
{
	uint n; /* number or ranges */
	uint r; /* length of ranges */
	uint a; /* value increment per range */
	uint m; /* value for current range */
	int i;

	if(argc != 2)
		usage();
	n = strtoul(argv[1], nil, 0);
	if(n < 2 || n > 256) {
		fprint(2, "%s: number must be between 2 and 256\n", pname);
		threadexitsall("usage");
	}
	r = 256/n;
	a = 255/(n-1);
	m = 0;
	for(i = 0; i < 256; i++) {
		if(i > 0 && i % r == 0){
			m += a;
			if(m > 255)
				m = 255;
		}
		map[i] = m;
		print("%d -> %d\n", i, map[i]);
	}
//	mainloop();
	threadexitsall(nil);
}
