/* Print the name of the window that has focus */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    Display* display;
    XEvent an_event;
    char *display_name = getenv("DISPLAY");
    display = XOpenDisplay(display_name);
    if (display == NULL) {
        fprintf(stderr, "%s: cannot connect to X server '%s'\n",
                argv[0], display_name);
    }
    Window root_window = DefaultRootWindow(display);

    Atom property = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);

    //return values
    Atom type_return;
    int format_return;
    unsigned long nitems_return;
    unsigned long bytes_left;
    unsigned char *data;

    XSelectInput(display, root_window, FocusChangeMask);
    
    while (1) {
        XNextEvent(display, &an_event);

        printf("%d\n", an_event.type);

        // https://stackoverflow.com/questions/73833171/how-to-get-the-active-window-using-x11-xlib-c-api
        XGetWindowProperty(display,
                           root_window,
                           property,
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
        //printf("%s\n", *data[0]);

        Window active_window = ((Window*) data)[0];

        char* window_name;
        if (XFetchName(display, active_window, &window_name) != 0) {
            printf("The active window is: %s\n", window_name);
            XFree(window_name);
        }

        XClassHint class_hint;

        XGetClassHint(display, active_window, &class_hint);

        char * window_class = class_hint.res_class;
        char * window_foo = class_hint.res_name;

        printf("res.class = %s\n", window_class);
        printf("res.name = %s\n", window_foo);

        printf("\n\n");
    }

    // XFree(data);
    
    /* XFree(class_hint.res_name); */
    /* XFree(class_hint.res_class); */

    XCloseDisplay(display);
    return 0;
}
