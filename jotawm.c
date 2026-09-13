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
#include <X11/extensions/Xinerama.h>

#include "jotawm.h"

#define NELEM(a) (sizeof(a) / sizeof(*(a)))
#define MINSIZE  50
#define CTLKEY   ControlMask

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
    int cx, cy, cw, ch; /* canvas-space geometry                    */
};

/* ── Forward declarations ───────────────────────────────────────────────── */

#define EDGE_L 1
#define EDGE_R 2
#define EDGE_T 4
#define EDGE_B 8
#define EDGE_ALL (EDGE_L|EDGE_R|EDGE_T|EDGE_B)

static void tilenode(Node *n, int x, int y, int w, int h, int x_offset, int edges);
static void tile(void);
static void setfocus(int m, Node *n);
static void update_ewmh_active(Window w);
static void detach(int m, int s, Node *n);
static void attach(int m, int s, Node *leaf);
static Node *findleaf(Node *n, Window w);
static Node *firstleaf(Node *n);
static Node *nextleaf(Node *cur, int m, int s);
static Node *prevleaf(Node *cur, int m, int s);
static void canvas_seed_workspace(int m, int s);
static void canvas_place_tree(Node *n, int m, int s, int x_offset);
static void update_canvas_grabs(void);

/* ── Global state ───────────────────────────────────────────────────────── */

static Display *dpy;
static Window   root;
static int scrw, scrh, curspace, running = 1;
static int prevspace = 0;
static int disph;
static Window barwin[MAXMONITOR] = {0}, edgewin[MAXMONITOR] = {0};

/* ── Monitors ───────────────────────────────────────────────────────────── */

typedef struct {
    int x, y, w, h;    /* full geometry, root-window coordinates */
} Monitor;

static Monitor monitors[MAXMONITOR];
static int nmon = 1;
static int curmon = 0;

/* current-monitor context: refreshed by use_monitor() before any geometry
   math, mirroring how curspace already drives implicit array access below */
static int scrx, scry, topgap;
static Atom net_wm_state, net_wm_state_full;
static Atom net_wm_window_type, net_wm_window_type_dialog;
static Atom net_active_window;
static int layout_modes[MAXMONITOR][NSPACE] = {{0}}; /* 0 = BSP, 1 = stage manager, 2 = 2D canvas */
static int canvas_prev_modes[MAXMONITOR][NSPACE] = {{0}};
static int canvas_vx[MAXMONITOR][NSPACE] = {{0}};
static int canvas_vy[MAXMONITOR][NSPACE] = {{0}};

/* One BSP tree + focused leaf per monitor, per workspace. curspace is a
   single global index shown on every monitor at once; curmon picks which
   monitor's slice keyboard/spawn actions currently target. */
static Node *trees[MAXMONITOR][NSPACE];
static Node *focus[MAXMONITOR][NSPACE];

/* Drag state (mod + LMB = move, mod + RMB = resize) */
static Node *drag_node;
static int   drag_mode;                     /* 1=move, 2=resize */
static int   drag_ox, drag_oy;             /* pointer origin    */
static int   drag_wx, drag_wy;             /* window origin     */
static int   drag_ww, drag_wh;             /* window size       */

/* Canvas panning: a core-X11 pointer drag, suitable for a mouse or trackpad. */
static int pan_ox, pan_oy;
static int pan_vx, pan_vy;
static int pan_active;

/* ── Monitor detection ──────────────────────────────────────────────────── */

static int monitor_has_bar(int m) {
    return BAR_MONITOR < 0 || BAR_MONITOR == m;
}

/* Point every "current monitor" global at monitor m. Call this before any
   tiling/geometry math that reads scrw/scrh/scrx/scry/topgap or the legacy
   disph, the same way curspace is implicitly read by the array indices
   throughout this file. */
static void use_monitor(int m) {
    scrx   = monitors[m].x;
    scry   = monitors[m].y;
    disph  = monitors[m].h;
    scrw   = monitors[m].w;
    topgap = monitor_has_bar(m) ? BARH : 0;
    scrh   = disph - topgap;
}

/* Static detection: queried once at startup. Outputs added or removed at
   runtime require a jotawm restart to be picked up. */
static void detect_monitors(void) {
    int n = 0;
    if (XineramaIsActive(dpy)) {
        XineramaScreenInfo *info = XineramaQueryScreens(dpy, &n);
        if (info) {
            if (n > MAXMONITOR) n = MAXMONITOR;
            for (int i = 0; i < n; i++) {
                monitors[i].x = info[i].x_org;
                monitors[i].y = info[i].y_org;
                monitors[i].w = info[i].width;
                monitors[i].h = info[i].height;
            }
            XFree(info);
        }
    }
    if (n <= 0) {
        n = 1;
        monitors[0].x = 0;
        monitors[0].y = 0;
        monitors[0].w = DisplayWidth(dpy, 0);
        monitors[0].h = DisplayHeight(dpy, 0);
    }
    nmon = n;
    if (curmon >= nmon) curmon = 0;
}

