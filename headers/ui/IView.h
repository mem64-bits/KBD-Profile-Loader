#pragma once

namespace UI {

    class IView {
    public:
        virtual ~IView() = default;
        virtual void render() = 0;
        [[nodiscard]] virtual bool isVisible() const noexcept = 0;
        virtual void setVisible(bool visible) noexcept = 0;
    };

} // namespace UI