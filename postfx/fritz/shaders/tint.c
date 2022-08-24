#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include <mouse.h>
#include <thread.h>
#include "../../../include/fxterm.h"
#include "common.h"

char *pname = "tint";

uint tintcolor;
int iflag; /* invert */

static void
tint(Memimage *S, Rectangle r, Memimage *D)
{
	/* argument / variable suffixes:
	 * S = source image
	 * D = destination image
	 * r = area to tint
	*/
	ulong *lsD, *lsS;  /* line start pointers */
	ulong *pD, *pS;    /* pixel pointers */
	uchar *cD, *cS, *cT;    /* channel pointers */
	int w;             /* new/old image width */
	int x, y;          /* current pixel x/y cSordinates */
	int v;
	
	cT = (uchar*)&tintcolor;

	w = Dx(S->r);
	x = r.min.x;
	y = r.min.y;
	lsD = (ulong*)byteaddr(D, r.min);
	lsS = (ulong*)byteaddr(S, r.min);
	pD = lsD;
	pS = lsS;
	while(y < r.max.y){
		cD = (uchar*)pD;
		cS = (uchar*)pS;

		if(iflag){
			cS[R] = 255 - cS[R];
			cS[G] = 255 - cS[G];
			cS[B] = 255 - cS[B];
		}

		v = (cS[R]+cS[G]+cS[B])/3;

		cD[R] = cT[R] * v / 255;
		cD[G] = cT[G] * v / 255;
		cD[B] = cT[B] * v / 255;

		if(x == r.max.x-1){
			x = r.min.x;
			y++;
			lsD += w;
			lsS += w;
			pD = lsD;
			pS = lsS;
		} else {
			x++;
			pD++;
			pS++;
		}
	}
}

static void
pass1(Rectangle r)
{
	tint(frameimage, r, frameimage);
}

void
processframe(void)
{
	if(!eqrect(frame->dirtr, ZR))
		work(frame->dirtr, pass1);

}

void
usage(void)
{
	fprint(2, "usage: tint [-i] color\n");
	threadexitsall("usage");
}

void
shadersetup(void)
{

}

void
threadmain(int argc, char* argv[])
{
	ARGBEGIN{
	case 'i':
		iflag=1;
		break;
	default:
		usage();
	}ARGEND
	if(argc != 1)
		usage();
	tintcolor = strtoul(argv[0], nil, 0);
	mainloop();
	threadexitsall(nil);
}
