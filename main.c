//main.c
//QDWM -- Quick n Dirty Window Manager
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <fcntl.h>
#include <time.h>
#include "config.h"
#include <errno.h>

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define WORKSPACES 5 //i shant deal with thy bs
#define clients workspaces[current_ws]
#define nclients counts[current_ws]

Window bar;
GC bargc;
XFontStruct *font;
Window scratchpad = None;
Display *dpy;
Window focused = None;
Window workspaces[WORKSPACES][maxwindows];


int vol_fd = -1;
int counts[WORKSPACES];
int scratchpad_visible = 0;
int current_ws = 0;
int tiling = 1;
int bar_unclean = 1;
int nmaster = 2;
int current_volume = -1;
char current_song[1024] = "nothing playing";
int xerror(Display *dpy, XErrorEvent *ee) {
	fprintf(stderr, "X11 error: %d\n", ee->error_code);
	return 0;
}
int bw = border_width;

static const char *volumeup = "pactl set-sink-volume @DEFAULT_SINK@ +5%";

static const char *volumedown = "pactl set-sink-volume @DEFAULT_SINK@ -5%";

KeySym ws_keys[WORKSPACES] = {XK_1,XK_2,XK_3,XK_4,XK_5};

void update_volume(void) {
	FILE *fp = popen(
		"pactl get-sink-volume @DEFAULT_SINK@ | awk '{print $5}' | head -n1 | tr -d '%'",
		"r"
	);

	if (!fp) {
		current_volume = -1;
		return;
	}

	char buf[16] = {0};
	if (fgets(buf, sizeof(buf), fp))
		current_volume = atoi(buf);
	else
		current_volume = -1;

	pclose(fp);
}
void setup_volume_listener(void) {
	int fds[2];
	if (pipe(fds) == -1) {
		perror("pipe");
		vol_fd = -1;
		return;
	}

	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		close(fds[0]);
		close(fds[1]);
		vol_fd = -1;
		return;
	}

	if (pid == 0) {
		dup2(fds[1], STDOUT_FILENO);
		close(fds[0]);
		close(fds[1]);

		execlp("sh", "sh", "-c",
			   "pactl subscribe | grep --line-buffered \"Event 'change' on sink\"",
		 NULL);
		_exit(1);
	}

	close(fds[1]);
	vol_fd = fds[0];
} //fuck
void removeclient(Window w) {
	for (int i = 0; i < nclients; i++) {
		if (clients[i] == w) {
			for (int j = i; j < nclients - 1; j++)
				clients[j] = clients[j + 1];
			nclients--;
			return;
		}
	}
}
void drawbar(void) {
	char buf[2048];
	const char *layout = tiling ? "[T]" : "[F]";

	FILE *fp = popen(
		"printf '{\"command\":[\"get_property\",\"media-title\"]}\n' | socat - /tmp/mpvsocket 2>/dev/null",
		"r"
	);

	strcpy(current_song, "nothing playing");

	if (fp) {
		char raw[2048] = {0};
		fread(raw, 1, sizeof(raw) - 1, fp);
		pclose(fp);

		char *p = strstr(raw, "\"data\":\"");
		if (p) {
			p += 8;
			char *end = strchr(p, '"');
			if (end) {
				size_t len = end - p;
				if (len > sizeof(current_song) - 1)
					len = sizeof(current_song) - 1;

				strncpy(current_song, p, len);
				current_song[len] = '\0';
			}
		}
	}

	if (current_volume >= 0)
		snprintf(buf, sizeof(buf)," qdwm-0.3  -  %s  -  %d%%  -  %d/%d  -  %-.900s",layout,current_volume,current_ws + 1,WORKSPACES,current_song);
		else
			snprintf(buf, sizeof(buf), "qdwm-0.2 | %s | VOL: ? | ws:%d | current song: %.900s",
					 layout, current_ws+1,
			current_song[0] ? current_song : "-");
		int y = (bar_height + font->ascent - font->descent) / 2;

	XClearWindow(dpy, bar);
	XDrawString(dpy, bar, bargc, 10, y, buf, strlen(buf));
}


