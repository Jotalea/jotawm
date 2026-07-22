#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/XF86keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "jotawm.h"

#define NELEM(a) (sizeof(a) / sizeof(*(a)))
#define MINSIZE  50

/* ── BSP node ───────────────────────────────────────────────────────────── */

typedef struct Node Node;
struct Node {
    int    leaf;        /* 1 = window leaf, 0 = split node          */
    int    isfull;      /* fullscreen (leaf only)                   */
    int    isfloat;     /* floating   (leaf only)                   */
    int    horiz;       /* split direction: 1=horizontal, 0=vertical*/
    float  ratio;       /* split ratio [0.1, 0.9]                   */
    Node  *a, *b;       /* children (split only)                    */
    Node  *par;         /* parent node, NULL for root               */
    Window win;         /* X window (leaf only)                     */
    int x, y, w, h;     /* cached geometry                          */
    int fx, fy, fw, fh; /* floating geometry                        */
};

/* ── Forward declarations ───────────────────────────────────────────────── */

#define EDGE_L 1
#define EDGE_R 2
#define EDGE_T 4
#define EDGE_B 8
#define EDGE_ALL (EDGE_L|EDGE_R|EDGE_T|EDGE_B)

static void tilenode(Node *n, int x, int y, int w, int h, int x_offset, int edges);
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
static int disph;
static Window barwin = 0, edgewin = 0;
static Atom net_wm_state, net_wm_state_full;
static Atom net_wm_window_type, net_wm_window_type_dialog;
static int layout_modes[NSPACE] = {0}; /* 0 = BSP, 1 = macOS stage manager */

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

static Node *findleaf_at(Node *n, int px, int py) {
    if (!n) return NULL;
    if (n->leaf) return n;
    if (n->horiz) {
        if (px < n->x + (int)(n->w * n->ratio)) return findleaf_at(n->a, px, py);
        return findleaf_at(n->b, px, py);
    } else {
        if (py < n->y + (int)(n->h * n->ratio)) return findleaf_at(n->a, px, py);
        return findleaf_at(n->b, px, py);
    }
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
    
    /* If the workspace is empty, just insert and focus */
    if (!trees[s]) {
        trees[s] = leaf;
        focus[s] = leaf;
        return;
    }

    Window root_ret, child_ret;
    int root_x = 0, root_y = 0, win_x, win_y;
    unsigned int mask;
    Node *t = NULL;

    /* 1. Query the X server for the current pointer coordinates */
    Bool pointer_valid = XQueryPointer(dpy, root, &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask);
    
    if (pointer_valid) {
        /* 2. Traverse the BSP tree to find the node under the cursor */
        t = findleaf_at(trees[s], root_x, root_y);
    }

    /* 3. Fallback: if pointer is out of bounds or not found, default to focused/first leaf */
    if (!t) {
        t = (focus[s] && focus[s]->leaf) ? focus[s] : firstleaf(trees[s]);
    }

    /* Choose split direction based on the target cell's aspect ratio */
    int horiz = (t->w > 0) ? (t->w >= t->h) : (scrw >= scrh);

    Node *sp    = calloc(1, sizeof *sp);
    sp->ratio   = 0.5f;
    sp->horiz   = horiz;
    sp->par     = t->par;

    /* 4. Fix Focus Regression: Determine cursor's relative position */
    int cursor_on_first_half = 0;
    if (pointer_valid && t->w > 0) { /* Ensure geometry is cached */
        if (horiz) {
            cursor_on_first_half = (root_x < t->x + (int)(t->w * sp->ratio));
        } else {
            cursor_on_first_half = (root_y < t->y + (int)(t->h * sp->ratio));
        }
    }

    /* 5. Dynamically assign nodes to keep the cursor resting on the new leaf */
    if (cursor_on_first_half) {
        sp->a = leaf; /* New window spawned Top/Left */
        sp->b = t;    /* Old window pushed Bottom/Right */
    } else {
        sp->a = t;    /* Old window stays Top/Left */
        sp->b = leaf; /* New window spawned Bottom/Right */
    }

    /* Wire up the new split node to the parent */
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

    /* if (focus[s] == n) focus[s] = firstleaf(sib); */
    if (focus[s] == n) {
        focus[s] = firstleaf(sib);
    }
    free(p);
    n->par = NULL;
}