/* Outer bounding box of every monitor combined -- the full virtual desktop,
   used to let a floating window be dragged across a monitor seam instead of
   being clamped to whichever monitor it started on. */
static void virtual_bounds(int *x0, int *y0, int *x1, int *y1) {
    *x0 = monitors[0].x;
    *y0 = monitors[0].y;
    *x1 = monitors[0].x + monitors[0].w;
    *y1 = monitors[0].y + monitors[0].h;
    for (int m = 1; m < nmon; m++) {
        if (monitors[m].x < *x0) *x0 = monitors[m].x;
        if (monitors[m].y < *y0) *y0 = monitors[m].y;
        if (monitors[m].x + monitors[m].w > *x1) *x1 = monitors[m].x + monitors[m].w;
        if (monitors[m].y + monitors[m].h > *y1) *y1 = monitors[m].y + monitors[m].h;
    }
}

/* Which monitor contains the point (px, py)? Falls back to curmon if the
   point lies outside every known monitor rect (shouldn't normally happen
   for a live pointer position, but geometry can be stale mid-drag). */
static int monitor_at(int px, int py) {
    for (int m = 0; m < nmon; m++) {
        if (px >= monitors[m].x && px < monitors[m].x + monitors[m].w &&
            py >= monitors[m].y && py < monitors[m].y + monitors[m].h)
            return m;
    }
    return curmon;
}

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
static Node *nextleaf(Node *cur, int m, int s) {
    if (!cur || !trees[m][s]) return firstleaf(trees[m][s]);
    Node *n = cur;
    while (n->par) {
        if (n->par->a == n) {
            Node *r = firstleaf(n->par->b);
            if (r) return r;
        }
        n = n->par;
    }
    return firstleaf(trees[m][s]);   /* wrap */
}

/* In-order prev leaf (wraps around) */
static Node *prevleaf(Node *cur, int m, int s) {
    if (!cur || !trees[m][s]) return lastleaf(trees[m][s]);
    Node *n = cur;
    while (n->par) {
        if (n->par->b == n) {
            Node *r = lastleaf(n->par->a);
            if (r) return r;
        }
        n = n->par;
    }
    return lastleaf(trees[m][s]);    /* wrap */
}

/* Track the closest leaf (and its owning monitor) to a point, across
   however many calls/trees the caller walks it over. */
static void nearest_leaf_in(Node *n, int m, int px, int py,
                             int *best_m, Node **best, long *bestd) {
    if (!n) return;
    if (n->leaf) {
        long dx = px - (n->x + n->w / 2);
        long dy = py - (n->y + n->h / 2);
        long d = dx * dx + dy * dy;
        if (!*best || d < *bestd) { *bestd = d; *best = n; *best_m = m; }
        return;
    }
    nearest_leaf_in(n->a, m, px, py, best_m, best, bestd);
    nearest_leaf_in(n->b, m, px, py, best_m, best, bestd);
}

/* Raise all floating leaves above tiled ones */
static void raise_floats(Node *n) {
    if (!n) return;
    if (n->leaf) { if (n->isfloat) XRaiseWindow(dpy, n->win); return; }
    raise_floats(n->a);
    raise_floats(n->b);
}

/* ── Attach / detach ────────────────────────────────────────────────────── */