void tile(void) {
	if (nclients == 0) return;

	int sw = DisplayWidth(dpy, DefaultScreen(dpy));
	int sh = DisplayHeight(dpy, DefaultScreen(dpy));
	int wx = gap;
	int wy = bar_height + gap;
	int ww = sw - 2 * gap;
	int wh = sh - bar_height - 2 * gap;

	if (nclients == 1) {
		XMoveResizeWindow(dpy, clients[0], wx, wy, ww - 2*bw, wh - 2*bw);
	}

	int mcount = (nclients < nmaster) ? nclients : nmaster;
	int stack_n = nclients - mcount;

	int master_x = wx;
	int master_w = (stack_n > 0) ? (ww * 3) / 5 : ww;

	int stack_x = master_x + master_w + gap;
	int stack_w = ww - master_w - gap;

	int master_h = (wh - (mcount - 1) * gap) / mcount;
	for (int i = 0; i < mcount; i++) {
		int x = master_x;
		int y = wy + i * (master_h + gap);
		int h = (i == mcount - 1) ? (wy + wh - y) : master_h;
		XMoveResizeWindow(dpy, clients[i], x, y, master_w - 2*bw, h - 2*bw);
	}

	if (stack_n <= 0) return;

	int stack_h = (wh - (stack_n - 1) * gap) / stack_n;
	for (int i = 0; i < stack_n; i++) {
		int idx = mcount + i;
		int x = stack_x;
		int y = wy + i * (stack_h + gap);
		int h = (i == stack_n - 1) ? (wy + wh - y) : stack_h;
		XMoveResizeWindow(dpy, clients[idx], x, y,stack_w - 2*bw, h - 2*bw);
	}
}

void wss(int target, int move) { //abandon all hope, ye who enter
	if (target < 0 || target >= WORKSPACES) return;
	if (move && focused != None){
		for (int i = 0; i<counts[current_ws]; i++) {
			if (workspaces[current_ws][i] == focused) {
				for (int j = i; j<counts[current_ws]-1;j++) workspaces[current_ws][j]=workspaces[current_ws][j+1];
				counts[current_ws]--;
				break;
			}
		}
		if (counts[target] < maxwindows) {
			workspaces[target][counts[target]++] = focused;
			XUnmapWindow(dpy, focused);
			focused = None;
		}
	}
	if (target==current_ws) return;
	for (int i = 0; i < counts[current_ws]; i++) XUnmapWindow(dpy, workspaces[current_ws][i]);
	current_ws=target;
	for (int i = 0; i < counts[current_ws]; i++) XMapWindow(dpy, workspaces[current_ws][i]);
	if (focused != None)
		XSetWindowBorder(dpy, focused, border_normal);

	focused = None;

	if (counts[current_ws] > 0) {
		focused = workspaces[current_ws][0];
		XSetWindowBorderWidth(dpy, focused, border_width);
		XSetWindowBorder(dpy, focused, border_focus);
		XSetInputFocus(dpy, focused, RevertToPointerRoot, CurrentTime);
	}

	if (tiling) tile();

}


void rotate_forward(void) {
	if (nclients < 2) return;

	Window first = clients[0];

	for (int i = 0; i < nclients - 1; i++)
		clients[i] = clients[i + 1];

	clients[nclients - 1] = first;

	tile();
}

Window getclient(Window w)
{
	Window root, parent, *children;
	unsigned int n;

	while (1) {
		if (!XQueryTree(dpy, w, &root, &parent, &children, &n))
			break;

		if (children)
			XFree(children);

		if (parent == root || parent == None)
			break;

		w = parent;
	}

	return w;
}

void killclient(Window w)
{
	Atom *protocols;
	int n;
	Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

	if (XGetWMProtocols(dpy, w, &protocols, &n)) {
		for (int i = 0; i < n; i++) {
			if (protocols[i] == wm_delete) {
				XEvent ev = {0};
				ev.type = ClientMessage;
				ev.xclient.window = w;
				ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", True);
				ev.xclient.format = 32;
				ev.xclient.data.l[0] = wm_delete;
				ev.xclient.data.l[1] = CurrentTime;

				XSendEvent(dpy, w, False, NoEventMask, &ev);
				XFree(protocols);
				return;
			}
		}
		XFree(protocols);
	}

	XKillClient(dpy, w);
}
void spawn(const char *cmd) {
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);

		FILE *f = fopen("/tmp/launchcmd.log", "a");
		if (f) {
			fprintf(f, "failed to exec '%s': %s\n", cmd, strerror(errno));
			fclose(f);
		}
		_exit(1);
	}
}