/* ── Tiling ─────────────────────────────────────────────────────────────── */

static int is_floating(Node *n) {
    if (!n) return 1;
    if (n->leaf) return n->isfloat;
    return is_floating(n->a) && is_floating(n->b);
}

static void find_bar(void) {
    Window root_ret, parent_ret, *children;
    unsigned int nchildren;
    if (XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            XWindowAttributes wa;
            if (XGetWindowAttributes(dpy, children[i], &wa) && wa.override_redirect && wa.height == BARH) {
                if ((BAR_POS == 0 && wa.y == 0) || (BAR_POS == 1 && wa.y == disph - BARH)) {
                    barwin = children[i];
                    break;
                }
            }
        }
        if (children) XFree(children);
    }
}

static void tilenode(Node *n, int x, int y, int w, int h, int x_offset, int edges) {
    if (!n) return;
    n->x = x; n->y = y; n->w = w; n->h = h;

    if (n->leaf) {
        if (n->isfloat) {
            XMoveResizeWindow(dpy, n->win, n->fx + x_offset, n->fy, n->fw, n->fh);
            return;
        }

        if (n->isfull) {
            XMoveResizeWindow(dpy, n->win, x_offset, 0, scrw, disph);
            return;
        }

        int gl = (edges & EDGE_L) ? GAP_OUTER : GAP_INNER;
        int gr = (edges & EDGE_R) ? GAP_OUTER : GAP_INNER;
        int gt = (edges & EDGE_T) ? GAP_OUTER : GAP_INNER;
        int gb = (edges & EDGE_B) ? GAP_OUTER : GAP_INNER;

        int gx = x + gl;
        int gy = y + gt;
        int gw = w - gl - gr;
        int gh = h - gt - gb;
        if (gw < 1) gw = 1;
        if (gh < 1) gh = 1;
        XMoveResizeWindow(dpy, n->win, gx, gy, gw, gh);
        return;
    }

    int a_float = is_floating(n->a);
    int b_float = is_floating(n->b);

    /* If a child is floating, give 100% of the space to the other child */
    if (a_float || b_float) {
        tilenode(n->a, x, y, w, h, x_offset, edges);
        tilenode(n->b, x, y, w, h, x_offset, edges);
    } else {
        if (n->horiz) {
            int wa = (int)(w * n->ratio);
            tilenode(n->a, x,      y, wa,     h, x_offset, edges & ~EDGE_R);
            tilenode(n->b, x + wa, y, w - wa, h, x_offset, edges & ~EDGE_L);
        } else {
            int ha = (int)(h * n->ratio);
            tilenode(n->a, x, y,      w, ha,     x_offset, edges & ~EDGE_B);
            tilenode(n->b, x, y + ha, w, h - ha, x_offset, edges & ~EDGE_T);
        }
    }
}

static void place_stage_leaf(Node *n, Node *foc, int *current_y, int master_x, int master_y, int master_w, int master_h, int stack_w, int stack_h, float f_scale, int x_offset) {
    if (!n) return;
    if (n->leaf) {
        if (n == foc) {
            if (n->isfull) {
                XMoveResizeWindow(dpy, n->win, x_offset, 0, scrw, disph);
            } else if (n->isfloat) {
                XMoveResizeWindow(dpy, n->win, n->fx + x_offset, n->fy, n->fw, n->fh);
            } else {
                XMoveResizeWindow(dpy, n->win, master_x, master_y, master_w, master_h);
            }
        } else {
            int sw, sh;
            if (n->isfloat) {
                sw = (int)(n->fw * f_scale);
                sh = (int)(n->fh * f_scale);
                if (sw < MINSIZE) sw = MINSIZE;
                if (sh < MINSIZE) sh = MINSIZE;
            } else {
                sw = stack_w;
                sh = stack_h;
            }
            XMoveResizeWindow(dpy, n->win, GAP_OUTER + x_offset, *current_y, sw, sh);
            *current_y += sh + GAP_INNER;
        }
        return;
    }

    place_stage_leaf(n->a, foc, current_y, master_x, master_y, master_w, master_h, stack_w, stack_h, f_scale, x_offset);
    place_stage_leaf(n->b, foc, current_y, master_x, master_y, master_w, master_h, stack_w, stack_h, f_scale, x_offset);
}

