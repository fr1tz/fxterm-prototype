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

#include <mp.h>
#include <libsec.h>

/* 
 * list head. used to hold the list, the lock, dim, and pixelfmt
 */
Clients clients;

int	shared;
int	sleeptime = 5;
int	verbose = 0;
int	vncdisplay = -1;
int	vncnoauth = 0;

char *cert;
static int	srvfd;
static Vncs	**vncpriv;

static void listenerproc(void*);
static int vncannounce(char *net, char *adir, int base);
static void noteshutdown(void*, char*);
static void vncacceptproc(void*);
static int vncsfmt(Fmt*);
static void getremote(char*, char*);

#pragma varargck type "V" Vncs*

void
vnclistenerproc(void*)
{
	int baseport, cfd, fd;
	char adir[NETPATHLEN], ldir[NETPATHLEN];
	char net[NETPATHLEN];
	Vncs *v;

	fmtinstall('V', vncsfmt);
	baseport = 5900;
	setnetmtpt(net, sizeof net, nil);
	vncpriv = privalloc();
	if(vncpriv == nil)
		sysfatal("privalloc: %r");
	srvfd = vncannounce(net, adir, baseport);
	if(srvfd < 0)
		sysfatal("announce failed");
	if(verbose)
		fprint(2, "announced in %s\n", adir);
	threadsetname("listenerproc");
	for(;;){
		cfd = listen(adir, ldir);
		if(cfd < 0)
			break;
		if(verbose)
			fprint(2, "call in %s\n", ldir);
		fd = accept(cfd, ldir);
		if(fd < 0){
			close(cfd);
			continue;
		}
		v = mallocz(sizeof(Vncs), 1);
		if(v == nil){
			close(cfd);
			close(fd);
			continue;
		}
		v->ctlfd = cfd;
		v->datafd = fd;
		v->nproc = 1;
		v->ndead = 0;
		getremote(ldir, v->remote);
		strcpy(v->netpath, ldir);
		qlock(&clients);
		v->next = clients.head;
		clients.head = v;
		qunlock(&clients);
		proccreate(vncacceptproc, v, STACK);
	}		
}

static void
getremote(char *ldir, char *remote)
{
	char buf[NETPATHLEN];
	int fd, n;

	snprint(buf, sizeof buf, "%s/remote", ldir);
	strcpy(remote, "<none>");
	if((fd = open(buf, OREAD)) < 0)
		return;
	n = readn(fd, remote, NETPATHLEN-1);
	close(fd);
	if(n < 0)
		return;
	remote[n] = 0;
	if(n>0 && remote[n-1] == '\n')
		remote[n-1] = 0;
}

static int
vncsfmt(Fmt *fmt)
{
	Vncs *v;

	v = va_arg(fmt->args, Vncs*);
	return fmtprint(fmt, "[%d] %s %s", getpid(), v->remote, v->netpath);
}

/*
 * We register exiting as an atexit handler in each proc, so that 
 * client procs need merely exit when something goes wrong.
 */
static void
vncclose(Vncs *v)
{
	Vncs **l;

	/* remove self from client list if there */
	qlock(&clients);
	for(l=&clients.head; *l; l=&(*l)->next)
		if(*l == v){
			*l = v->next;
			break;
		}
	qunlock(&clients);

	/* if last proc, free v */
	vnclock(v);
	if(++v->ndead < v->nproc){
		vncunlock(v);
		return;
	}

	freerlist(&v->rlist);
	vncterm(v);
	if(v->ctlfd >= 0)
		close(v->ctlfd);
	if(v->datafd >= 0)
		close(v->datafd);
	if(v->image)
		freememimage(v->image);
	free(v);
}

static void
exiting(void)
{
	vncclose(*vncpriv);
}

void
vnchungup(Vnc *v)
{
	if(verbose)
		fprint(2, "%V: hangup\n", (Vncs*)v);
	exits(0);	/* atexit and exiting() will take care of everything */
}

/*
 * Kill all clients except safe.
 * Used to start a non-shared client and at shutdown. 
 */
static void
killclients(Vncs *safe)
{
	Vncs *v;

	qlock(&clients);
	for(v=clients.head; v; v=v->next){
		if(v == safe)
			continue;
		if(v->ctlfd >= 0){
			hangup(v->ctlfd);
			close(v->ctlfd);
			v->ctlfd = -1;
			close(v->datafd);
			v->datafd = -1;
		}
	}
	qunlock(&clients);
}

/*
 * Look for a port on which to announce.
 * Ifvncdisplay != -1, we only try that one.
 * Otherwise we hunt.
 *
 * Returns the announce fd.
 */