int main(void)
{
	XWindowAttributes attr;
	XButtonEvent start = {0};
	XEvent ev;

	if (!(dpy = XOpenDisplay(NULL)))
		return 1;

	XSetErrorHandler(xerror);

	int screen = DefaultScreen(dpy);
	Window root = DefaultRootWindow(dpy);
	Window parent, *children;
	unsigned int n;
	setup_volume_listener();
	update_volume();
	bar_unclean = 1;
	bar = XCreateSimpleWindow(dpy, root, 0, 0, DisplayWidth(dpy, screen), bar_height, 0, barbg, barbg);
	XClassHint barhint = {
		.res_name = "qdwmbar",
		.res_class = "qdwmbar"
	};

	XSetClassHint(dpy, bar, &barhint);

	XMapWindow(dpy, bar);
	XSelectInput(dpy, bar, ExposureMask);
	bargc = XCreateGC(dpy, bar, 0, NULL);

	font = XLoadQueryFont(dpy, "fixed");
	if (!font) font = XLoadQueryFont(dpy, "6x13");

	XSetFont(dpy, bargc, font->fid); //no i will not guard it, fuck you :3
	XSetForeground(dpy, bargc, barfg);
	unsigned int modifiers[] = {
		modkey,
		modkey | ShiftMask,
		modkey | LockMask,
		modkey | ShiftMask | LockMask,
		modkey | Mod2Mask,
		modkey | ShiftMask | Mod2Mask,
		modkey | LockMask | Mod2Mask,
		modkey | ShiftMask | LockMask | Mod2Mask
	};

	KeySym grabbed_keys[] = {XK_c,XK_t,XK_j,XK_i,XK_k,XK_Return,XK_grave,XK_p,XK_a,XK_KP_Add,XK_KP_Subtract,XK_l,XK_Escape, XK_m};

	for (unsigned int k = 0; k < sizeof(grabbed_keys)/sizeof(grabbed_keys[0]); k++) {
		KeyCode code = XKeysymToKeycode(dpy, grabbed_keys[k]);
		if (!code) continue;

		for (unsigned int i = 0; i < sizeof(modifiers)/sizeof(modifiers[0]); i++) {
			XGrabKey(dpy, code, modifiers[i],DefaultRootWindow(dpy), True,GrabModeAsync, GrabModeAsync);
		}
	}

	for (int k = 0; k < WORKSPACES; k++) {
		KeyCode code = XKeysymToKeycode(dpy, ws_keys[k]);
		if (!code) continue;

		for (unsigned int i = 0; i < sizeof(modifiers)/sizeof(modifiers[0]); i++) {
			XGrabKey(dpy, code, modifiers[i],DefaultRootWindow(dpy), True,GrabModeAsync, GrabModeAsync);
		}
	}

	XSelectInput(dpy, DefaultRootWindow(dpy),
				 SubstructureRedirectMask | SubstructureNotifyMask);
	XSync(dpy, False);


	XGrabButton(dpy, move_button, modkey, DefaultRootWindow(dpy), True,
				ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
			 GrabModeAsync, GrabModeAsync, None, None);

	XGrabButton(dpy, resize_button, modkey, DefaultRootWindow(dpy), True,
				ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
			 GrabModeAsync, GrabModeAsync, None, None);

	start.subwindow = None;

	Cursor normal = XCreateFontCursor(dpy, XC_left_ptr);
	Cursor move   = XCreateFontCursor(dpy, XC_fleur);
	Cursor resize = XCreateFontCursor(dpy, XC_sizing);
	XDefineCursor(dpy, DefaultRootWindow(dpy), normal);


	if (XQueryTree(dpy, DefaultRootWindow(dpy), &root, &parent, &children, &n)) {
		for (unsigned int i = 0; i < n; i++) {
			if (children[i] == bar)
				continue;

			XWindowAttributes wa;
			if (!XGetWindowAttributes(dpy, children[i], &wa))
				continue;
			if (wa.override_redirect || wa.map_state != IsViewable)
				continue;

			XSelectInput(dpy, children[i], EnterWindowMask | FocusChangeMask);
			XSetWindowBorderWidth(dpy, children[i], border_width);
			XSetWindowBorder(dpy, children[i], border_normal);

			if (nclients < maxwindows)
				clients[nclients++] = getclient(children[i]);
		}
		if (children)
			XFree(children);
	}

	if (tiling)
		tile();

	for (;;) {
		fd_set fds;
		FD_ZERO(&fds);

		int xfd = ConnectionNumber(dpy);
		FD_SET(xfd, &fds);

		int maxfd = xfd;

		if (vol_fd >= 0) {
			FD_SET(vol_fd, &fds);
			if (vol_fd > maxfd)
				maxfd = vol_fd;
		}

		if (bar_unclean) {
			drawbar();
			bar_unclean = 0;
			XFlush(dpy);
		}

		if (select(maxfd + 1, &fds, NULL, NULL, NULL) == -1) {
			perror("select");
			continue;
		}

		if (vol_fd >= 0 && FD_ISSET(vol_fd, &fds)) {
			char buf[256];
			read(vol_fd, buf, sizeof(buf));
			update_volume();
			bar_unclean = 1;
		}

		if (FD_ISSET(xfd, &fds)) {
			while (XPending(dpy)) {
				XNextEvent(dpy, &ev);

				if (ev.type == KeyPress) {
					KeySym keysym = XLookupKeysym(&ev.xkey, 0);

					if (keysym == XK_p && (ev.xkey.state & modkey))
						spawn(launchcmd);
					else if (keysym == XK_Return && (ev.xkey.state & modkey))
						spawn(termcmd);
					else if (keysym == XK_Escape && (ev.xkey.state & modkey)) {
						XCloseDisplay(dpy);
						exit(0);
					}
					else if ((keysym == XK_C || keysym == XK_c) &&
						(ev.xkey.state & ShiftMask) &&
						(ev.xkey.state & modkey)) {
						if (focused != None) {
							XSetWindowBorder(dpy, focused, border_normal);
							killclient(getclient(focused));
							focused = None;
							bar_unclean = 1;
						}
						}
						else if (keysym == XK_t && (ev.xkey.state & modkey)) {
							tiling = !tiling;
							if (tiling) tile();
							bar_unclean = 1;
						}
						else if (keysym == XK_j && (ev.xkey.state & modkey)) {
							rotate_forward();
							if (nclients > 0) {
								if (focused != None)
									XSetWindowBorder(dpy, focused, border_normal);

								focused = clients[0];
								XSetWindowBorder(dpy, focused, border_focus);
								XSetInputFocus(dpy, focused, RevertToPointerRoot, CurrentTime);
								XRaiseWindow(dpy, focused);
							}
							bar_unclean = 1;
						}
						else if (keysym == XK_i && (ev.xkey.state & modkey)) {
							nmaster++;
							if (nmaster > nclients)
								nmaster = nclients;
							tile();
							bar_unclean = 1;
						}
						else if (keysym == XK_k && (ev.xkey.state & modkey)) {
							nmaster--;
							if (nmaster < 1)
								nmaster = 1;
							tile();
							bar_unclean = 1;
						}

						else if (keysym == XK_m && (ev.xkey.state & modkey)) {
							if (nclients > 0) {
								int i;
								Window old = focused;

								for (i = 0; i < nclients; i++)
									if (clients[i] == focused)
										break;

								i = (i + 1) % nclients;
								focused = clients[i];

								if (old != None && old != focused)
									XSetWindowBorder(dpy, old, border_normal);

								XSetWindowBorder(dpy, focused, border_focus);
								XSetInputFocus(dpy, focused, RevertToPointerRoot, CurrentTime);
								XRaiseWindow(dpy, focused);
							}
							if (focused != None)
								XSetWindowBorder(dpy, focused, border_focus);
						}
						else if (keysym == XK_grave && (ev.xkey.state & modkey)) {
							if (scratchpad == None) {
								spawn(scratchpadcommand);
							} else {
								if (scratchpad_visible) {
									XUnmapWindow(dpy, scratchpad);
									scratchpad_visible = 0;
								} else {
									XMapRaised(dpy, scratchpad);
									XSetInputFocus(dpy, scratchpad, RevertToParent, CurrentTime);
									scratchpad_visible = 1;
								}
								bar_unclean = 1;
							}
						}else if (keysym == XK_a && (ev.xkey.state & modkey)) {
							spawn(
								"[ -S /tmp/mpvsocket ] || "
								"(mpv --no-video --idle=yes --input-ipc-server=/tmp/mpvsocket >/tmp/mpv.log 2>&1 &); "
								"sel=$(find \"$HOME/Music\" -type f | dmenu -l 15); "
								"[ -n \"$sel\" ] && printf 'loadfile \"%s\" replace\n' \"$sel\" | socat - /tmp/mpvsocket"
							);
							bar_unclean = 1;
						}else if (keysym == XK_KP_Add && (ev.xkey.state & modkey)) {
							spawn(volumeup);
						}else if (keysym == XK_KP_Subtract && (ev.xkey.state & modkey)) {
							spawn(volumedown);
						}else if (keysym == XK_l && (ev.xkey.state & Mod1Mask)) {
							spawn("printf '{\"command\":[\"cycle\",\"pause\"]}\n' | socat - /tmp/mpvsocket");
							bar_unclean = 1;
						}
						else if (ev.xkey.subwindow != None) {
							XRaiseWindow(dpy, ev.xkey.subwindow);
						}


						for (int i = 0; i < WORKSPACES; i++) {
							if (keysym == ws_keys[i] && (ev.xkey.state & modkey)) {
								if (ev.xkey.state & ShiftMask) wss(i, 1);
								else wss(i, 0);
								bar_unclean = 1;
								break;
							}
						}



				}

				else if (ev.type == ButtonPress) {
					if (ev.xbutton.subwindow != None && ev.xbutton.subwindow != bar) {
						Window w = getclient(ev.xbutton.subwindow);

						if (focused != None && focused != w)
							XSetWindowBorder(dpy, focused, border_normal);

						focused = w;
						XSetWindowBorderWidth(dpy, focused, border_width);
						XSetWindowBorder(dpy, focused, border_focus);

						XSetInputFocus(dpy, focused, RevertToPointerRoot, CurrentTime);
						XRaiseWindow(dpy, focused);
						XGetWindowAttributes(dpy, focused, &attr);
						start = ev.xbutton;
						start.subwindow = focused;

						if (start.button == move_button)
							XDefineCursor(dpy, DefaultRootWindow(dpy), move);
						else if (start.button == resize_button)
							XDefineCursor(dpy, DefaultRootWindow(dpy), resize);
					}
				}

				else if (ev.type == MotionNotify && start.subwindow != None && !tiling) {
					int xdiff = ev.xmotion.x_root - start.x_root;
					int ydiff = ev.xmotion.y_root - start.y_root;

					if (start.button == move_button) {
						int nx = attr.x + xdiff;
						int ny = attr.y + ydiff;
						XMoveWindow(dpy, start.subwindow, nx, ny);
					}
					else if (start.button == resize_button) {
						int nw = MAX(1, attr.width + xdiff);
						int nh = MAX(1, attr.height + ydiff);
						XResizeWindow(dpy, start.subwindow, nw, nh);
					}
				}

				else if (ev.type == ButtonRelease) {
					start.subwindow = None;
					XDefineCursor(dpy, DefaultRootWindow(dpy), normal);
				}

				else if (ev.type == MapRequest) {
					if (ev.xmaprequest.window == bar)
						continue;

					XMapWindow(dpy, ev.xmaprequest.window);
					XRaiseWindow(dpy, ev.xmaprequest.window);


					XClassHint ch = {0};

					if (XGetClassHint(dpy, ev.xmaprequest.window, &ch)) {
						if (ch.res_class && strcmp(ch.res_class, "scratchpad") == 0) {
							scratchpad = ev.xmaprequest.window;
							scratchpad_visible = 1;

							int sw = DisplayWidth(dpy, DefaultScreen(dpy));
							int sh = DisplayHeight(dpy, DefaultScreen(dpy));

							int w = sw / 2;
							int h = (sh - bar_height) / 2;
							int x = (sw - w) / 2;
							int y = bar_height + (sh - bar_height - h) / 2;

							XMoveResizeWindow(dpy, scratchpad, x, y, w, h);
						}
						if (ch.res_name) XFree(ch.res_name);
						if (ch.res_class) XFree(ch.res_class);
					}

					if (focused != None)
						XSetWindowBorder(dpy, focused, border_normal);

					focused = getclient(ev.xmaprequest.window);
					XSetWindowBorderWidth(dpy, focused, border_width);
					XSetWindowBorder(dpy, focused, border_focus);
					XSetInputFocus(dpy, focused, RevertToPointerRoot, CurrentTime);
					XSelectInput(dpy, focused, EnterWindowMask | FocusChangeMask);

					int is_scratchpad = (scratchpad == ev.xmaprequest.window);

					if (!is_scratchpad && nclients < maxwindows)
						clients[nclients++] = getclient(ev.xmaprequest.window);

					if (tiling && !is_scratchpad)
						tile();

					bar_unclean = 1;
				}

				else if (ev.type == Expose && ev.xexpose.window == bar) {
					bar_unclean = 1;
				}

				else if (ev.type == ConfigureRequest) {
					XConfigureRequestEvent *e = &ev.xconfigurerequest;

					if (!tiling) {
						XWindowChanges wc;
						wc.x = e->x;
						wc.y = e->y;
						wc.width = e->width;
						wc.height = e->height;
						wc.border_width = e->border_width;
						wc.sibling = e->above;
						wc.stack_mode = e->detail;

						XConfigureWindow(dpy, e->window, e->value_mask, &wc);
					} else {
						XWindowChanges wc;
						wc.x = e->x;
						wc.y = e->y;
						wc.width = e->width;
						wc.height = e->height;
						wc.border_width = e->border_width;
						wc.sibling = e->above;
						wc.stack_mode = e->detail;

						XConfigureWindow(dpy, e->window, e->value_mask & CWStackMode, &wc);
						tile();
					}
				}

				else if (ev.type == EnterNotify) {
					Window w;

					if (ev.xcrossing.window == bar)
						continue;

					if (ev.xcrossing.mode != NotifyNormal)
						continue;

					if (ev.xcrossing.detail == NotifyInferior)
						continue;

					w = getclient(ev.xcrossing.window);
					if (w == None || w == bar)
						continue;

					if (w == focused)
						continue;

					if (focused != None)
						XSetWindowBorder(dpy, focused, border_normal);

					focused = w;
					XSetWindowBorderWidth(dpy, focused, border_width);
					XSetWindowBorder(dpy, focused, border_focus);
					XSetInputFocus(dpy, focused, RevertToPointerRoot, CurrentTime);
				}

				else if (ev.type == DestroyNotify) {
					Window w = ev.xdestroywindow.window;

					removeclient(w);

					if (w == scratchpad) {
						scratchpad = None;
						scratchpad_visible = 0;
					}

					if (w == focused) {
						focused = None;

						if (nclients > 0) {
							focused = clients[0];
							XSetWindowBorderWidth(dpy, focused, border_width);
							XSetWindowBorder(dpy, focused, border_focus);
							XSetInputFocus(dpy, focused, RevertToPointerRoot, CurrentTime);
						}
					}

					if (tiling)
						tile();

					bar_unclean = 1;
				}
				else if (ev.type == UnmapNotify) {
					Window w = ev.xunmap.window;

					if (w == bar)
						continue;

					if (w == scratchpad) {
						scratchpad_visible = 0;
						bar_unclean = 1;
						continue;
					}

					removeclient(w);

					if (w == focused) {
						focused = None;

						if (nclients > 0) {
							focused = clients[0];
							XSetWindowBorderWidth(dpy, focused, border_width);
							XSetWindowBorder(dpy, focused, border_focus);
							XSetInputFocus(dpy, focused, RevertToPointerRoot, CurrentTime);
						}
					}
					if (tiling)
						tile();

					bar_unclean = 1;
				}
			}
		}
	}
}


