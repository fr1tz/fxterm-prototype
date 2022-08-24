#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <memlayer.h>
#include <cursor.h>
#include "compat.h"
#include "fxterm.h"
#include "error.h"
#include "screen.h"

#define	IOUNIT		(64*1024)

void drawreplacecolor(ulong bcolor, ulong rcolor);
void drawreplacewhite(ulong rcolor);
void drawreplaceblack(ulong rcolor);
int drawgetbcolors(ulong bcolors[], Rectangle rects[], ulong chans[], ulong flags[], int max);

enum /* QIDs */
{
	Qtopdir = 0,   /* #ℝ                */
	Qricedir,      /* #ℝ/rice           */
	Qctl,          /* #ℝ/rice/ctl       */
	Qbcolors,      /* #ℝ/rice/bcolors   */
	Qwindows,      /* #ℝ/rice/windows   */
};

static Dirtab topdir[]={
	".",         {Qtopdir,  0, QTDIR},   0,   0555,
	"rice",      {Qricedir, 0, QTDIR},   0,   0555,
};

static Dirtab ricedir[]={
	".",         {Qricedir, 0, QTDIR  },   0,   0555,
	"ctl",       {Qctl,     0, 0      },   0,   0660,
	"bcolors",   {Qbcolors, 0, 0      },   0,   0440,
	"windows",   {Qwindows, 0, 0      },   0,   0440,
};

static void
postfxtimeout(void)
{

}


void
riceinit(void)
{

}

static Chan*
riceattach(char *spec)
{
	return devattach(L'ℝ', spec);
}

static int
ricegen(Chan *c, Dirtab*, int, int s, Dir *dp)
{
	switch(c->qid.path){
	case Qtopdir:
		return devgen(c, topdir, nelem(topdir), s, dp);
	case Qricedir:
	case Qctl:
	case Qbcolors:
	case Qwindows:
		return devgen(c, ricedir, nelem(ricedir), s, dp);
	default:
		break;
	}
	return -1;
}

static Walkqid*
ricewalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, ricegen);
}

static int
ricestat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, 0, 0, ricegen);
}

static Chan*
riceopen(Chan *c, int omode)
{
	if(postfxdebug > 0)
		fprint(2,"riceopen: qid.path: 0x%lux\n", (ulong)c->qid.path);

	c = devopen(c, omode, 0, 0, ricegen);
	return c;
}

static void
riceclose(Chan *c)
{
	if(postfxdebug > 0)
		fprint(2,"riceclose: qid.path: 0x%lux\n", (ulong)c->qid.path);
}

static long
riceread(Chan *chan, void *buf, long n, vlong off)
{
	ulong bcols[256];       /* bcolors from devdraw */
	Rectangle rects[256];   /* rects from devdraw */
	ulong chans[256];       /* chans from devdraw */
	ulong flags[256];       /* flags from devdraw */
	char cs[9];             /* chan string */
	Rectangle r;
	char *s, *sp, *se;
	int i;

	if(postfxdebug > 1)
		fprint(2,"riceread: off: %lld, n: %ld\n", off, n);

	if(n <= 0)
		return n;
	switch((ulong)chan->qid.path){
	case Qtopdir:
	case Qricedir:
		return devdirread(chan, buf, n, 0, 0, ricegen);

	case Qctl:
		return readstr(off, buf, n, "TODO");

	case Qbcolors:
		i = drawgetbcolors(bcols, rects, chans, flags, nelem(bcols));
		s = malloc(i*77+1); 
		if(s == 0)
			error(Enomem);
		s[0] = 0;
		sp = s;
		se = s+(i*256)+1;
		while(--i >= 0){
			chantostr(cs, chans[i]);
			sp = seprint(sp, se, "0x%08ulx 0x%08ulx %10s %10d %10d %10d %10d\n",
				bcols[i], flags[i], cs,  
				rects[i].min.x, rects[i].min.y, rects[i].max.x, rects[i].max.y);
		}
		n = readstr(off, buf, n, s);
		free(s);
		return n;

	case Qwindows:
		if(postfxhdr == nil)
			return 0;
		n = readbuf(off, buf, n, (char*)postfxhdr->windows, postfxhdr->nwindows*sizeof(Rectangle));
		return n;

	default:
		fprint(2,"riceread 0x%llux\n", chan->qid.path);
		error(Egreg);
	}
	return -1;		/* never reached */
}

static long
ricewrite(Chan *c, void *a, long n, vlong off)
{
	char *fields[3], *q0, *q1, *p;
	int i, m, _n, x;
	int scalex, scaley; /* used in scale msg */
	int wakeupgfx;

	wakeupgfx = 0;
	if(postfxdebug > 1)
		fprint(2,"ricewrite: qid.path: 0x%lux n: %ld off: %lld\n", (ulong)c->qid.path, n, off);
	switch((ulong)c->qid.path){
	case Qctl:
		_n = n;
		m = n;
		n = 0;
		while(m > 0){
			x = m;
			q0 = a;
			q1 = memchr(a, '\n', x);
			if(q1 == 0)
				break;
			*q1 = 0;
			i = q1-q0+1;
			n += i;
			a = q0+i;
			m -= i;
			if(getfields(q0, fields, 3, 1, " ") > 3)
				error(Ebadarg);
			//fprint(2,"%s: %s -> %s\n", fields[0], fields[1], fields[2]);
			if(strcmp(fields[0], "replace") == 0){
				if(cistrcmp(fields[1], "black") == 0)
					drawreplaceblack(strtoul(fields[2], nil, 0));
				else if(cistrcmp(fields[1], "white") == 0)
					drawreplacewhite(strtoul(fields[2], nil, 0));
				else
					drawreplacecolor(strtoul(fields[1], nil, 0), strtoul(fields[2], nil, 0));
			}
			else if(strcmp(fields[0], "scale") == 0){
				scalex = strtoul(fields[1], nil, 0);
				scaley = strtoul(fields[2], nil, 0);
				if(scalex < 1 || scaley < 1)
					error(Ebadarg);
				setdrawpixelscale(scalex, scaley);
			}
			else if(strcmp(fields[0], "postfx") == 0){
				pfxlock();
				if(strcmp(fields[1], "shader") == 0){
					for(p = fields[2]; p != q1; p++)
						if(*p == 0)
							*p = ' ';
					if(gfxctl.shader != nil)
						free(gfxctl.shader);
					gfxctl.shader = strdup(fields[2]);
					wakeupgfx = 1;
				} else if(strcmp(fields[1], "continuous") == 0){
					if(strcmp(fields[2], "on") == 0)
						gfxctl.continuous = 1;
					else
						gfxctl.continuous = 0;
					wakeupgfx = 1;
				} else if(strcmp(fields[1], "on") == 0){
					gfxctl.enabled = 1;
					wakeupgfx = 1;
				} else if(strcmp(fields[1], "off") == 0){
					gfxctl.enabled = 0;
					wakeupgfx = 1;
				}
				pfxunlock();
			}
		}
		if(wakeupgfx)
			rendwakeup(&gfxctl.init);
		return _n;

	default:
		fprint(2,"ricewrite: 0x%llux\n", c->qid.path);
		error(Egreg);
	}
	return n;
}

Dev ricedevtab = {
	L'ℝ',
	"rice",

	devreset,
	riceinit,
	riceattach,
	ricewalk,
	ricestat,
	riceopen,
	devcreate,
	riceclose,
	riceread,
	devbread,
	ricewrite,
	devbwrite,
	devremove,
	devwstat,
};


