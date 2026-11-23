#ifdef __APPLE__

#include "probe/system.h"

#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/sysctl.h>
#include <string>


namespace probe::system
{
    std::string name()
    {
        char buf[256];
        size_t size = sizeof(buf);

        if (sysctlbyname("kern.osproductversion", buf, &size, nullptr, 0) == 0) {
            return "MacOS" + std::string(buf);
        }

        return "MacOS";
    }

    theme_t theme()
    {
        // TODO : other desktop env
        return theme_t::light;
    }

    // https://gist.github.com/natefoo/814c5bf936922dad97ff
    version_t version()
    {
        char buf[256];
        size_t size = sizeof(buf);

        if (sysctlbyname("kern.osproductversion", buf, &size, nullptr, 0) == 0) {
            return to_version(buf);
        }
        return {};
    }

    std::string hostname()
    {
        // char buffer[256]{};
        // if (::gethostname(buffer, 256) == 0) {
        //     return buffer;
        // }
        return {};
    }

    std::string username()
    {
        // char buffer[256]{};
        // if (::getlogin_r(buffer, 256) == 0) {
        //     return buffer;
        // }
        return {};
    }

    namespace kernel
    {
        std::string name() { return "Darwin"; }

        version_t version()
        {
            char buf[256];
            size_t size = sizeof(buf);

            if (sysctlbyname("kern.osproductversion", buf, &size, nullptr, 0) == 0) {
                return to_version(buf);
            }
            return {};
        }
    } // namespace kernel
} // namespace probe::system

namespace probe::system
{
    desktop_environment_t desktop_environment()
    {
        return desktop_environment_t::MacOS;
    }

   version_t desktop_environment_version() { return version(); }
} // namespace probe::system

namespace probe::system
{
    windowing_system_t windowing_system()
    {
        return windowing_system_t::MacOS;
    }
} // namespace probe::system


#endif // __APPLE__
