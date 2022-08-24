typedef struct Postfxframe Postfxframe;
typedef struct Cursor Cursor;

struct Postfxframe {
	int    flags;
	vlong    nsec;
	ulong    chan;
	Rectangle    rect;
	Rectangle    dirtr;
	uchar 	*data;
	long    ndata;
	uchar    *usrdata;
	long    nusrdata;
	Cursor    cursor;
	Point    mousexy;
	Rectangle    windows[1024];
	int    nwindows;
	int    windowsdirty;
};
