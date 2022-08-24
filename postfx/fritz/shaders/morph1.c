#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include <mouse.h>
#include <thread.h>
#include "../../../include/fxterm.h"
#include "common.h"

char *pname = "morph1";

Rectangle activerect;
Memimage *targetimage;

static void
update(Memimage *n, Memimage *o, Rectangle r)
{
	/* argument / variable suffixes:
	 *	n = new image
	 *  o = old image
	 *  r = area to update
	*/
	ulong *lsn, *lso;  /* line start pointers */
	ulong *pn, *po;    /* pixel pointers */
	uchar *cn, *co;    /* channel pointers */
	int w;             /* new/old image width */
	int x, y;          /* current pixel x/y coordinates */
	int npx;           /* number of pixels to process */
	int i;

	w = Dx(n->r);
	x = r.min.x;
	y = r.min.y;
	lsn = (ulong*)byteaddr(n, r.min);
	lso = (ulong*)byteaddr(o, r.min);
	pn = lsn;
	po = lso;
	npx = Dx(r)*Dy(r);
	while(npx--){
		cn = (uchar*)pn;
		co = (uchar*)po;

		for(i = 0; i < 3; i++){
			if(co[i] < cn[i])
				co[i] = clampi(co[i]+20, 0, cn[i]);
			else if(co[i] > cn[i])
				co[i] = clampi(co[i]-20, cn[i], 255);
		}

		if(x == r.max.x-1){
			x = r.min.x;
			y++;
			lsn += w;
			lso += w;
			pn = lsn;
			po = lso;
		} else {
			x++;
			pn++;
			po++;
		}
	}
}

void
processframe(void)
{
/*
	if(eqrect(activerect, ZR))
		activerect = frame->dirtr;
	else
		combinerect(&activerect, frame->dirtr);
*/
	if(!eqrect(frame->dirtr, ZR))
		memimagedraw(targetimage, frame->dirtr, frameimage, frame->dirtr.min, 0, ZP, S);


	frame->dirtr = activerect;
//	fprint(2, "morph: updating %R\n", activerect);
	update(targetimage, frameimage, activerect);
}

void
usage(void)
{
	fprint(2, "usage: morph1\n");
	threadexitsall("usage");
}

void
shadersetup(void)
{
	activerect = frame->rect;
	targetimage = allocmemimage(frame->rect, frame->chan);
	if(targetimage == nil){
		fprint(2, "Unable to allocate targetimage: %r\n");
		threadexitsall("allocmemimage");
	}
}

void
threadmain(int argc, char* argv[])
{
	ARGBEGIN{
	default:
		usage();
	}ARGEND
	if(argc != 0)
		usage();
	mainloop();
	threadexitsall(nil);
}
