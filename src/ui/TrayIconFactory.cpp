#include "ui/TrayIconFactory.h"
#include "ui/NullTrayIcon.h"

#if defined(_WIN32)
    #include "ui/Win32TrayIcon.h"
#elif defined(KBD_HAVE_SNI_TRAY)
    #include "ui/SniTrayIcon.h"
#endif

namespace ui {

std::unique_ptr<ITrayIcon> createTrayIcon() {
#if defined(_WIN32)
    return std::make_unique<Win32TrayIcon>();
#elif defined(KBD_HAVE_SNI_TRAY)
    return std::make_unique<SniTrayIcon>();
#else
    return std::make_unique<NullTrayIcon>();
#endif
}

} // namespace ui