static int
vncannounce(char *net, char *adir, int base)
{
	int port, eport, fd;
	char addr[NETPATHLEN];

	if(vncdisplay == -1){
		port = base;
		eport = base+50;
	}else{
		port = base+vncdisplay;
		eport = port;
	}

	for(; port<=eport; port++){
		snprint(addr, sizeof addr, "%s/tcp!*!%d", net, port);
		if((fd = announce(addr, adir)) >= 0){
			fprint(2, "server started on vnc display :%d\n", port-base);
			return fd;
		}
	}
	if(vncdisplay == -1)
		fprint(2, "could not find any ports to announce\n");
	else
		fprint(2, "announce: %r\n");
	return -1;
}

/*
 * Handle a new connection.
 */
static void clientreadproc(void*);
static void clientwriteproc(void*);
static void chan2fmt(Pixfmt*, ulong);
static ulong fmt2chan(Pixfmt*);

static void
vncacceptproc(void *arg)
{
	Vncs *v;
	char buf[32];
	int fd;

	v = arg;
	*vncpriv = v;

	if(!atexit(exiting)){
		fprint(2, "%V: could not register atexit handler: %r; hanging up\n", v);
		exiting();
		exits(nil);
	}

	if(cert != nil){
		TLSconn conn;

		memset(&conn, 0, sizeof conn);
		conn.cert = readcert(cert, &conn.certlen);
		if(conn.cert == nil){
			fprint(2, "%V: could not read cert %s: %r; hanging up\n", v, cert);
			exits(nil);
		}
		fd = tlsServer(v->datafd, &conn);
		if(fd < 0){
			fprint(2, "%V: tlsServer: %r; hanging up\n", v);
			free(conn.cert);
			free(conn.sessionID);
			exits(nil);
		}
		v->datafd = fd;
		free(conn.cert);
		free(conn.sessionID);
	}
	vncinit(v->datafd, v->ctlfd, v);

	if(verbose)
		fprint(2, "%V: handshake\n", v);
	if(vncsrvhandshake(v) < 0){
		fprint(2, "%V: handshake failed; hanging up\n", v);
		exits(0);
	}

	if(vncnoauth){
		if(verbose)
			fprint(2, "%V: vncnoauth\n", v);
		vncwrlong(v, ANoAuth);
		vncflush(v);
	} else {
		if(verbose)
			fprint(2, "%V: auth\n", v);
		if(vncsrvauth(v) < 0){
			fprint(2, "%V: auth failed; hanging up\n", v);
			exits(0);
		}
	}

	shared = vncrdchar(v);

	if(verbose)
		fprint(2, "%V: %sshared\n", v, shared ? "" : "not ");
	if(!shared)
		killclients(v);

	v->dim = screenimage->r;
	vncwrpoint(v, v->dim.max);
	if(verbose)
		fprint(2, "%V: send screen size %R\n", v, v->dim);

	v->bpp = screenimage->depth;
	v->depth = screenimage->depth;
	v->truecolor = 1;
	v->bigendian = 0;
	chan2fmt(v, screenimage->chan);
	if(verbose)
		fprint(2, "%V: bpp=%d, depth=%d, chan=%s\n", v,
			v->bpp, v->depth, chantostr(buf, screenimage->chan));
	vncwrpixfmt(v, v);
	vncwrlong(v, 14);
	vncwrbytes(v, "Plan9 Desktop", 14);
	vncflush(v);

	if(verbose)
		fprint(2, "%V: handshaking done\n", v);

	v->updatereq = 0;
	proccreate(clientreadproc, v, STACK);
	proccreate(clientwriteproc, v, STACK);
	*vncpriv = v;
	v->nproc++;
}

/*
 * Set the pixel format being sent.  Can only happen once.
 * (Maybe a client would send this again if the screen changed
 * underneath it?  If we want to support this we need a way to
 * make sure the current image is no longer in use, so we can free it. 
 */
static void
setpixelfmt(Vncs *v)
{
	ulong chan;

	vncgobble(v, 3);
	v->Pixfmt = vncrdpixfmt(v);
	chan = fmt2chan(v);
	if(chan == 0){
		fprint(2, "%V: bad pixel format; hanging up\n", v);
		vnchungup(v);
	}
	v->imagechan = chan;
}

/*
 * Set the preferred encoding list.  Can only happen once.
 * If we want to support changing this more than once then
 * we need to put a lock around the encoding functions
 * so as not to conflict with updateimage.
 */
