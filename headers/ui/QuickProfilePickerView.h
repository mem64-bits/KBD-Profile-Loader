#pragma once

#include "ui/IView.h"
#include <string>
#include <vector>
#include <functional>

namespace kbd { class ProfileService; }

namespace UI {

    class QuickProfilePickerView : public IView {
    public:
        explicit QuickProfilePickerView(kbd::ProfileService& profileService);

        void render() override;

        [[nodiscard]] bool isVisible() const noexcept override { return m_visible; }
        void setVisible(const bool visible) noexcept override         { m_visible = visible; }
        void toggleVisibility() noexcept                        { m_visible = !m_visible; }

    private:
        kbd::ProfileService& m_profileService;
        bool                 m_visible = false;
    };

} // namespace ui