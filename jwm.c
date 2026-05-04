#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/XF86keysym.h>

#include "jwm.h"

#define NELEM(a) (sizeof(a) / sizeof(*(a)))
#define MINSIZE  50

/* ── BSP node ───────────────────────────────────────────────────────────── */

typedef struct Node Node;
struct Node {
    int    leaf;        /* 1 = window leaf, 0 = split node          */
    int    isfull;      /* fullscreen (leaf only)                    */
    int    isfloat;     /* floating   (leaf only)                    */
    int    horiz;       /* split direction: 1=horizontal, 0=vertical */
    float  ratio;       /* split ratio [0.1, 0.9]                    */
    Node  *a, *b;       /* children (split only)                     */
    Node  *par;         /* parent node, NULL for root                */
    Window win;         /* X window (leaf only)                      */
    /* cached geometry (set during tilenode, used by drag) */
    int x, y, w, h;
};

/* ── Forward declarations ───────────────────────────────────────────────── */

static void tilenode(Node *n, int x, int y, int w, int h);
static void tile(void);
static void setfocus(Node *n);
static void detach(int s, Node *n);
static void attach(int s, Node *leaf);
static Node *findleaf(Node *n, Window w);
static Node *firstleaf(Node *n);
static Node *nextleaf(Node *cur, int s);
static Node *prevleaf(Node *cur, int s);

/* ── Global state ───────────────────────────────────────────────────────── */

static Display *dpy;
static Window   root;
static int scrw, scrh, curspace, running = 1;
static int prevspace = 0;

/* One BSP tree + focused leaf per workspace */
static Node *trees[NSPACE];
static Node *focus[NSPACE];

/* Drag state (mod + LMB = move, mod + RMB = resize) */
static Node *drag_node;
static int   drag_mode;                     /* 1=move, 2=resize */
static int   drag_ox, drag_oy;             /* pointer origin    */
static int   drag_wx, drag_wy;             /* window origin     */
static int   drag_ww, drag_wh;             /* window size       */

/* ── Error handler ──────────────────────────────────────────────────────── */

static int xerror(Display *d, XErrorEvent *e) {
    (void)d; (void)e;
    return 0;
}

/* ── BSP helpers ────────────────────────────────────────────────────────── */

static Node *mkleaf(Window w) {
    Node *n  = calloc(1, sizeof *n);
    n->leaf  = 1;
    n->ratio = 0.5f;
    n->win   = w;
    return n;
}

static Node *findleaf(Node *n, Window w) {
    if (!n) return NULL;
    if (n->leaf) return n->win == w ? n : NULL;
    Node *r = findleaf(n->a, w);
    return r ? r : findleaf(n->b, w);
}

static Node *firstleaf(Node *n) {
    while (n && !n->leaf) n = n->a;
    return n;
}

static Node *lastleaf(Node *n) {
    while (n && !n->leaf) n = n->b;
    return n;
}

/* In-order next leaf (wraps around) */
static Node *nextleaf(Node *cur, int s) {
    if (!cur || !trees[s]) return firstleaf(trees[s]);
    Node *n = cur;
    while (n->par) {
        if (n->par->a == n) {
            Node *r = firstleaf(n->par->b);
            if (r) return r;
        }
        n = n->par;
    }
    return firstleaf(trees[s]);   /* wrap */
}

/* In-order prev leaf (wraps around) */
static Node *prevleaf(Node *cur, int s) {
    if (!cur || !trees[s]) return lastleaf(trees[s]);
    Node *n = cur;
    while (n->par) {
        if (n->par->b == n) {
            Node *r = lastleaf(n->par->a);
            if (r) return r;
        }
        n = n->par;
    }
    return lastleaf(trees[s]);    /* wrap */
}

/* Raise all floating leaves above tiled ones */
static void raise_floats(Node *n) {
    if (!n) return;
    if (n->leaf) { if (n->isfloat) XRaiseWindow(dpy, n->win); return; }
    raise_floats(n->a);
    raise_floats(n->b);
}