static void
setencoding(Vncs *v)
{
	int n, x;

	vncrdchar(v);
	n = vncrdshort(v);
	while(n-- > 0){
		x = vncrdlong(v);
		switch(x){
		case EncCopyRect:
			v->copyrect = 1;
			continue;
		case EncMouseWarp:
			v->canwarp = 1;
			continue;
		case EncDesktopSize:
			v->canresize |= 1;
			continue;
		case EncXDesktopSize:
			v->canresize |= 2;
			continue;
		}
		if(v->countrect != nil)
			continue;
		switch(x){
		case EncRaw:
			v->encname = "raw";
			v->countrect = countraw;
			v->sendrect = sendraw;
			break;
		case EncRre:
			v->encname = "rre";
			v->countrect = countrre;
			v->sendrect = sendrre;
			break;
		case EncCorre:
			v->encname = "corre";
			v->countrect = countcorre;
			v->sendrect = sendcorre;
			break;
		case EncHextile:
			v->encname = "hextile";
			v->countrect = counthextile;
			v->sendrect = sendhextile;
			break;
		}
	}

	if(v->countrect == nil){
		v->encname = "raw";
		v->countrect = countraw;
		v->sendrect = sendraw;
	}

	if(verbose)
		fprint(2, "Encoding with %s%s%s%s\n", v->encname,
			v->copyrect ? ", copyrect" : "",
			v->canwarp ? ", canwarp" : "",
			v->canresize ? ", resize" : "");
}

/*
 * Continually read updates from one client.
 */
static void
clientreadproc(void* arg)
{
	Vncs *v;
	int incremental, type, n;
	Rectangle r;

	v = arg;
	threadsetname("clientreadproc %V", v);
	for(;;){
		type = vncrdchar(v);
		switch(type){
		default:
			fprint(2, "%V: unknown vnc message type %d; hanging up\n", v, type);
			vnchungup(v);

		/* set pixel format */
		case MPixFmt:
			setpixelfmt(v);
			break;

		/* ignore color map changes */
		case MFixCmap:
			vncgobble(v, 3);
			n = vncrdshort(v);
			vncgobble(v, n*6);
			break;

		/* set encoding list */
		case MSetEnc:
			setencoding(v);
			break;

		/* request image update in rectangle */
		case MFrameReq:
			incremental = vncrdchar(v);
			r = vncrdrect(v);
			if(!incremental){
				qlock(&screenimagelock);	/* protects rlist */
				addtorlist(&v->rlist, r);
				qunlock(&screenimagelock);
			}
			v->updatereq++;
			break;

		/* ignore requests to change desktop size */
		case MSetDesktopSize:
			if(verbose > 1)
				fprint(2, "%V: ignoring set desktop size\n", v);
			vncrdchar(v);
			vncrdpoint(v);	// desktop size
			n = vncrdchar(v);
			vncrdchar(v);
			if(n == 0)
				break;
			vncrdlong(v);	// id
			vncrdrect(v);
			vncrdlong(v);	// flags
			while(--n > 0){
				vncrdlong(v);
				vncrdrect(v);
				vncrdlong(v);
			}
			break;

		/* ignore keystroke */
		case MKey:
			if(verbose > 1)
				fprint(2, "%V: ignoring keystroke\n", v);
			vncrdchar(v);
			vncgobble(v, 2);
			vncrdlong(v);
			break;

		/* ignore mouse event */
		case MMouse:
			if(verbose > 1)
				fprint(2, "%V: ignoring mouse event\n", v);
			vncrdchar(v);
			vncrdshort(v);
			vncrdshort(v);
			break;

		/* ignore client cut text */
		case MCCut:
			if(verbose > 1)
				fprint(2, "%V: ignoring client cut text\n", v);
			vncgobble(v, 3);
			n = vncrdlong(v);
			vncgobble(v, n);
			break;
		}
	}
}

static int
nbits(ulong mask)
{
	int n;

	n = 0;
	for(; mask; mask>>=1)
		n += mask&1;
	return n;
}

typedef struct Col Col;
struct Col {
	int type;
	int nbits;
	int shift;
};

