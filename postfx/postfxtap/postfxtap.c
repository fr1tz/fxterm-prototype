#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <memdraw.h>
#include <bio.h>
#include <cursor.h>
#include "../../include/fxterm.h"
#include "dat.h"
#include "fns.h"

char *pname = "postfxtap";

Postfxframe	*frame;				/* post fx data to be processed */
Memdata	frameimagedata;		/* points to frame->data */
Memimage	*frameimage;		/* allocated using frameimagedata */
Memimage	*screenimage;		/* copy of frameimage */
QLock		screenimagelock;		/* used to control access to screenimage */

void
processframe(void)
{
	Vncs *v;
	Rectangle r;

	if(eqrect(frame->dirtr, ZR))
		return;

	qlock(&screenimagelock);
	memimagedraw(screenimage, frame->dirtr, frameimage, frame->dirtr.min, nil, ZP, S);
	qlock(&clients);
	for(v=clients.head; v; v=v->next)
		addtorlist(&v->rlist, frame->dirtr);
	qunlock(&clients);
	qunlock(&screenimagelock);
}

static void
setup(void)
{
	frameimagedata.base = (ulong*)frame->data;
	frameimagedata.bdata = (uchar*)frame->data;
	frameimagedata.ref = 0;
	frameimagedata.imref = nil;
	frameimagedata.allocd = 0;
	frameimage = allocmemimaged(frame->rect, XRGB32, &frameimagedata);
	screenimage = allocmemimage(frame->rect, frame->chan);
	if(screenimage == nil){
		fprint(2, "Unable to allocate screenimage: %r\n");
		threadexitsall("allocmemimage");
	}
	proccreate(vnclistenerproc, nil, STACK);
}

static void
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
				setup();
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

static void
usage(void)
{
	fprint(2, "usage: postfxtap [-d vncdisplay] [-A]\n");
	threadexitsall("usage");
}

static int
parsedisplay(char *p)
{
	int n;

	if(*p != ':')
		usage();
	if(*p == 0)
		usage();
	n = strtol(p+1, &p, 10);
	if(*p != 0)
		usage();
	return n;
}

void
threadmain(int argc, char* argv[])
{
	ARGBEGIN{
	default:
		usage();
	case 'v':
		verbose++;
		break;
	case 'd':
		if(vncdisplay != -1)
			usage();
		vncdisplay = parsedisplay(EARGF(usage()));
		break;
	case 'A':
		vncnoauth = 1;
		break;
	}ARGEND
	mainloop();
	threadexitsall(nil);
}