/* ── Attach / detach ────────────────────────────────────────────────────── */

static void attach(int s, Node *leaf) {
    leaf->par = NULL;
    if (!trees[s]) {
        trees[s] = leaf;
        focus[s] = leaf;
        return;
    }

    /* Split around the currently focused leaf, or the first leaf */
    Node *t = (focus[s] && focus[s]->leaf) ? focus[s] : firstleaf(trees[s]);

    /* Choose split direction based on the target cell's aspect ratio */
    int horiz = (t->w > 0) ? (t->w >= t->h) : (scrw >= scrh);

    Node *sp    = calloc(1, sizeof *sp);
    sp->ratio   = 0.5f;
    sp->horiz   = horiz;
    sp->par     = t->par;
    sp->a       = t;
    sp->b       = leaf;

    if (!t->par)             trees[s] = sp;
    else if (t->par->a == t) t->par->a = sp;
    else                     t->par->b = sp;

    t->par    = sp;
    leaf->par = sp;
    focus[s]  = leaf;
}

static void detach(int s, Node *n) {
    if (!n->par) {
        trees[s] = NULL;
        focus[s] = NULL;
        return;
    }
    Node *p   = n->par;
    Node *sib = (p->a == n) ? p->b : p->a;
    sib->par  = p->par;

    if (!p->par)             trees[s] = sib;
    else if (p->par->a == p) p->par->a = sib;
    else                     p->par->b = sib;

    if (focus[s] == n) focus[s] = firstleaf(sib);
    free(p);
    n->par = NULL;
}

/* ── Tiling ─────────────────────────────────────────────────────────────── */

static void tilenode(Node *n, int x, int y, int w, int h) {
    if (!n) return;
    n->x = x; n->y = y; n->w = w; n->h = h;

    if (n->leaf) {
        if (n->isfloat) return;   /* floats keep their own geometry */

        if (n->isfull) {
            XMoveResizeWindow(dpy, n->win, 0, BARH, scrw, scrh);
            return;
        }

        int gx = x + GAP;
        int gy = y + GAP;
        int gw = w - 2 * GAP;
        int gh = h - 2 * GAP;
        if (gw < 1) gw = 1;
        if (gh < 1) gh = 1;
        XMoveResizeWindow(dpy, n->win, gx, gy, gw, gh);
        return;
    }

    if (n->horiz) {
        int wa = (int)(w * n->ratio);
        tilenode(n->a, x,      y, wa,    h);
        tilenode(n->b, x + wa, y, w - wa, h);
    } else {
        int ha = (int)(h * n->ratio);
        tilenode(n->a, x, y,      w, ha);
        tilenode(n->b, x, y + ha, w, h - ha);
    }
}

static void tile(void) {
    for (int s = 0; s < NSPACE; s++) {
        if (!trees[s]) continue;

        int x_offset = 0;
        if (s != curspace) {
            x_offset = (s < curspace) ? -scrw : scrw;
        }

        tilenode(trees[s], x_offset, BARH, scrw, scrh);

        if (s == curspace) {
            raise_floats(trees[s]);
        }
    }

    Node *f = focus[curspace];
    XSetInputFocus(dpy, f ? f->win : root, RevertToPointerRoot, CurrentTime);
    if (f) {
        XRaiseWindow(dpy, f->win);
    }

    XSync(dpy, False);
}

/* ── Focus ──────────────────────────────────────────────────────────────── */

static void setfocus(Node *n) {
    if (!n || !n->leaf) return;
    focus[curspace] = n;
    XSetInputFocus(dpy, n->win, RevertToPointerRoot, CurrentTime);
    XRaiseWindow(dpy, n->win);
    raise_floats(trees[curspace]);
    XSync(dpy, False);
}

/* ── Remove window from whichever workspace owns it ─────────────────────── */

