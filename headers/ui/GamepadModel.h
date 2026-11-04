#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

// What the gamepad workspace knows about a game controller, independent of SDL: the state of the standard
// buttons and axes, where it came from, and the analysis built on top (fighting-game input history, stick
// trail, circularity test). SDL3 feeds it in the app (ui/imgui/SdlGamepadSource), a scripted pad in --demo
// and hand-made samples in the tests.
namespace ui::gamepad {

    // Buttons of a standard controller, named by position: South is A on an Xbox pad and Cross on a
    // PlayStation pad. The order matches SDL_GamepadButton, so the SDL source can index by it.
    enum class Button : std::uint8_t {
        South,
        East,
        West,
        North,
        Back,
        Guide,
        Start,
        LeftStick,
        RightStick,
        LeftShoulder,
        RightShoulder,
        DpadUp,
        DpadDown,
        DpadLeft,
        DpadRight,
        Count
    };
    inline constexpr std::size_t BUTTON_COUNT = static_cast<std::size_t>(Button::Count);

    // Same order as SDL_GamepadAxis.
    enum class Axis : std::uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };
    inline constexpr std::size_t AXIS_COUNT = static_cast<std::size_t>(Axis::Count);

    // Which names to show for the face buttons and the menu buttons.
    enum class LabelStyle : std::uint8_t { Xbox, PlayStation };

    // What the operating system says the controller is.
    enum class PadFamily : std::uint8_t { Xbox, PlayStation, Nintendo, Generic };

    inline constexpr std::uint16_t KEYCHRON_VENDOR_ID = 0x3434;

    struct PadInfo {
        std::uint32_t id{0};
        std::string   name;
        std::uint16_t vendor{0};
        std::uint16_t product{0};
        PadFamily     family{PadFamily::Generic};

        [[nodiscard]] bool isKeychron() const noexcept { return vendor == KEYCHRON_VENDOR_ID; }
        bool operator==(const PadInfo&) const = default;
    };

    // One reading of a controller. Sticks run -1 .. 1 with +x = right and +y = UP (the SDL source flips SDL's
    // "down is positive"), triggers 0 .. 1.
    struct PadState {
        std::array<float, AXIS_COUNT> axes{};
        std::array<bool, BUTTON_COUNT> buttons{};

        [[nodiscard]] bool  pressed(Button button) const noexcept { return buttons[static_cast<std::size_t>(button)]; }
        [[nodiscard]] float axis(Axis which) const noexcept { return axes[static_cast<std::size_t>(which)]; }
        void set(Button button, const bool down) noexcept { buttons[static_cast<std::size_t>(button)] = down; }
        void set(Axis which, const float value) noexcept { axes[static_cast<std::size_t>(which)] = value; }

        bool operator==(const PadState&) const = default;
    };

    // The controller's state right after a change, stamped with the time the OS delivered the change.
    struct Sample {
        std::uint64_t timestampNs{0};
        PadState      state;
    };

    // Where controller input comes from. Called on the UI thread only.
    class IGamepadSource {
    public:
        virtual ~IGamepadSource() = default;

        [[nodiscard]] virtual std::vector<PadInfo> pads() const = 0;
        [[nodiscard]] virtual std::optional<std::uint32_t> selectedPad() const = 0;
        virtual void selectPad(std::uint32_t id) = 0;

        // State of the selected pad (all released / centred without one).
        [[nodiscard]] virtual PadState current() const = 0;

        // Every state of the selected pad since the previous call, oldest first: one sample per change, so a
        // tap shorter than a frame still shows up in the input history.
        [[nodiscard]] virtual std::vector<Sample> takeSamples() = 0;

        // Monotonic clock in the samples' time base.
        [[nodiscard]] virtual std::uint64_t nowNs() const = 0;

        // "SDL 3.4.20 (xinput)" or "Demo".
        [[nodiscard]] virtual std::string backendName() const = 0;

        // While false (the window is hidden) the source may stop listening to save CPU.
        virtual void setActive(bool active) = 0;
    };

    [[nodiscard]] const char* buttonName(Button button, LabelStyle style) noexcept;  // "A" / "Cross", "LB" / "L1"
    [[nodiscard]] const char* axisName(Axis axis) noexcept;                            // "Left stick X"
    [[nodiscard]] const char* familyName(PadFamily family) noexcept;
    [[nodiscard]] LabelStyle  defaultLabels(PadFamily family) noexcept;

    // A trigger counts as pressed past half-way (fighting-game notation treats RT as a button).
    inline constexpr float TRIGGER_PRESS_THRESHOLD = 0.5f;
    // The stick has to leave this radius before it counts as a direction.
    inline constexpr float DIRECTION_THRESHOLD = 0.5f;

    // Numpad notation: 5 = neutral, 8 = up, 6 = forward (right), 2 = down, 3 = down-right, ...
    // The D-pad wins over the left stick when both are used.
    [[nodiscard]] int numpadDirection(const PadState& state, float threshold = DIRECTION_THRESHOLD) noexcept;

    // How the input history writes buttons.
    enum class Notation : std::uint8_t {
        PadLabels,     // A B X Y LB RB LT RT (or the PlayStation names)
        Tekken,        // 1 2 3 4 = left punch, right punch, left kick, right kick (X Y A B)
        StreetFighter, // LP MP HP / LK MK HK = X Y RB / A B RT
    };

    // The "attack" inputs the history tracks: face buttons, shoulders and the two triggers.
    enum class Attack : std::uint8_t { South, East, West, North, LeftShoulder, RightShoulder, LeftTrigger, RightTrigger, Count };
    inline constexpr std::size_t ATTACK_COUNT = static_cast<std::size_t>(Attack::Count);
    using AttackSet = std::array<bool, ATTACK_COUNT>;

    [[nodiscard]] AttackSet attacksOf(const PadState& state) noexcept;
    // Label of an attack in a notation, or nullptr when the notation has no name for it (Tekken has no RB).
    [[nodiscard]] const char* attackLabel(Attack attack, Notation notation, LabelStyle style) noexcept;

    // One line of the input history: a direction held together with a set of attack buttons.
    struct HistoryEntry {
        int           direction{5};
        AttackSet     held{};     // attack buttons held during this entry
        AttackSet     pressed{};  // ... of which these went down when the entry started
        std::uint64_t startNs{0};
        std::uint64_t endNs{0};   // 0 while it is the current entry

        [[nodiscard]] bool anyPressed() const noexcept;
        [[nodiscard]] bool anyHeld() const noexcept;
    };

    // Turns the stream of samples into fighting-game style history lines ("6 x12", "2 x3", "3+LK x1").
    // A new line starts whenever the direction or the set of held attacks changes; frame counts use the
    // game's frame rate (60 fps by default), so "x1" is a one-frame input.
    class InputHistory {
    public:
        explicit InputHistory(std::size_t capacity = 48, double framesPerSecond = 60.0);

        void push(const Sample& sample);
        void clear();

        // Newest first.
        [[nodiscard]] const std::deque<HistoryEntry>& entries() const noexcept { return m_entries; }

        // Length of an entry in frames (at least 1); the current entry is measured up to `nowNs`.
        [[nodiscard]] std::uint32_t frames(const HistoryEntry& entry, std::uint64_t nowNs) const noexcept;

        [[nodiscard]] double framesPerSecond() const noexcept { return m_fps; }
        void setFramesPerSecond(double fps) noexcept;

    private:
        std::size_t              m_capacity;
        double                   m_fps;
        std::deque<HistoryEntry> m_entries;
        bool                     m_started{false};
    };

    // Recent positions of a stick, for drawing a fading trail.
    class StickTrail {
    public:
        struct Point {
            float         x{0};
            float         y{0};
            std::uint64_t timestampNs{0};
        };

        explicit StickTrail(std::uint64_t lengthNs = 450'000'000ULL, std::size_t capacity = 256);

        void push(float x, float y, std::uint64_t timestampNs);
        // Drops points older than the trail length, measured from `nowNs`.
        void expire(std::uint64_t nowNs);
        void clear() noexcept { m_points.clear(); }

        [[nodiscard]] const std::deque<Point>& points() const noexcept { return m_points; }
        [[nodiscard]] std::uint64_t lengthNs() const noexcept { return m_lengthNs; }

    private:
        std::uint64_t     m_lengthNs;
        std::size_t       m_capacity;
        std::deque<Point> m_points;
    };

    // The classic stick test: roll the stick around its edge and see how round the result is. Every angle
    // bucket keeps the largest radius seen. A perfect circle gives 0 % error; a keyboard "stick" reaches the
    // corners of a square, so its diagonals read about 141 %.
    class CircularityTest {
    public:
        static constexpr std::size_t BUCKETS = 72; // 5 degrees each
        static constexpr float       MIN_RADIUS = 0.25f; // ignore the centre, where the angle means nothing

        void add(float x, float y) noexcept;
        void reset() noexcept;

        [[nodiscard]] const std::array<float, BUCKETS>& radii() const noexcept { return m_radii; }
        // Share of the buckets that have been reached (0 .. 1).
        [[nodiscard]] float coverage() const noexcept;
        // Mean |radius - 1| over the reached buckets, in percent; nullopt before anything was reached.
        [[nodiscard]] std::optional<float> averageErrorPercent() const noexcept;
        // Largest radius seen (1.41 for a square gate).
        [[nodiscard]] float maxRadius() const noexcept;

    private:
        std::array<float, BUCKETS> m_radii{};
    };

} // namespace ui::gamepad