static void attach(int m, int s, Node *leaf) {
    leaf->par = NULL;
    use_monitor(m);

    /* If the workspace is empty, just insert and focus */
    if (!trees[m][s]) {
        trees[m][s] = leaf;
        focus[m][s] = leaf;
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
        t = findleaf_at(trees[m][s], root_x, root_y);
    }

    /* 3. Fallback: if pointer is out of bounds or not found, default to focused/first leaf */
    if (!t) {
        t = (focus[m][s] && focus[m][s]->leaf) ? focus[m][s] : firstleaf(trees[m][s]);
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
    if (!t->par)             trees[m][s] = sp;
    else if (t->par->a == t) t->par->a = sp;
    else                     t->par->b = sp;

    t->par      = sp;
    leaf->par   = sp;
    focus[m][s] = leaf;
}

static void detach(int m, int s, Node *n) {
    if (!n->par) {
        trees[m][s] = NULL;
        focus[m][s] = NULL;
        return;
    }
    Node *p   = n->par;
    Node *sib = (p->a == n) ? p->b : p->a;
    sib->par  = p->par;

    if (!p->par)             trees[m][s] = sib;
    else if (p->par->a == p) p->par->a = sib;
    else                     p->par->b = sib;

    if (focus[m][s] == n) {
        focus[m][s] = firstleaf(sib);
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
            if (!XGetWindowAttributes(dpy, children[i], &wa) ||
                !wa.override_redirect || wa.height != BARH)
                continue;
            for (int m = 0; m < nmon; m++) {
                if (!monitor_has_bar(m) || barwin[m]) continue;
                int expect_y = (BAR_POS == 0) ? monitors[m].y : monitors[m].y + monitors[m].h - BARH;
                if (wa.x == monitors[m].x && wa.y == expect_y) {
                    barwin[m] = children[i];
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
            /* fx/fy are absolute root coordinates already */
            XMoveResizeWindow(dpy, n->win, n->fx + x_offset, n->fy, n->fw, n->fh);
            return;
        }

        if (n->isfull) {
            XMoveResizeWindow(dpy, n->win, scrx + x_offset, scry, scrw, disph);
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
                XMoveResizeWindow(dpy, n->win, scrx + x_offset, scry, scrw, disph);
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
            XMoveResizeWindow(dpy, n->win, scrx + GAP_OUTER + x_offset, *current_y, sw, sh);
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

static void canvas_seed_leaf(Node *n, int m, int s) {
    if (!n) return;
    if (!n->leaf) {
        canvas_seed_leaf(n->a, m, s);
        canvas_seed_leaf(n->b, m, s);
        return;
    }

    /* Do not overwrite a window's established canvas position when the user
       temporarily returns to BSP or stage mode and later comes back. */
    if (n->cw <= 0 || n->ch <= 0) {
        XWindowAttributes wa;
        if (XGetWindowAttributes(dpy, n->win, &wa)) {
            /* canvas space is monitor-local: strip this monitor's origin
               (and reserved bar space) before folding in the pan offset */
            n->cx = (wa.x - scrx) + canvas_vx[m][s];
            n->cy = (wa.y - scry - topgap) + canvas_vy[m][s];
            n->cw = wa.width > 0 ? wa.width : scrw / 2;
            n->ch = wa.height > 0 ? wa.height : scrh / 2;
        } else {
            n->cw = scrw / 2;
            n->ch = scrh / 2;
            n->cx = canvas_vx[m][s] + (scrw - n->cw) / 2;
            n->cy = canvas_vy[m][s] + (scrh - n->ch) / 2;
        }
    }

    /* Fullscreen is a viewport operation in the old layouts, not a canvas
       geometry. Leaving it set would make the state lie to clients. */
    n->isfull = 0;
    XChangeProperty(dpy, n->win, net_wm_state, XA_ATOM, 32,
        PropModeReplace, (unsigned char *)0, 0);
}

static void canvas_seed_workspace(int m, int s) {
    use_monitor(m);
    canvas_seed_leaf(trees[m][s], m, s);
}

static void canvas_sync_float_leaf(Node *n, int m, int s) {
    if (!n) return;
    if (!n->leaf) {
        canvas_sync_float_leaf(n->a, m, s);
        canvas_sync_float_leaf(n->b, m, s);
        return;
    }
    if (n->isfloat) {
        /* fx/fy are absolute; cx/cy are monitor-local canvas coordinates */
        n->fx = scrx + n->cx - canvas_vx[m][s];
        n->fy = scry + topgap + n->cy - canvas_vy[m][s];
        n->fw = n->cw;
        n->fh = n->ch;
    }
}

static void canvas_place_tree(Node *n, int m, int s, int x_offset) {
    if (!n) return;
    if (!n->leaf) {
        canvas_place_tree(n->a, m, s, x_offset);
        canvas_place_tree(n->b, m, s, x_offset);
        return;
    }

    int x = scrx + n->cx - canvas_vx[m][s] + x_offset;
    int y = scry + topgap + n->cy - canvas_vy[m][s];
    n->x = x;
    n->y = y;
    n->w = n->cw > 0 ? n->cw : scrw / 2;
    n->h = n->ch > 0 ? n->ch : scrh / 2;
    XMoveResizeWindow(dpy, n->win, x, y, n->w, n->h);
}

static void update_canvas_grabs_leaf(Node *n, int enable) {
    if (!n) return;
    if (!n->leaf) {
        update_canvas_grabs_leaf(n->a, enable);
        update_canvas_grabs_leaf(n->b, enable);
        return;
    }

    unsigned int mods[] = {
        MODKEY | CTLKEY,
        MODKEY | CTLKEY | LockMask,
        MODKEY | CTLKEY | Mod2Mask,
        MODKEY | CTLKEY | LockMask | Mod2Mask,
    };
    for (size_t i = 0; i < NELEM(mods); i++) {
        if (enable) {
            XGrabButton(dpy, Button1, mods[i], n->win, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
        } else {
            XUngrabButton(dpy, Button1, mods[i], n->win);
        }
    }
}

static void update_canvas_grabs(void) {
    /* Each monitor tracks its own layout mode per workspace now, so grabs
       must be (re)evaluated per (monitor, workspace) tree rather than
       stamped uniformly from a single curspace mode. */
    for (int m = 0; m < nmon; m++)
        for (int s = 0; s < NSPACE; s++)
            update_canvas_grabs_leaf(trees[m][s], layout_modes[m][s] == 2);
}

static void canvas_center_focus(void) {
    use_monitor(curmon);
    Node *n = focus[curmon][curspace];
    if (!n || !n->leaf) return;
    canvas_vx[curmon][curspace] = n->cx - (scrw - n->cw) / 2;
    canvas_vy[curmon][curspace] = n->cy - (scrh - n->ch) / 2;
    tile();
}

static void canvas_home(void) {
    canvas_vx[curmon][curspace] = 0;
    canvas_vy[curmon][curspace] = 0;
    tile();
}

static void tile(void) {
    for (int m = 0; m < nmon; m++) {
        use_monitor(m);

        for (int s = 0; s < NSPACE; s++) {
            if (!trees[m][s]) continue;

            int x_offset = 0;
            if (s != curspace) {
                x_offset = (s < curspace) ? -scrw : scrw;
            }

            if (layout_modes[m][s] == 2) {
                canvas_place_tree(trees[m][s], m, s, x_offset);
            } else if (layout_modes[m][s] == 1) {
                Node *foc = focus[m][s];
                if (!foc) foc = firstleaf(trees[m][s]);

                int stack_width  = (int)(scrw * STAGE_STACK_W_PCT);
                int master_x     = scrx + stack_width + STAGE_GAP_MASTER + x_offset;
                int master_y     = scry + topgap + STAGE_MARGIN_Y;
                int master_width = scrw - (stack_width + STAGE_GAP_MASTER) - STAGE_MARGIN_X;
                int stage_height = scrh - (STAGE_MARGIN_Y * 2);
                int base_stack_w = stack_width - GAP_OUTER - GAP_INNER;
                int base_stack_h = (base_stack_w * 9) / 16;
                float float_scale = 0.25f;

                int total_stack_h = get_stack_height(trees[m][s], foc, base_stack_h, float_scale);
                if (total_stack_h > 0) total_stack_h -= GAP_INNER;
                int current_y = scry + topgap + (scrh - total_stack_h) / 2;
                if (current_y < scry + topgap + GAP_OUTER) {
                    current_y = scry + topgap + GAP_OUTER;
                }

                place_stage_leaf(trees[m][s], foc, &current_y, master_x, master_y, master_width, stage_height, base_stack_w, base_stack_h, float_scale, x_offset);
            } else {
                tilenode(trees[m][s], scrx + x_offset, scry + topgap, scrw, scrh, x_offset, EDGE_ALL);
            }

            if (s == curspace) {
                raise_floats(trees[m][s]);
            }
        }
    }

    use_monitor(curmon);
    Node *f = focus[curmon][curspace];
    XSetInputFocus(dpy, f ? f->win : root, RevertToPointerRoot, CurrentTime);
    if (f) XRaiseWindow(dpy, f->win);
    update_ewmh_active(f ? f->win : None);

    if (f && f->isfull && barwin[curmon]) {
        XRaiseWindow(dpy, edgewin[curmon]);
    }

    for (int m = 0; m < nmon; m++) {
        if (layout_modes[m][curspace] == 2) {
            if (!barwin[m]) find_bar();
            if (barwin[m]) XRaiseWindow(dpy, barwin[m]);
        }
    }

    XSync(dpy, False);
}

/* ── Focus ──────────────────────────────────────────────────────────────── */

static void setfocus(int m, Node *n) {
    if (!n || !n->leaf) return;
    XUngrabPointer(dpy, CurrentTime);
    XUngrabKeyboard(dpy, CurrentTime);
    curmon = m;
    focus[m][curspace] = n;
    XSetInputFocus(dpy, n->win, RevertToPointerRoot, CurrentTime);
    update_ewmh_active(n->win);
    raise_floats(trees[m][curspace]);
    if (n->isfloat)
        XRaiseWindow(dpy, n->win);
    XSync(dpy, False);
}

/* Resolve focus after a workspace switch: whatever is directly under the
   cursor (on whichever monitor that is), else the nearest leaf to the
   cursor on curspace across every monitor, else nothing at all. The
   cursor itself never moves -- only which window owns the keyboard does. */
static void focus_switch_target(void) {
    Window rr, cr;
    int rx = 0, ry = 0, wx, wy;
    unsigned int mask;
    if (!XQueryPointer(dpy, root, &rr, &cr, &rx, &ry, &wx, &wy, &mask))
        return;

    curmon = monitor_at(rx, ry);

    Node *hit = trees[curmon][curspace]
        ? findleaf_at(trees[curmon][curspace], rx, ry) : NULL;
    if (hit) {
        setfocus(curmon, hit);
        return;
    }

    int best_m = -1;
    Node *best = NULL;
    long bestd = 0;
    for (int m = 0; m < nmon; m++)
        nearest_leaf_in(trees[m][curspace], m, rx, ry, &best_m, &best, &bestd);

    if (best) setfocus(best_m, best);
}

/* ── Remove window from whichever workspace owns it ─────────────────────── */

static int rmwin(Window w) {
    for (int m = 0; m < nmon; m++) {
        for (int s = 0; s < NSPACE; s++) {
            Node *n = findleaf(trees[m][s], w);
            if (!n) continue;
            if (drag_node == n) { drag_node = NULL; drag_mode = 0; }
            detach(m, s, n);
            free(n);
            return 1;
        }
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
    for (int m = 0; m < nmon; m++)
        for (int s = 0; s < NSPACE; s++)
            n = collect_wins(trees[m][s], buf, (int)NELEM(buf), n);

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

static void update_ewmh_active(Window w) {
    XChangeProperty(dpy, root, net_active_window, XA_WINDOW, 32,
        PropModeReplace, (unsigned char *)&w, 1);
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

    detect_monitors();
    curmon = 0;
    use_monitor(curmon);

    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_state_full = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    net_wm_window_type_dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    net_active_window = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);

    Atom net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    Atom net_desks      = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    Atom net_curr       = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    Atom supported[] = {
        net_supported, net_desks, net_curr, net_active_window,
        net_wm_state, net_wm_state_full,
        net_wm_window_type, net_wm_window_type_dialog,
    };
    XChangeProperty(dpy, root, net_supported, XA_ATOM, 32, PropModeReplace,
        (unsigned char *)supported, NELEM(supported));

    unsigned long ndesks = NSPACE;
    XChangeProperty(dpy, root, net_desks, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&ndesks, 1);
    update_ewmh_desktop();
    update_ewmh_active(None);

    for (int m = 0; m < nmon; m++) {
        if (!monitor_has_bar(m)) continue;
        edgewin[m] = XCreateWindow(dpy, root, monitors[m].x,
            (BAR_POS == 0) ? monitors[m].y : monitors[m].y + monitors[m].h - 1,
            monitors[m].w, 1, 0, 0, InputOnly, CopyFromParent, 0, NULL);
        XSelectInput(dpy, edgewin[m], EnterWindowMask);
    }

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
            for (int m = 0; m < nmon && !already; m++)
                for (int s = 0; s < NSPACE && !already; s++)
                    already = (findleaf(trees[m][s], w) != NULL);
            if (already) { XMapWindow(dpy, w); break; }

            /* New windows attach to whichever monitor the pointer is over */
            {
                Window rr, cr;
                int rx, ry, wx, wy;
                unsigned int mask;
                if (XQueryPointer(dpy, root, &rr, &cr, &rx, &ry, &wx, &wy, &mask))
                    curmon = monitor_at(rx, ry);
            }
            use_monitor(curmon);

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
                leaf->fx = scrx + (scrw - leaf->fw) / 2; leaf->fy = scry + topgap + (scrh - leaf->fh) / 2;
            }

            if (layout_modes[curmon][curspace] == 2) {
                leaf->cw = is_float && leaf->fw > 0 ? leaf->fw : (wa.width > 0 ? wa.width : scrw / 2);
                leaf->ch = is_float && leaf->fh > 0 ? leaf->fh : (wa.height > 0 ? wa.height : scrh / 2);
                if (leaf->cw < MINSIZE) leaf->cw = MINSIZE;
                if (leaf->ch < MINSIZE) leaf->ch = MINSIZE;
                leaf->cx = canvas_vx[curmon][curspace] + (scrw - leaf->cw) / 2;
                leaf->cy = canvas_vy[curmon][curspace] + (scrh - leaf->ch) / 2;
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
            attach(curmon, curspace, leaf);
            update_canvas_grabs();
            tile();
            XMapWindow(dpy, w);
            setfocus(curmon, leaf);
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
            int owner_m = -1, owner_s = -1;
            for (int m = 0; m < nmon && !n; m++) {
                for (int s = 0; s < NSPACE; s++) {
                    if ((n = findleaf(trees[m][s], w))) {
                        owner_m = m;
                        owner_s = s;
                        break;
                    }
                }
            }

            if (n && owner_s >= 0 && layout_modes[owner_m][owner_s] == 2) {
                ev.xconfigurerequest.value_mask &= ~(CWX | CWY);
                if (ev.xconfigurerequest.value_mask & CWWidth)
                    n->cw = ev.xconfigurerequest.width;
                if (ev.xconfigurerequest.value_mask & CWHeight)
                    n->ch = ev.xconfigurerequest.height;
                if (n->cw < MINSIZE) n->cw = MINSIZE;
                if (n->ch < MINSIZE) n->ch = MINSIZE;
            } else if (n) {
                if (n->isfull || !n->isfloat) {
                    ev.xconfigurerequest.value_mask &= ~(CWX | CWY | CWWidth | CWHeight);
                } else {
                    /* curspace is shown on every monitor at once, so being
                       on curspace anywhere makes the window visible */
                    int is_curspace = (owner_s == curspace);
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

            {
                int edge_m = -1;
                for (int mi = 0; mi < nmon; mi++) {
                    if (ev.xcrossing.window == edgewin[mi]) { edge_m = mi; break; }
                }
                if (edge_m >= 0) {
                    if (!barwin[edge_m]) find_bar();
                    if (barwin[edge_m]) XRaiseWindow(dpy, barwin[edge_m]);
                    break;
                }
            }

            {
                /* curspace is shown on every monitor at once, so the
                   entered window could belong to any of them */
                int m = -1;
                Node *n = NULL;
                for (int mi = 0; mi < nmon && !n; mi++) {
                    n = findleaf(trees[mi][curspace], ev.xcrossing.window);
                    if (n) m = mi;
                }
                if (n) {
                    /* Bypass hover-focus when Stage Manager is active on
                       that monitor's workspace */
                    if (layout_modes[m][curspace] == 1) break;
                    if (n->isfull && barwin[m]) XLowerWindow(dpy, barwin[m]);
                    if (!n->isfloat && n != focus[m][curspace]) {
                        setfocus(m, n);
                    } else {
                        curmon = m;
                    }
                }
            }
            break;

        /* ── Button press: click-to-focus or start float drag ────────── */
        case ButtonPress: {
            Window clicked = ev.xbutton.subwindow
                ? ev.xbutton.subwindow : ev.xbutton.window;

            /* curspace is shown on every monitor at once: resolve which
               monitor (and, if any, which leaf) was actually clicked
               before applying any of the branches below. */
            int m = -1;
            Node *n = NULL;
            for (int mi = 0; mi < nmon && !n; mi++) {
                n = findleaf(trees[mi][curspace], clicked);
                if (n) m = mi;
            }
            if (m < 0) m = monitor_at(ev.xbutton.x_root, ev.xbutton.y_root);
            curmon = m;

            /* Canvas navigation is deliberately core-X11: a left drag on
               the root pans the camera, and Mod+Ctrl+LMB does the same over
               a client window. */
            if (layout_modes[m][curspace] == 2 && ev.xbutton.button == Button1 &&
                (ev.xbutton.state & (MODKEY | CTLKEY)) == (MODKEY | CTLKEY)) {
                if (n) setfocus(m, n);
                pan_ox = ev.xbutton.x_root;
                pan_oy = ev.xbutton.y_root;
                pan_vx = canvas_vx[m][curspace];
                pan_vy = canvas_vy[m][curspace];
                pan_active = 1;
                XGrabPointer(dpy, root, False,
                    PointerMotionMask | ButtonReleaseMask,
                    GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
            } else if (layout_modes[m][curspace] == 2 && clicked == root &&
                       ev.xbutton.button == Button1 &&
                       !(ev.xbutton.state & (MODKEY | CTLKEY))) {
                pan_ox = ev.xbutton.x_root;
                pan_oy = ev.xbutton.y_root;
                pan_vx = canvas_vx[m][curspace];
                pan_vy = canvas_vy[m][curspace];
                pan_active = 1;
                XGrabPointer(dpy, root, False,
                    PointerMotionMask | ButtonReleaseMask,
                    GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
            /* Check if modifier is held (float drag or canvas window drag) */
            } else if (ev.xbutton.state & MODKEY) {
                if (n && (n->isfloat || layout_modes[m][curspace] == 2)) {
                    setfocus(m, n);

                    if (layout_modes[m][curspace] == 1) tile();

                    Window dw; unsigned gw, gh, gb, gd;
                    if (layout_modes[m][curspace] == 2) {
                        drag_wx = n->cx;
                        drag_wy = n->cy;
                        drag_ww = n->cw;
                        drag_wh = n->ch;
                    } else {
                        XGetGeometry(dpy, n->win, &dw,
                            &drag_wx, &drag_wy, &gw, &gh, &gb, &gd);
                        drag_ww  = (int)gw;
                        drag_wh  = (int)gh;
                    }
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
                int was_master = (n == focus[m][curspace]);

                if (n && !was_master) {
                    setfocus(m, n);
                    if (layout_modes[m][curspace] == 1) tile();
                }

                if (layout_modes[m][curspace] == 1 && n && !was_master) {
                    XAllowEvents(dpy, AsyncPointer, CurrentTime);
                } else {
                    XAllowEvents(dpy, ReplayPointer, CurrentTime);
                }
            }
            break;
        }

        /* ── End drag ────────────────────────────────────────────────── */
        case ButtonRelease:
            if (pan_active) {
                XUngrabPointer(dpy, CurrentTime);
                pan_active = 0;
            }
            if (drag_mode) {
                /* A floating window (outside canvas mode, which has its own
                   independent per-monitor coordinate space) that was moved
                   onto another monitor now belongs to that monitor's tree,
                   at whatever spot its drop position implies. */
                if (drag_mode == 1 && drag_node && drag_node->isfloat &&
                    layout_modes[curmon][curspace] != 2) {
                    int cx = drag_node->fx + drag_node->fw / 2;
                    int cy = drag_node->fy + drag_node->fh / 2;
                    int target = monitor_at(cx, cy);
                    if (target != curmon) {
                        detach(curmon, curspace, drag_node);
                        attach(target, curspace, drag_node);
                        tile();
                        setfocus(target, drag_node);
                    }
                }
                XUngrabPointer(dpy, CurrentTime);
                drag_mode = 0;
                drag_node = NULL;
            }
            break;

        /* ── Float move / resize ─────────────────────────────────────── */
        case MotionNotify: {
            /* Drags and pans are always scoped to curmon: setfocus() (or
               the pan-start branch in ButtonPress) already pinned curmon to
               whichever monitor owns the thing being dragged/panned. */
            use_monitor(curmon);

            if (pan_active) {
                XEvent tmp;
                while (XCheckTypedEvent(dpy, MotionNotify, &tmp)) ev = tmp;
                canvas_vx[curmon][curspace] = pan_vx - (ev.xmotion.x_root - pan_ox);
                canvas_vy[curmon][curspace] = pan_vy - (ev.xmotion.y_root - pan_oy);
                tile();
                break;
            }
            if (!drag_mode || !drag_node) break;
            /* Coalesce motion events */
            XEvent tmp;
            while (XCheckTypedEvent(dpy, MotionNotify, &tmp)) ev = tmp;

            int dx = ev.xmotion.x_root - drag_ox;
            int dy = ev.xmotion.y_root - drag_oy;

            if (layout_modes[curmon][curspace] == 2) {
                if (drag_mode == 1) {
                    drag_node->cx = drag_wx + dx;
                    drag_node->cy = drag_wy + dy;
                } else {
                    drag_node->cw = drag_ww + dx;
                    drag_node->ch = drag_wh + dy;
                    if (drag_node->cw < MINSIZE) drag_node->cw = MINSIZE;
                    if (drag_node->ch < MINSIZE) drag_node->ch = MINSIZE;
                }
                XMoveResizeWindow(dpy, drag_node->win,
                    scrx + drag_node->cx - canvas_vx[curmon][curspace],
                    scry + topgap + drag_node->cy - canvas_vy[curmon][curspace],
                    drag_node->cw, drag_node->ch);
                break;
            }

            if (drag_mode == 1) {
                /* Move -- fx/fy/drag_wx/drag_wy are absolute root
                   coordinates. Clamp to the full virtual desktop (every
                   monitor combined) rather than just the owning monitor, so
                   the window can be dragged across a monitor seam; it's
                   reassigned to whichever monitor it lands on when the drag
                   ends, in ButtonRelease. */
                int vx0, vy0, vx1, vy1;
                virtual_bounds(&vx0, &vy0, &vx1, &vy1);

                int nx = drag_wx + dx;
                int ny = drag_wy + dy;
                if (nx < vx0) nx = vx0;
                if (ny < vy0) ny = vy0;
                if (nx + drag_ww > vx1) nx = vx1 - drag_ww;
                if (ny + drag_wh > vy1) ny = vy1 - drag_wh;

                drag_node->fx = nx;
                drag_node->fy = ny;
                XMoveWindow(dpy, drag_node->win, nx, ny);
            } else {
                /* Resize -- stays clamped to the owning monitor; there's no
                   sensible monitor to hand a resize across a seam to */
                int bot_limit = (BAR_POS == 0) ? disph : scrh;

                int nw = drag_ww + dx;
                int nh = drag_wh + dy;
                if (nw < MINSIZE) nw = MINSIZE;
                if (nh < MINSIZE) nh = MINSIZE;
                if (drag_wx + nw > scrx + scrw) nw = scrx + scrw - drag_wx;
                if (drag_wy + nh > scry + bot_limit) nh = scry + bot_limit - drag_wy;

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
                use_monitor(curmon);
                Node *foc = focus[curmon][curspace];

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
                        update_canvas_grabs();
                        tile();
                        focus_switch_target();
                    }
                    break;

                case CYCLE:
                    if (trees[curmon][curspace]) {
                        Node *n = (a.i > 0)
                            ? nextleaf(foc, curmon, curspace)
                            : prevleaf(foc, curmon, curspace);
                        if (n && n != foc) {
                            setfocus(curmon, n);
                            if (layout_modes[curmon][curspace] == 1) tile();
                        }
                    }
                    if (layout_modes[curmon][curspace] == 2) {
                        canvas_center_focus();
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
                    if (layout_modes[curmon][curspace] == 2) break;
                    if (foc) {
                        foc->isfull ^= 1;
                        if (!barwin[curmon]) find_bar();

                        if (foc->isfull) {
                            XChangeProperty(dpy, foc->win, net_wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char*)&net_wm_state_full, 1);
                            if (barwin[curmon]) {
                                XMapRaised(dpy, edgewin[curmon]);
                                XLowerWindow(dpy, barwin[curmon]);
                            }
                        } else {
                            XChangeProperty(dpy, foc->win, net_wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char*)0, 0);
                            if (barwin[curmon]) {
                                XUnmapWindow(dpy, edgewin[curmon]);
                                XRaiseWindow(dpy, barwin[curmon]);
                            }
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
                            foc->fx = scrx + (scrw - foc->fw) / 2;
                            foc->fy = scry + topgap + (scrh - foc->fh) / 2;

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
                    if (trees[curmon][curspace] && foc) {
                        Node *other = (a.i > 0)
                            ? nextleaf(foc, curmon, curspace)
                            : prevleaf(foc, curmon, curspace);
                        if (other && other != foc) {
                            Window tmp  = foc->win;
                            foc->win    = other->win;
                            other->win  = tmp;
                            focus[curmon][curspace] = other;
                            tile();
                            setfocus(curmon, other);
                        }
                    }
                    break;

                case SEND:
                    /* SEND moves a window between workspaces on the same
                       monitor -- use the dedicated monitor keybinds to move
                       a window across monitors instead. */
                    if (foc && a.i >= 0 && a.i < NSPACE && a.i != curspace) {
                        Window w = foc->win;
                        detach(curmon, curspace, foc);
                        foc->isfloat = 0;
                        foc->isfull  = 0;
                        if (layout_modes[curmon][a.i] == 2) {
                            canvas_seed_leaf(foc, curmon, a.i);
                            foc->cx = canvas_vx[curmon][a.i] + (scrw - foc->cw) / 2;
                            foc->cy = canvas_vy[curmon][a.i] + (scrh - foc->ch) / 2;
                        }
                        attach(curmon, a.i, foc);
                        tile();
                        /* focus something in the current workspace */
                        if (focus[curmon][curspace]) setfocus(curmon, focus[curmon][curspace]);
                        (void)w;
                    }
                    break;

                case VIEW_ADJ: {
                    int next = curspace + a.i;
                    if (next >= 0 && next < NSPACE) {
                        prevspace = curspace;
                        curspace = next;
                        update_ewmh_desktop();
                        update_canvas_grabs();
                        tile();
                        focus_switch_target();
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
                    if (layout_modes[curmon][curspace] != 2) {
                        layout_modes[curmon][curspace] ^= 1;
                        tile();
                    }
                    break;

                case TOGGLE_CANVAS:
                    if (layout_modes[curmon][curspace] == 2) {
                        canvas_sync_float_leaf(trees[curmon][curspace], curmon, curspace);
                        layout_modes[curmon][curspace] = canvas_prev_modes[curmon][curspace];
                        update_canvas_grabs();
                        tile();
                    } else {
                        canvas_prev_modes[curmon][curspace] = layout_modes[curmon][curspace];
                        canvas_seed_workspace(curmon, curspace);
                        if (edgewin[curmon]) XUnmapWindow(dpy, edgewin[curmon]);
                        if (barwin[curmon]) XRaiseWindow(dpy, barwin[curmon]);
                        layout_modes[curmon][curspace] = 2;
                        update_canvas_grabs();
                        tile();
                    }
                    break;

                case CENTER_CANVAS:
                    if (layout_modes[curmon][curspace] == 2) canvas_center_focus();
                    break;

                case CANVAS_HOME:
                    if (layout_modes[curmon][curspace] == 2) canvas_home();
                    break;

                case FIXTREE:
                    fixtree();
                    break;

                case FOCUSMON:
                    if (nmon > 1) {
                        int target = (curmon + a.i + nmon) % nmon;
                        XWarpPointer(dpy, None, root, 0, 0, 0, 0,
                            monitors[target].x + monitors[target].w / 2,
                            monitors[target].y + monitors[target].h / 2);
                        curmon = target;
                        if (focus[target][curspace]) {
                            setfocus(target, focus[target][curspace]);
                        } else {
                            use_monitor(target);
                            XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
                            update_ewmh_active(None);
                        }
                    }
                    break;

                case SENDMON:
                    /* Same workspace index, different monitor -- the mirror
                       image of SEND, which moves across workspaces on the
                       same monitor instead. */
                    if (foc && nmon > 1) {
                        int target = (curmon + a.i + nmon) % nmon;
                        if (target != curmon) {
                            detach(curmon, curspace, foc);
                            if (layout_modes[target][curspace] == 2) {
                                use_monitor(target);
                                canvas_seed_leaf(foc, target, curspace);
                                foc->cx = canvas_vx[target][curspace] + (scrw - foc->cw) / 2;
                                foc->cy = canvas_vy[target][curspace] + (scrh - foc->ch) / 2;
                            }
                            attach(target, curspace, foc);
                            tile();
                            setfocus(target, foc);
                            XWarpPointer(dpy, None, root, 0, 0, 0, 0,
                                monitors[target].x + monitors[target].w / 2,
                                monitors[target].y + monitors[target].h / 2);
                        }
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