static int rmwin(Window w) {
    for (int s = 0; s < NSPACE; s++) {
        Node *n = findleaf(trees[s], w);
        if (!n) continue;
        if (drag_node == n) { drag_node = NULL; drag_mode = 0; }
        detach(s, n);
        free(n);
        return 1;
    }
    return 0;
}

static void closewin(Window w) {
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    Atom wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    
    XEvent ev;
    ev.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = wm_delete;
    ev.xclient.data.l[1] = CurrentTime;

    XSendEvent(dpy, w, False, NoEventMask, &ev);
}

/* ── Extended Window Manager Hints ──────────────────────────────────────── */

static void update_ewmh_desktop(void) {
    Atom net_curr = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    unsigned long data = curspace;
    XChangeProperty(dpy, root, net_curr, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&data, 1);
}

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(void) {
    XEvent ev;

    if (!(dpy = XOpenDisplay(NULL))) { errx(1, "cannot open display"); }
    XSetErrorHandler(xerror);

#ifdef __OpenBSD__
    pledge("stdio proc exec", NULL);
#endif

    root = DefaultRootWindow(dpy);
    XSetWindowBackground(dpy, root, BlackPixel(dpy, DefaultScreen(dpy)));
    XClearWindow(dpy, root);

    Atom net_desks = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    unsigned long ndesks = NSPACE;
    XChangeProperty(dpy, root, net_desks, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ndesks, 1);
    update_ewmh_desktop();

    scrw = DisplayWidth(dpy, 0);
    scrh = DisplayHeight(dpy, 0) - BARH;

    XSelectInput(dpy, root,
        SubstructureRedirectMask | SubstructureNotifyMask |
        KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask);

    /* for (size_t i = 0; i < NELEM(keys); i++) {
        XGrabKey(dpy, XKeysymToKeycode(dpy, keys[i].sym), keys[i].mod,
            root, True, GrabModeAsync, GrabModeAsync);
    } */

    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, LockMask|Mod2Mask }; // caps lock and num lock

    for (size_t i = 0; i < NELEM(keys); i++) {
        for (size_t j = 0; j < NELEM(modifiers); j++) {
            XGrabKey(dpy, XKeysymToKeycode(dpy, keys[i].sym), 
                 keys[i].mod | modifiers[j],
                 root, True, GrabModeAsync, GrabModeAsync);
        }
    }

    /* Grab mod+LMB and mod+RMB on root for float drag/resize */
    XGrabButton(dpy, Button1, MODKEY, root, False,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button3, MODKEY, root, False,
        ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        GrabModeAsync, GrabModeAsync, None, None);

    signal(SIGCHLD, SIG_IGN);

    while (running && !XNextEvent(dpy, &ev)) {
        switch (ev.type) {

        /* ── New window ──────────────────────────────────────────────── */
        case MapRequest: {
            XWindowAttributes wa;
            Window w = ev.xmaprequest.window;
            if (!XGetWindowAttributes(dpy, w, &wa) || wa.override_redirect) break;

            Node *leaf = mkleaf(w);
            XSelectInput(dpy, w, EnterWindowMask | StructureNotifyMask);
            /* Grab mod+buttons on the window itself for float interaction */
            XGrabButton(dpy, Button1, MODKEY, w, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
            XGrabButton(dpy, Button3, MODKEY, w, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
            /* Also grab plain Button1 for click-to-focus */
            XGrabButton(dpy, Button1, AnyModifier, w, False,
                ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);

            attach(curspace, leaf);
            tile();
            XMapWindow(dpy, w);
            setfocus(leaf);
            break;
        }

        /* ── Window closed ───────────────────────────────────────────── */
        case DestroyNotify:
            if (rmwin(ev.xdestroywindow.window)) tile();
            break;
        case UnmapNotify:
            if (ev.xunmap.send_event && rmwin(ev.xunmap.window)) tile();
            break;

        /* ── Apps requesting geometry ────────────────────────────────── */
        case ConfigureRequest: {
            XWindowChanges wc = {
                .x = ev.xconfigurerequest.x,   .y = ev.xconfigurerequest.y,
                .width  = ev.xconfigurerequest.width,
                .height = ev.xconfigurerequest.height,
                .border_width = 0,
                .sibling   = ev.xconfigurerequest.above,
                .stack_mode = ev.xconfigurerequest.detail,
            };
            XConfigureWindow(dpy, ev.xconfigurerequest.window,
                ev.xconfigurerequest.value_mask, &wc);
            break;
        }

        /* ── Focus follows mouse ─────────────────────────────────────── */
        case EnterNotify:
            if (ev.xcrossing.mode != NotifyNormal ||
                ev.xcrossing.detail == NotifyInferior) break;
            {
                Node *n = findleaf(trees[curspace], ev.xcrossing.window);
                if (n && n != focus[curspace]) {
                    focus[curspace] = n;
                    setfocus(n);
                }
            }
            break;

        /* ── Button press: click-to-focus or start float drag ────────── */
        case ButtonPress: {
            Window clicked = ev.xbutton.subwindow
                ? ev.xbutton.subwindow : ev.xbutton.window;

            /* Check if modifier is held (float drag) */
            if (ev.xbutton.state & MODKEY) {
                Node *n = findleaf(trees[curspace], clicked);
                if (n && n->isfloat) {
                    focus[curspace] = n;
                    setfocus(n);

                    Window dw; int rx, ry; unsigned gw, gh, gb, gd;
                    XGetGeometry(dpy, n->win, &dw,
                        &drag_wx, &drag_wy, &gw, &gh, &gb, &gd);
                    drag_ww  = (int)gw;
                    drag_wh  = (int)gh;
                    drag_ox  = ev.xbutton.x_root;
                    drag_oy  = ev.xbutton.y_root;
                    drag_node = n;
                    drag_mode = (ev.xbutton.button == Button1) ? 1 : 2;

                    XGrabPointer(dpy, root, False,
                        PointerMotionMask | ButtonReleaseMask,
                        GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
                } else {
                    XAllowEvents(dpy, ReplayPointer, CurrentTime);
                }
            } else {
                /* Plain click-to-focus */
                Node *n = findleaf(trees[curspace], clicked);
                if (n && n != focus[curspace]) {
                    focus[curspace] = n;
                    setfocus(n);
                }
                XAllowEvents(dpy, ReplayPointer, CurrentTime);
            }
            break;
        }

        /* ── End drag ────────────────────────────────────────────────── */
        case ButtonRelease:
            if (drag_mode) {
                XUngrabPointer(dpy, CurrentTime);
                drag_mode = 0;
                drag_node = NULL;
            }
            break;

        /* ── Float move / resize ─────────────────────────────────────── */
        case MotionNotify: {
            if (!drag_mode || !drag_node) break;
            /* Coalesce motion events */
            XEvent tmp;
            while (XCheckTypedEvent(dpy, MotionNotify, &tmp)) ev = tmp;

            int dx = ev.xmotion.x_root - drag_ox;
            int dy = ev.xmotion.y_root - drag_oy;

            if (drag_mode == 1) {
                /* Move */
                int nx = drag_wx + dx;
                int ny = drag_wy + dy;
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx + drag_ww > scrw) nx = scrw - drag_ww;
                if (ny + drag_wh > scrh) ny = scrh - drag_wh;
                XMoveWindow(dpy, drag_node->win, nx, ny);
            } else {
                /* Resize */
                int nw = drag_ww + dx;
                int nh = drag_wh + dy;
                if (nw < MINSIZE) nw = MINSIZE;
                if (nh < MINSIZE) nh = MINSIZE;
                if (drag_wx + nw > scrw) nw = scrw - drag_wx;
                if (drag_wy + nh > scrh) nh = scrh - drag_wy;
                XResizeWindow(dpy, drag_node->win, nw, nh);
            }
            break;
        }

        /* ── Key bindings ────────────────────────────────────────────── */
        case KeyPress: {
            KeySym sym = XLookupKeysym(&ev.xkey, 0);
            for (size_t i = 0; i < NELEM(keys); i++) {
                if (sym != keys[i].sym || keys[i].mod != ev.xkey.state) continue;

                Arg   a   = keys[i].arg;
                Node *foc = focus[curspace];

                switch (keys[i].act) {

                case EXEC:
                    if (!fork()) {
                        if (dpy) close(ConnectionNumber(dpy));
                        setsid();
                        execvp(((char **)a.v)[0], (char **)a.v);
                        _exit(1);
                    }
                    break;

                case VIEW:
                    if (a.i >= 0 && a.i < NSPACE && a.i != curspace) {
                        prevspace = curspace;
                        curspace = a.i;
                        update_ewmh_desktop();
                        tile();
                        if (focus[curspace]) setfocus(focus[curspace]);
                    }
                    break;
                case CYCLE:
                    if (trees[curspace]) {
                        Node *n = (a.i > 0)
                            ? nextleaf(foc, curspace)
                            : prevleaf(foc, curspace);
                        if (n && n != foc) {
                            focus[curspace] = n;
                            setfocus(n);
                        }
                    }
                    break;

                case QUIT:
                    running = 0;
                    break;

                case CLOSE:
                    /* if (foc) XKillClient(dpy, foc->win); */
                    if (foc) closewin(foc->win);
                    break;

                case FULLSCR:
                    if (foc) {
                        foc->isfull ^= 1;
                        tile();
                    }
                    break;

                case FLOAT:
                    if (foc) {
                        foc->isfloat ^= 1;
                        if (foc->isfloat) {
                            /* Centre the window at half screen size */
                            int fw = scrw / 2, fh = scrh / 2;
                            int fx = (scrw - fw) / 2;
                            int fy = BARH + (scrh - fh) / 2;
                            XMoveResizeWindow(dpy, foc->win, fx, fy, fw, fh);
                            /* Grab mod+buttons so dragging works */
                            XGrabButton(dpy, Button1, MODKEY, foc->win, False,
                                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                GrabModeAsync, GrabModeAsync, None, None);
                            XGrabButton(dpy, Button3, MODKEY, foc->win, False,
                                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                GrabModeAsync, GrabModeAsync, None, None);
                            XRaiseWindow(dpy, foc->win);
                        } else {
                            tile();
                        }
                    }
                    break;

                case RESIZE:
                    /* Adjust the parent split ratio of the focused leaf */
                    if (foc && foc->par) {
                        foc->par->ratio += a.f;
                        if (foc->par->ratio < 0.1f) foc->par->ratio = 0.1f;
                        if (foc->par->ratio > 0.9f) foc->par->ratio = 0.9f;
                        tile();
                    }
                    break;

                case SWAP:
                    /* Swap the windows of two adjacent leaves */
                    if (trees[curspace] && foc) {
                        Node *other = (a.i > 0)
                            ? nextleaf(foc, curspace)
                            : prevleaf(foc, curspace);
                        if (other && other != foc) {
                            Window tmp  = foc->win;
                            foc->win    = other->win;
                            other->win  = tmp;
                            focus[curspace] = other;
                            tile();
                            setfocus(other);
                        }
                    }
                    break;

                case SEND:
                    if (foc && a.i >= 0 && a.i < NSPACE && a.i != curspace) {
                        Window w = foc->win;
                        detach(curspace, foc);
                        foc->isfloat = 0;
                        foc->isfull  = 0;
                        attach(a.i, foc);
                        tile();
                        /* focus something in the current workspace */
                        if (focus[curspace]) setfocus(focus[curspace]);
                        (void)w;
                    }
                    break;

                case SPLITDIR:
                    /* Toggle the split direction of the focused node's parent */
                    if (foc && foc->par) {
                        foc->par->horiz ^= 1;
                        tile();
                    }
                    break;
                }
            }
            break;
        }

        } /* switch ev.type */
    }

    XCloseDisplay(dpy);
    return 0;
}
