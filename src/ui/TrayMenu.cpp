#include "ui/TrayMenu.h"

#include <algorithm>

namespace ui {

const TrayMenuItem* TrayMenu::find(const int id) const {
    const auto it = std::find_if(items.begin(), items.end(), [id](const TrayMenuItem& item) { return item.id == id; });
    return it == items.end() ? nullptr : &*it;
}

} // namespace ui