static ulong
fmt2chan(Pixfmt *fmt)
{
	Col c[4], t;
	int i, j, depth, n, nc;
	ulong mask, u;

	/* unpack the Pixfmt channels */
	c[0] = (Col){CRed, nbits(fmt->red.max), fmt->red.shift};
	c[1] = (Col){CGreen, nbits(fmt->green.max), fmt->green.shift};
	c[2] = (Col){CBlue, nbits(fmt->blue.max), fmt->blue.shift};
	nc = 3;

	/* add an ignore channel if necessary */
	depth = c[0].nbits+c[1].nbits+c[2].nbits;
	if(fmt->bpp != depth){
		/* BUG: assumes only one run of ignored bits */
		if(fmt->bpp == 32)
			mask = ~0;
		else
			mask = (1<<fmt->bpp)-1;
		mask ^= fmt->red.max << fmt->red.shift;
		mask ^= fmt->green.max << fmt->green.shift;
		mask ^= fmt->blue.max << fmt->blue.shift;
		if(mask == 0)
			abort();
		n = 0;
		for(; !(mask&1); mask>>=1)
			n++;
		c[3] = (Col){CIgnore, nbits(mask), n};
		nc++;
	}

	/* sort the channels, largest shift (leftmost bits) first */
	for(i=1; i<nc; i++)
		for(j=i; j>0; j--)
			if(c[j].shift > c[j-1].shift){
				t = c[j];
				c[j] = c[j-1];
				c[j-1] = t;
			}

	/* build the channel descriptor */
	u = 0;
	for(i=0; i<nc; i++){
		u <<= 8;
		u |= CHAN1(c[i].type, c[i].nbits);
	}

	return u;
}

static void
chan2fmt(Pixfmt *fmt, ulong chan)
{
	ulong c, rc, shift;

	shift = 0;
	for(rc = chan; rc; rc >>=8){
		c = rc & 0xFF;
		switch(TYPE(c)){
		case CRed:
			fmt->red = (Colorfmt){(1<<NBITS(c))-1, shift};
			break;
		case CBlue:
			fmt->blue = (Colorfmt){(1<<NBITS(c))-1, shift};
			break;
		case CGreen:
			fmt->green = (Colorfmt){(1<<NBITS(c))-1, shift};
			break;
		}
		shift += NBITS(c);
	}
}

/*
 * Send a client his changed screen image.
 * v is locked on entrance, locked on exit, but released during.
 */
static int
updateimage(Vncs *v)
{
	int i, j, ncount, nsend;
	vlong ooffset = 0, t1 = 0;
	Rlist rlist;
	int (*count)(Vncs*, Rectangle);
	int (*send)(Vncs*, Rectangle);

	/* copy the screen bits and then unlock the screen so updates can proceed */
	qlock(&screenimagelock);
	rlist = v->rlist;
	memset(&v->rlist, 0, sizeof v->rlist);

	if( (v->image == nil && v->imagechan != 0)
	|| (v->image != nil && v->image->chan != v->imagechan)){
		if(v->image)
			freememimage(v->image);
		v->image = allocmemimage(v->dim, v->imagechan);
		if(v->image == nil){
			fprint(2, "%V: allocmemimage: %r; hanging up\n", v);
			qlock(&screenimagelock);
			vnchungup(v);
		}
	}

	/* copy changed screen parts */
	for(i=0; i<rlist.nrect; i++)
		memimagedraw(v->image, rlist.rect[i], screenimage, rlist.rect[i].min, memopaque, ZP, S);

	qunlock(&screenimagelock);

	count = v->countrect;
	send = v->sendrect;
	if(count == nil || send == nil){
		count = countraw;
		send = sendraw;
	}

	ncount = 0;
	for(i=j=0; i<rlist.nrect; i++){
		if(j < i)
			rlist.rect[j] = rlist.rect[i];
		if(rectclip(&rlist.rect[j], v->dim))
			ncount += (*count)(v, rlist.rect[j++]);
	}
	rlist.nrect = j;

	if(ncount == 0)
		return 0;

	if(verbose > 1){
		fprint(2, "sendupdate: rlist.nrect=%d, ncount=%d\n", rlist.nrect, ncount);
		t1 = nsec();
		ooffset = Boffset(&v->out);
	}

	vncwrchar(v, MFrameUpdate);
	vncwrchar(v, 0);
	vncwrshort(v, ncount);

	nsend = 0;
	for(i=0; i<rlist.nrect; i++)
		nsend += (*send)(v, rlist.rect[i]);

	if(ncount != nsend){
		fprint(2, "%V: ncount=%d, nsend=%d; hanging up\n", v, ncount, nsend);
		vnchungup(v);
	}

	if(verbose > 1){
		t1 = nsec() - t1;
		fprint(2, " in %lldms, %lld bytes\n", t1/1000000, Boffset(&v->out) - ooffset);
	}

	freerlist(&rlist);
	return 1;
}

/*
 * Continually update one client.
 */
static void
clientwriteproc(void *arg)
{
	Vncs *v;

	v = arg;
	ulong last = 0;
	threadsetname("clientwriteproc %V", v);
	while(!v->ndead){
		sleep(sleeptime);
		if(v->updatereq != last && updateimage(v))
			last++;
		vncflush(v);
	}
	vnchungup(v);
}