static int get_stack_height(Node *n, Node *foc, int stack_h, float f_scale) {
    if (!n) return 0;
    if (n->leaf) {
        if (n == foc) return 0;
        int sh = stack_h;
        if (n->isfloat) {
            sh = (int)(n->fh * f_scale);
            if (sh < MINSIZE) sh = MINSIZE;
        }
        return sh + GAP_INNER;
    }
    return get_stack_height(n->a, foc, stack_h, f_scale) + 
           get_stack_height(n->b, foc, stack_h, f_scale);
}

static void tile(void) {
    for (int s = 0; s < NSPACE; s++) {
        if (!trees[s]) continue;

        int x_offset = 0;
        if (s != curspace) {
            x_offset = (s < curspace) ? -scrw : scrw;
        }

        if (layout_modes[s] == 1) {
            Node *foc = focus[s];
            if (!foc) foc = firstleaf(trees[s]);

            int stack_width  = (int)(scrw * STAGE_STACK_W_PCT);
            int master_x     = stack_width + STAGE_GAP_MASTER + x_offset;
            int master_y     = BARH + STAGE_MARGIN_Y;
            int master_width = scrw - (stack_width + STAGE_GAP_MASTER) - STAGE_MARGIN_X;
            int stage_height = scrh - (STAGE_MARGIN_Y * 2);
            int base_stack_w = stack_width - GAP_OUTER - GAP_INNER;
            int base_stack_h = (base_stack_w * 9) / 16;
            float float_scale = 0.25f;

            int total_stack_h = get_stack_height(trees[s], foc, base_stack_h, float_scale);
            if (total_stack_h > 0) total_stack_h -= GAP_INNER; 
            int current_y = BARH + (scrh - total_stack_h) / 2;
            if (current_y < BARH + GAP_OUTER) {
                current_y = BARH + GAP_OUTER;
            }

            place_stage_leaf(trees[s], foc, &current_y, master_x, master_y, master_width, stage_height, base_stack_w, base_stack_h, float_scale, x_offset);
        } else {
            tilenode(trees[s], x_offset, BARH, scrw, scrh, x_offset, EDGE_ALL);
        }

        if (s == curspace) {
            raise_floats(trees[s]);
        }
    }

    Node *f = focus[curspace];
    XSetInputFocus(dpy, f ? f->win : root, RevertToPointerRoot, CurrentTime);
    if (f) XRaiseWindow(dpy, f->win);

    XSync(dpy, False);
}

/* ── Focus ──────────────────────────────────────────────────────────────── */

