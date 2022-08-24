/*
 * we're only using the ulong as a place to store bytes,
 * and as something to compare against.
 * the bytes are stored in little-endian format.
 */
typedef ulong Color;

typedef struct Pixfmt Pixfmt;
typedef struct Colorfmt Colorfmt;
typedef struct Vnc Vnc;
typedef struct Rlist Rlist;
typedef struct Vncs Vncs;
typedef struct Clients Clients;

struct Colorfmt {
	int		max;
	int		shift;
};

struct Pixfmt {
	int		bpp;
	int		depth;
	int		bigendian;
	int		truecolor;
	Colorfmt	red;
	Colorfmt	green;
	Colorfmt	blue;
};

struct Vnc {
	QLock;
	int		datafd;			/* for network connection */
	int		ctlfd;			/* control for network connection */

	Biobuf		in;
	Biobuf		out;

	Rectangle	dim;
	Pixfmt;
	char		*name;	/* client only */

	int		canresize;
	struct {
		ulong		id;
		Rectangle	rect;
		ulong		flags;
	}		screen[1];
};

enum {
	/* authentication negotiation */
	AFailed		= 0,
	ANoAuth,
	AVncAuth,

	/* vnc auth negotiation */
	VncAuthOK	= 0,
	VncAuthFailed,
	VncAuthTooMany,
	VncChalLen	= 16,

	/* server to client */
	MFrameUpdate	= 0,
	MSetCmap,
	MBell,
	MSCut,
	MSAck,

	/* client to server */
	MPixFmt		= 0,
	MFixCmap,
	MSetEnc,
	MFrameReq,
	MKey,
	MMouse,
	MCCut,
	MSetDesktopSize = 251,

	/* image encoding methods */
	EncRaw		= 0,
	EncCopyRect	= 1,
	EncRre		= 2,
	EncCorre	= 4,
	EncHextile	= 5,
	EncZlib		= 6,  /* 6,7,8 have been used by others */
	EncTight	= 7,
	EncZHextile	= 8,
	EncMouseWarp	= 9,

	EncDesktopSize	= -223,
	EncXDesktopSize	= -308,

	/* paramaters for hextile encoding */
	HextileDim	= 16,
	HextileRaw	= 1,
	HextileBack	= 2,
	HextileFore	= 4,
	HextileRects	= 8,
	HextileCols	= 16,

	/* stack size for libthread procs */
	STACK = 32*1024
};

struct Rlist
{
	Rectangle	bbox;
	int	maxrect;
	int	nrect;
	Rectangle *rect;
};

struct Vncs
{
	Vnc;

	Vncs	*next;
	char		remote[NETPATHLEN];
	char		netpath[NETPATHLEN];

	char		*encname;
	int		(*countrect)(Vncs*, Rectangle);
	int		(*sendrect)(Vncs*, Rectangle);
	int		copyrect;
	int		canwarp;
	int		dowarp;
	Point		warppt;

	ulong		updatereq;

	Rlist		rlist;
	int		ndead;
	int		nproc;
	int		cursorver;
	Point		cursorpos;
	Rectangle	cursorr;
	int		snarfvers;

	Memimage	*image;
	ulong	imagechan;
};

struct Clients {
	QLock;
	Vncs *head;
};

extern int verbose;
extern int vncdisplay;
extern int vncnoauth;
extern char* serveraddr;
extern Memimage *screenimage;
extern QLock screenimagelock;
extern Clients clients;
