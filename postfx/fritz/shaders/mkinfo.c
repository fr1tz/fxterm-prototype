#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include <mouse.h>
#include <thread.h>
#include "../../../include/fxterm.h"
#include "common.h"

char *pname = "info";

static int
winid(Rectangle r)
{
	return r.min.x+r.min.y+r.max.x+r.max.y;
}

static void
updateinfoimage(Rectangle r)
{
	ulong *data;   /* pointer to start of infoimage pixel data */
	ulong *pp;     /* pointer to pixel in infoimage */
	int ppl;       /* pixels per line */
	int bw;        /* border width */
	int f;         /* pixel flags */
	int i, x, y;   /* iterators */
	Rectangle wr;  /* window rect */
	Rectangle cwr; /* clipped window rect */

	bw = 4;
	data = wordaddr(infoimage, ZP);
	ppl = Dx(infoimage->r);
	if(frame->nwindows == 0){
		cwr = r;
		for(y = cwr.min.y; y < cwr.max.y; y++){
			for(x = cwr.min.x; x < cwr.max.x; x++){
				pp = &data[y*ppl+x];
				*pp = Fwindow;
			}
		}
		return;
	}
	memimagedraw(infoimage, r, memblack, ZP, 0, ZP, S);
	for(i = 0; i < frame->nwindows; i++){
		wr = frame->windows[i];
		cwr = wr;
		if(rectclip(&cwr, r) == 0)
			continue;
		for(y = cwr.min.y; y < cwr.max.y; y++){
			for(x = cwr.min.x; x < cwr.max.x; x++){
				pp = &data[y*ppl+x];
				if(*pp != 0)
					continue;
				f = Fwindow;
				if(x < wr.min.x+bw || x >= wr.max.x-bw 
				|| y < wr.min.y+bw || y >= wr.max.y-bw)
						f |= Fborder;
				*pp = (winid(wr) << 8);
				*pp |= f;
			}
		}
	}
}

void
processframe(void)
{
	if(frame->windowsdirty)
		work(frame->dirtr, updateinfoimage);	
}

void
usage(void)
{
	fprint(2, "usage: info\n");
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
	default:
		usage();
	}ARGEND
	if(argc != 0)
		usage();
	mainloop();
	threadexitsall(nil);
}
