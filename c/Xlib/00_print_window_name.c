#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    Display* display;
    XEvent event;
    char *display_name = getenv("DISPLAY");

    display = XOpenDisplay(display_name);
    if (display == NULL) {
        fprintf(stderr, "%s: cannot connect to X server '%s'\n",
                argv[0], display_name);
    }

    Window root_window = DefaultRootWindow(display);
    Atom active_window_atom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);

    // Listen to any property change on the root window
    XSelectInput(display, root_window, PropertyChangeMask);

    //Return values of XGetWindowProperty
    Atom type_return;
    int format_return;
    unsigned long nitems_return;
    unsigned long bytes_left;
    unsigned char *data;

    while (1) {
        XNextEvent(display, &event);

        // We are only interested on the active window.
        if (event.xproperty.atom != active_window_atom) {
            printf("not interesting\n");
            continue;
        }

        printf("interesting\n");

        XGetWindowProperty(display,
                           root_window,
                           active_window_atom,
                           0,
                           1,
                           False,
                           XA_WINDOW,
                           &type_return,   //should be XA_WINDOW
                           &format_return, //should be 32
                           &nitems_return, //should be 1 (zero if there is no such window)
                           &bytes_left,    //should be 0 (i'm not sure but should be atomic read)
                           &data           //should be non-null
            );

        Window our_magic_window = *(Window *)data;

        if (our_magic_window == 0)
            continue;

        // Get name of the window
        char* window_name;
        if (XFetchName(display, our_magic_window, &window_name) != 0) {
            printf("The active window is: %s\n", window_name);
            XFree(window_name);
        }
        XClassHint class_hint;
        if (XGetClassHint(display, our_magic_window, &class_hint) == 0)
            continue;
        char * window_application_class = class_hint.res_class;
        char * window_application_name = class_hint.res_name;
        printf("res.class = %s\n", window_application_class);
        printf("res.name = %s\n", window_application_name);
        printf("\n\n");
    }

    XCloseDisplay(display);
    return 0;
}