static void setfocus(Node *n) {
    if (!n || !n->leaf) return;
    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    focus[curspace] = n;
    XSetInputFocus(dpy, n->win, RevertToPointerRoot, CurrentTime);
    raise_floats(trees[curspace]);
    if (n->isfloat)
        XRaiseWindow(dpy, n->win);
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

/* ── Repair: drop leaves whose window is dead or silently unmapped ───────
 * Safety net for the "fake window" case: a leaf survives in the tree but
 * its X window is gone (missed a DestroyNotify) or exists yet isn't
 * actually mapped (missed an UnmapNotify). Bound to a keybind so it can
 * always be run by hand, on top of fixing the root cause in the event
 * handlers below. Collects window IDs first, then removes stale ones, so
 * we never mutate the tree while still walking it. ── */

static int collect_wins(Node *n, Window *out, int cap, int count) {
    if (!n || count >= cap) return count;
    if (n->leaf) {
        out[count++] = n->win;
        return count;
    }
    count = collect_wins(n->a, out, cap, count);
    count = collect_wins(n->b, out, cap, count);
    return count;
}

static void fixtree(void) {
    Window buf[1024];
    int n = 0;
    for (int s = 0; s < NSPACE; s++)
        n = collect_wins(trees[s], buf, (int)NELEM(buf), n);

    int removed = 0;
    for (int i = 0; i < n; i++) {
        XWindowAttributes wa;
        int gone = !XGetWindowAttributes(dpy, buf[i], &wa);
        if ((gone || wa.map_state != IsViewable) && rmwin(buf[i]))
            removed++;
    }
    if (removed) tile();
}

/* ── Extended Window Manager Hints ──────────────────────────────────────── */

static void update_ewmh_desktop(void) {
    Atom net_curr = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    unsigned long data = curspace;
    XChangeProperty(dpy, root, net_curr, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&data, 1);
}

/* ── Grab keys ──────────────────────────────────────────────────────────── */

static void grab_keys(void) {
    XUngrabKey(dpy, AnyKey, AnyModifier, root);

    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, LockMask|Mod2Mask };
    for (size_t i = 0; i < NELEM(keys); i++) {
        KeyCode code = XKeysymToKeycode(dpy, keys[i].sym);
        if (code == 0) continue; /* Skip keys not present in the current map */

        for (size_t j = 0; j < NELEM(modifiers); j++) {
            XGrabKey(dpy, code, keys[i].mod | modifiers[j],
                 root, True, GrabModeAsync, GrabModeAsync);
        }
    }
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
    Cursor cursor = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, root, cursor);
    XSetWindowBackground(dpy, root, ROOT_BG);
    XClearWindow(dpy, root);

    Atom net_desks = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    unsigned long ndesks = NSPACE;
    XChangeProperty(dpy, root, net_desks, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ndesks, 1);
    update_ewmh_desktop();

    disph = DisplayHeight(dpy, 0);
    scrw = DisplayWidth(dpy, 0);
    scrh = disph - BARH;

    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_state_full = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    net_wm_window_type_dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);

    edgewin = XCreateWindow(dpy, root, 0, (BAR_POS == 0) ? 0 : disph - 1, scrw, 1, 0, 0, InputOnly, CopyFromParent, 0, NULL);
    XSelectInput(dpy, edgewin, EnterWindowMask);

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

            /* Already tracked in some tree? A stray second MapRequest for a
               window we still manage would otherwise insert a duplicate
               leaf -- the classic "empty slot in the BSP tree" symptom.
               Just show it again instead. */
            int already = 0;
            for (int s = 0; s < NSPACE && !already; s++)
                already = (findleaf(trees[s], w) != NULL);
            if (already) { XMapWindow(dpy, w); break; }

            int is_float = 0;
            int rule_matched = 0;
            XClassHint ch;
            if (XGetClassHint(dpy, w, &ch)) {
                for (size_t i = 0; i < NELEM(rules); i++) {
                    if ((ch.res_class && strstr(ch.res_class, rules[i].class)) ||
                        (ch.res_name && strstr(ch.res_name, rules[i].class))) {
                        is_float = rules[i].isfloat;
                        rule_matched = 1;
                        break;
                    }
                }
                if (ch.res_class) XFree(ch.res_class);
                if (ch.res_name) XFree(ch.res_name);
            }

            /* Dialogs float like they do on every other WM: WM_TRANSIENT_FOR
               pointing at an owner window (ICCCM), or _NET_WM_WINDOW_TYPE
               containing _NET_WM_WINDOW_TYPE_DIALOG (EWMH). GTK/Qt set one
               or both on "Open File", preferences, alert boxes, etc. An
               explicit rules[] entry above always wins over this. */
            int is_dialog = 0;
            if (!rule_matched) {
                Window trans = None;
                if (XGetTransientForHint(dpy, w, &trans) && trans != None)
                    is_dialog = 1;

                if (!is_dialog) {
                    Atom type; int fmt; unsigned long nitems, rest;
                    unsigned char *prop = NULL;
                    if (XGetWindowProperty(dpy, w, net_wm_window_type, 0, 16, False,
                            XA_ATOM, &type, &fmt, &nitems, &rest, &prop) == Success && prop) {
                        Atom *types = (Atom *)prop;
                        for (unsigned long i = 0; i < nitems; i++) {
                            if (types[i] == net_wm_window_type_dialog) { is_dialog = 1; break; }
                        }
                        XFree(prop);
                    }
                }
                if (is_dialog) is_float = 1;
            }

            Node *leaf = mkleaf(w);
            leaf->isfloat = is_float;

            if (is_float) {
                if (is_dialog) {
                    /* Honor the dialog's own requested size instead of the
                       generic 50% used for rules[]-based floats. */
                    int dw = wa.width, dh = wa.height;
                    if (dw < MINSIZE || dw > scrw) dw = scrw / 2;
                    if (dh < MINSIZE || dh > scrh) dh = scrh / 2;
                    leaf->fw = dw; leaf->fh = dh;
                } else {
                    leaf->fw = scrw / 2; leaf->fh = scrh / 2;
                }
                leaf->fx = (scrw - leaf->fw) / 2; leaf->fy = BARH + (scrh - leaf->fh) / 2;
            }

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

            XSetWindowBackground(dpy, w, ROOT_BG);
            XClearWindow(dpy, w);
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
            /* A real (non-synthetic) UnmapNotify means the window actually
               left the screen -- the client hid it or is about to destroy
               it. Plenty of GTK dialogs/popups do this without ever
               sending DestroyNotify, which used to leave an empty leaf
               sitting in the tree forever (the "fake window" bug). A
               *synthetic* UnmapNotify (send_event=1) is just the ICCCM
               WithdrawnState courtesy message clients send after they've
               already unmapped for real, so it's safely ignored here --
               jotawm never leaves a managed leaf mapped-but-hidden on its
               own, so there's nothing left to do for it by that point. */
            if (!ev.xunmap.send_event && rmwin(ev.xunmap.window)) tile();
            break;

        /* ── Apps requesting geometry ────────────────────────────────── */
        case ConfigureRequest: {
            Window w = ev.xconfigurerequest.window;
            Node *n = NULL;
            for (int s = 0; s < NSPACE; s++) {
                if ((n = findleaf(trees[s], w))) break;
            }

            if (n) {
                if (n->isfull || !n->isfloat) {
                    ev.xconfigurerequest.value_mask &= ~(CWX | CWY | CWWidth | CWHeight);
                } else {
                    int is_curspace = (findleaf(trees[curspace], w) != NULL);
                    if (!is_curspace) {
                        ev.xconfigurerequest.value_mask &= ~(CWX | CWY);
                    }
                }
            }

            XWindowChanges wc = {
                .x = ev.xconfigurerequest.x, .y = ev.xconfigurerequest.y,
                .width  = ev.xconfigurerequest.width,
                .height = ev.xconfigurerequest.height,
                .border_width = 0,
                .sibling   = ev.xconfigurerequest.above,
                .stack_mode = ev.xconfigurerequest.detail,
            };
            XConfigureWindow(dpy, w, ev.xconfigurerequest.value_mask, &wc);
            break;
        }

        /* ── Focus follows mouse ── */
        case EnterNotify:
            if (ev.xcrossing.mode != NotifyNormal ||
                ev.xcrossing.detail == NotifyInferior) break;

            if (ev.xcrossing.window == edgewin) {
                if (!barwin) find_bar();
                if (barwin) XRaiseWindow(dpy, barwin);
                break;
            }

            /* Bypass hover-focus completely when Stage Manager is active */
            if (layout_modes[curspace] == 1) break; 

            {
                Node *n = findleaf(trees[curspace], ev.xcrossing.window);
                if (n) {
                    if (n->isfull && barwin) XLowerWindow(dpy, barwin);
                    if (!n->isfloat && n != focus[curspace]) {
                        focus[curspace] = n;
                        setfocus(n);
                    }
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

                    if (layout_modes[curspace] == 1) tile();

                    Window dw; unsigned gw, gh, gb, gd;
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
                Node *n = findleaf(trees[curspace], clicked);
                int was_master = (n == focus[curspace]);
                
                if (n && (!was_master || n->isfloat)) {
                    focus[curspace] = n;
                    setfocus(n);
                    if (layout_modes[curspace] == 1) tile();
                }

                if (layout_modes[curspace] == 1 && n && !was_master) {
                    XAllowEvents(dpy, AsyncPointer, CurrentTime);
                } else {
                    XAllowEvents(dpy, ReplayPointer, CurrentTime);
                }
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

            int top_limit = (BAR_POS == 0) ? BARH : 0;
            int bot_limit = (BAR_POS == 0) ? disph : scrh;

            if (drag_mode == 1) {
                /* Move */
                int nx = drag_wx + dx;
                int ny = drag_wy + dy;
                if (nx < 0) nx = 0;
                if (ny < top_limit) ny = top_limit;
                if (nx + drag_ww > scrw) nx = scrw - drag_ww;
                if (ny + drag_wh > bot_limit) ny = bot_limit - drag_wh;

                drag_node->fx = nx;
                drag_node->fy = ny;
                XMoveWindow(dpy, drag_node->win, nx, ny);
            } else {
                /* Resize */
                int nw = drag_ww + dx;
                int nh = drag_wh + dy;
                if (nw < MINSIZE) nw = MINSIZE;
                if (nh < MINSIZE) nh = MINSIZE;
                if (drag_wx + nw > scrw) nw = scrw - drag_wx;
                if (drag_wy + nh > bot_limit) nh = bot_limit - drag_wy;
                
                drag_node->fw = nw;
                drag_node->fh = nh;
                XResizeWindow(dpy, drag_node->win, nw, nh);
            }
            break;
        }

        /* ── Handle lost keys ────────────────────────────────────────── */
        case MappingNotify:
            XRefreshKeyboardMapping(&ev.xmapping);

            if (ev.xmapping.request == MappingKeyboard || ev.xmapping.request == MappingModifier) {
                grab_keys();
            }
            break;

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
                            if (layout_modes[curspace] == 1) tile();
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
                        if (foc->isfull) {
                            XChangeProperty(dpy, foc->win, net_wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char*)&net_wm_state_full, 1);
                            XMapRaised(dpy, edgewin);
                            if (!barwin) find_bar();
                            if (barwin) XLowerWindow(dpy, barwin);
                        } else {
                            XChangeProperty(dpy, foc->win, net_wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char*)0, 0);
                            XUnmapWindow(dpy, edgewin);
                            if (barwin) XRaiseWindow(dpy, barwin);
                        }
                        tile();
                    }
                    break;

                case FLOAT:
                    if (foc) {
                        foc->isfloat ^= 1;
                        if (foc->isfloat) {
                            foc->fw = scrw / 2;
                            foc->fh = scrh / 2;
                            foc->fx = (scrw - foc->fw) / 2;
                            foc->fy = BARH + (scrh - foc->fh) / 2;

                            XMoveResizeWindow(dpy, foc->win, foc->fx, foc->fy, foc->fw, foc->fh);
                            XGrabButton(dpy, Button1, MODKEY, foc->win, False,
                                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                GrabModeAsync, GrabModeAsync, None, None);
                            XGrabButton(dpy, Button3, MODKEY, foc->win, False,
                                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                GrabModeAsync, GrabModeAsync, None, None);
                            XRaiseWindow(dpy, foc->win);
                        }
                        tile();
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

                case VIEW_ADJ: {
                    int next = curspace + a.i;
                    if (next >= 0 && next < NSPACE) {
                        prevspace = curspace;
                        curspace = next;
                        update_ewmh_desktop();
                        tile();
                        if (focus[curspace]) setfocus(focus[curspace]);
                    }
                    break;
                }

                case SPLITDIR:
                    if (foc && foc->par) {
                        foc->par->horiz ^= 1;
                        tile();
                    }
                    break;

                case TOGGLE_STAGE:
                    layout_modes[curspace] ^= 1;
                    tile();
                    break;

                case FIXTREE:
                    fixtree();
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
