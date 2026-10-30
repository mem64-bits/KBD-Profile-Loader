#pragma once

namespace ui {

    // A piece of UI that draws itself once per frame on the UI thread.
    class IView {
    public:
        virtual ~IView() = default;

        virtual void render() = 0;
        [[nodiscard]] virtual bool isVisible() const noexcept = 0;
        virtual void setVisible(bool visible) noexcept = 0;
    };

} // namespace ui
