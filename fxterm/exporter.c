#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "compat.h"
#include "fxterm.h"

int
mounter(char *mntpt, int how, int fd, int n)
{
	char buf[32];
	int i, ok, mfd;

	ok = 1;
	for(i = 0; i < n; i++){
		snprint(buf, sizeof buf, "%d", i);
		mfd = dup(fd, -1);
		if(mount(mfd, -1, mntpt, how, buf) == -1){
			close(mfd);
			fprint(2, "can't mount on %s: %r\n", mntpt);
			ok = 0;
			break;
		}
		close(mfd);
		if(how == MREPL)
			how = MAFTER;
	}

	close(fd);

	return ok;
}

static void
extramp(void *v)
{
	Exporter *ex;

//	rfork(RFNAMEG);
	ex = v;
	sysexport(ex->fd, ex->roots, ex->nroots);
	shutdown();
	exits(nil);
}

int
runexporter(Dev **dt)
{
	Chan **roots;
	int p[2], i, n, ed;

	for(n = 0; dt[n] != nil; n++)
		;
	if(!n){
		werrstr("no devices specified");
		return 0;
	}

	ed = errdepth(-1);
	if(waserror()){
		werrstr(up->error);
		return 0;
	}

	roots = smalloc(n * sizeof *roots);
	for(i = 0; i < n; i++){
		(*dt[i]->reset)();
		(*dt[i]->init)();
		roots[i] = (*dt[i]->attach)("");
	}
	poperror();
	errdepth(ed);

	if(pipe(p) < 0){
		werrstr("can't make pipe: %r");
		return 0;
	}

	exporter.fd = p[0];
	exporter.cfd = p[1];
	exporter.roots = roots;
	exporter.nroots = n;
	kproc("exporter", extramp, &exporter);

	return n;
}
