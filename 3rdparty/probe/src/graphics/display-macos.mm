#if defined(__APPLE__)

#include "probe/graphics.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <iostream>
#include <vector>

namespace probe::graphics
{
    std::vector<display_t> displays() {

        std::vector<display_t> _displays;
        @autoreleasepool {

            NSArray<NSScreen *> *screens = [NSScreen screens];
            CGDirectDisplayID mainID = CGMainDisplayID();

            for (NSScreen *screen in screens) {

                NSDictionary *desc = [screen deviceDescription];
                CGDirectDisplayID displayID =
                    (CGDirectDisplayID)[[desc objectForKey:@"NSScreenNumber"] unsignedIntValue];

                // Name
                std::string name = screen.localizedName.UTF8String;

                // Geometry (logical)
                CGRect bounds = CGDisplayBounds(displayID);

                // Scale (Retina)
                double scale = screen.backingScaleFactor;

                // Refresh rate
                CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
                double refreshRate = CGDisplayModeGetRefreshRate(mode);

                // BPP
                int bpp = 24;

                // Orientation
                double rotation = CGDisplayRotation(displayID);

                // Physical size (mm)
                CGSize sizeMM = CGDisplayScreenSize(displayID);

                // DPI (approx)
                double dpi = 0.0;
                if (sizeMM.width > 0) {
                    dpi = (CGDisplayPixelsWide(displayID) / (sizeMM.width / 25.4));
                }

                bool primary = (displayID == mainID);

                CGDisplayModeRelease(mode);

                //
                _displays.push_back(display_t{
                    .name      = name,
                    .id        = std::to_string(displayID),
                    .handle    = displayID,
                    .geometry  = geometry_t{ int32_t(bounds.origin.x), int32_t(bounds.origin.y),
                                           uint32_t(bounds.size.width), uint32_t(bounds.size.height) },
                    .frequency = refreshRate,
                    .bpp       = uint32_t(bpp),   // global
                    .dpi       = uint32_t(dpi), // global
                    .orientation =
                    static_cast<orientation_t>((int(rotation) & 0x000f) |
                                               static_cast<uint32_t>(!!(int(rotation) & 0x00f0))),
                    .primary = primary,
                    .scale   = scale,
                });

                // Output
                // std::cout << "-----------------------------\n";
                // std::cout << "name        : " << name << "\n";
                // std::cout << "id          : " << displayID << "\n";
                // std::cout << "handle      : " << displayID << "\n";
                // std::cout << "geometry    : "
                //           << bounds.origin.x << ", "
                //           << bounds.origin.y << ", "
                //           << bounds.size.width << "x"
                //           << bounds.size.height << "\n";
                // std::cout << "frequency   : " << refreshRate << " Hz\n";
                // std::cout << "bpp         : " << bpp << "\n";
                // std::cout << "dpi         : " << dpi << "\n";
                // std::cout << "orientation : " << rotation << "\n";
                // std::cout << "primary     : " << (primary ? "true" : "false") << "\n";
                // std::cout << "scale       : " << scale << "\n";
            }
        }

        return _displays;

    }

    std::deque<window_t> windows(window_filter_t, bool) { return {}; }

    std::optional<window_t> active_window() { return std::nullopt; }

    window_t virtual_screen()
    {
        return window_t{
            .name      = "~VIRTUAL-SCREEN",
            .classname = "",
            .geometry  = virtual_screen_geometry(),
            .visible   = true,
        };
    }
} // namespace probe::graphics

#endif // __APPLE__
