#include "WindowEffect.h"

#if __APPLE__

#include <QWidget>
// #include <Cocoa/Cocoa.h>

void TransparentInput(QWidget* widget, bool enable)
{
    // if (!widget)
    //     return;

    // NSWindow* window = (NSWindow*)widget->window()->winId();
    // if (!window)
    //     return;

    // [window setIgnoresMouseEvents: enable];
}

#endif
