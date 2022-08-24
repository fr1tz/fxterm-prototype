#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <memdraw.h>
#include <keyboard.h>
#include <cursor.h>
#include "compat.h"
#include "fxterm.h"
#include "screen.h"

extern	Dev	drawdevtab;
extern	Dev	mousedevtab;
extern	Dev	consdevtab;
extern	Dev	ricedevtab;

Dev	*devtab[] =
{
	&drawdevtab,
	&mousedevtab,
#	&consdevtab,
	&ricedevtab,
	nil
};

Exporter exporter;

static int	cmdpid;

Cursor nocursor = {
	{ -1, -1 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	},
};

void shutdown(void);

static void noteshutdown(void*, char*);


void
usage(void)
{
	fprint(1, "usage: fxterm [-i inchanstr] [-o outchanstr] [-s srvname] [cmd [args]...]\n");
	exits("usage");
}

void
derror(Display *d, char *msg)
{
	fprint(1, "derror: %s\n", msg);
}

void
mousewarpnote(Point pt)
{
	emoveto(addpt(screen->r.min, pt));
}

void
eresized(int new)
{
	if(new && getwindow(display, Refnone) < 0)
		sysfatal("getwindow: %r");
	screenresize(Dx(screen->r), Dy(screen->r));
	pfxlock();
	rendwakeup(&gfxctl.init);
	pfxunlock();
}

void
main(int argc, char **argv)
{
	char *kbdfs[] = { "/bin/aux/kbdfs", "-d", "-m", "/mnt/fxterm", nil };
	char *rc[] = { "/bin/rc", "-i", nil };
	Mouse m;
	char buf[512], *p;
	int inchan;
	int outchan;
	char *srv; /* service name */
	int n, t;
	int fd, pid;
	int kbdin;
	char *s;

	inchan = 0;
	outchan = 0;
	srv = nil;
	kbdin = -1;

	ARGBEGIN{
	case 'i':
		s = EARGF(usage());
		inchan = strtochan(s);
		if(inchan == 0)
			sysfatal("bad channel string: %s", s);
		break;
	case 'o':
		s = EARGF(usage());
		outchan = strtochan(s);
		if(outchan == 0)
			sysfatal("bad channel string: %s", s);
		break;
	case 's':
		srv = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND

	if(argc == 0)
		argv = rc;



	if(initdraw(derror, nil, "fxterm") == -1) {
		fprint(2, "initdraw failed: %r\n");
		exits("initdraw");
	}

	initcompat();

	fxtermdrawinit();

	/* start screen */
	if(inchan == 0 && outchan == 0){
		inchan = XRGB32;
		outchan = screen->chan;
	}else if(inchan == 0){
		inchan = XRGB32;
	}else if(outchan == 0){
		if(chantodepth(inchan) > screen->depth)
			outchan = screen->depth;
		else
			outchan = inchan;
	}
	if(waserror())
		sysfatal("screeninit %R: %s", up->error);
	screeninit(Dx(screen->r), Dy(screen->r), inchan, outchan);
	poperror();

	/* start file system device slaves */
	runexporter(devtab);

	/* post to service registry? */
	if(srv != nil){
		snprint(buf, 64, "/srv/%s", srv);
		fd = create(buf, OWRITE, 0600);
		if(fd < 0)
			sysfatal("create: %r");
		fprint(fd, "%d", exporter.cfd);
		close(fd);
	}

	bind("/dev", "/mnt/fxterm", MREPL);
	/* mount exporter */
	if(mounter("/mnt/fxterm", MBEFORE, exporter.cfd, exporter.nroots) < 0)
		sysfatal("mounter: %r");

	int fd1, fd2;

	pid = rfork(RFPROC|RFMEM|RFFDG|RFNOTEG);
	switch(pid){
	case -1:
		sysfatal("rfork: %r");
		break;
	case 0:
		close(exporter.fd);
		close(1);
		open("/mnt/fxterm/cons", OWRITE);
		close(2);
		open("/mnt/fxterm/cons", OWRITE);

		/* start and mount kbdfs */
/*
*		pid = rfork(RFPROC|RFMEM|RFFDG|RFREND);
*		switch(pid){
*		case -1:
*			sysfatal("rfork: %r");
*			break;
*		case 0:
*			exec(kbdfs[0], kbdfs);
*			fprint(1, "exec %s: %r\n", kbdfs[0]);
*			_exits("kbdfs");
*		}
*		if(waitpid() != pid){
*			rendezvous(&kbdin, nil);
*			sysfatal("waitpid: %s: %r", kbdfs[0]);
*		}
*		rendezvous(&kbdin, nil);
*/

		rfork(RFNAMEG|RFREND);
		bind("/mnt/fxterm", "/dev", MBEFORE);

/*
		close(0);
		open("/dev/cons", OREAD);
		close(1);
		open("/dev/cons", OWRITE);
		close(2);
		open("/dev/cons", OWRITE);
*/

		exec(argv[0], argv);
		fprint(2, "exec %s: %r\n", argv[0]);
		_exits(nil);
	}
	cmdpid = pid;

	/* wait for kbdfs to get mounted */
/*
	rendezvous(&kbdin, nil);
	if((kbdin = open("/dev/kbdin", OWRITE)) < 0)
		sysfatal("open /dev/kbdin: %r");
*/
	atexit(shutdown);
	notify(noteshutdown);

	einit(Emouse);
	for(;;){
		m = emouse();
/*
		if(ptinrect(m.xy, screen->r) == 1)
			esetcursor(&nocursor);
		else
			esetcursor(nil);
*/
		m.xy.x = (m.xy.x-screen->r.min.x);
		m.xy.y = (m.xy.y-screen->r.min.y);			
		absmousetrack(m.xy.x, m.xy.y, m.buttons, nsec()/(1000*1000LL));
	}
	exits(0);
}

/*
 * Kill the executing command and then kill everyone else.
 * Called to close up shop at the end of the day
 * and also if we get an unexpected note.
 */
static char killkin[] = "die fxterm kin";
static void
killall(void)
{
	postnote(PNGROUP, cmdpid, "hangup");
	close(exporter.cfd);
	postnote(PNGROUP, getpid(), killkin);
}

void
shutdown(void)
{
	killall();
}

static void
noteshutdown(void*, char *msg)
{
	if(strcmp(msg, killkin) == 0)	/* already shutting down */
		noted(NDFLT);
	killall();
	noted(NDFLT);
}



