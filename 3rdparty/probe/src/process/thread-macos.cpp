#ifdef __APPLE__

#include "probe/thread.h"

#include <pthread.h>

namespace probe::thread
{
    int set_name(const std::string& name)
    {
        return ::pthread_setname_np(name.substr(0, 15).c_str());
    }

    std::string name(uint64_t id)
    {
        char buffer[128];
        ::pthread_getname_np(::pthread_self(), buffer, 128);
        return buffer;
    }

    std::string name() { return name(0); }
} // namespace probe::thread

#endif
