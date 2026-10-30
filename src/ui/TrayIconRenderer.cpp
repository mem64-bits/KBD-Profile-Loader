#include "ui/TrayIconRenderer.h"
#include "ui/AppState.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

    constexpr int   SUPERSAMPLE = 4;     // 4x4 samples per pixel -> smooth edges at every size
    constexpr float CORNER_RADIUS = 0.24f;
    constexpr float RIM_WIDTH = 0.075f;
    constexpr float DOT_RADIUS = 0.10f;
    constexpr float DOT_SPACING = 0.26f;

    // Signed distance (in unit-square coordinates) to the rounded plate: negative inside.
    float plateDistance(const float x, const float y) {
        const float halfExtent = 0.5f - CORNER_RADIUS;
        const float qx = std::abs(x - 0.5f) - halfExtent;
        const float qy = std::abs(y - 0.5f) - halfExtent;
        const float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f));
        const float inside  = std::min(std::max(qx, qy), 0.0f);
        return outside + inside - CORNER_RADIUS;
    }

    struct Palette {
        Color plate;
        Color rim;
        Color activeDot;
        Color idleDot;
    };

    Palette paletteFor(const Theme& theme, const bool connected) {
        const ThemePalette& p = theme.palette;
        if (connected) {
            return {p.background.withAlpha(255), p.accent, p.accentText, p.accent.mix(p.background, 0.60f)};
        }
        const Color grey = p.textMuted.mix(p.background, 0.55f);
        return {p.background.withAlpha(255), p.border, grey, grey};
    }

} // namespace

TrayImage renderTrayIcon(const int size, const Theme& theme, const int activeSlot, const bool connected) {
    TrayImage image;
    if (size <= 0) return image;

    image.width  = size;
    image.height = size;
    image.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4, 0);

    const Palette colors = paletteFor(theme, connected);
    const float   sizeF  = static_cast<float>(size);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, coverage = 0.0f;

            for (int sy = 0; sy < SUPERSAMPLE; ++sy) {
                for (int sx = 0; sx < SUPERSAMPLE; ++sx) {
                    const float px = (static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / SUPERSAMPLE) / sizeF;
                    const float py = (static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / SUPERSAMPLE) / sizeF;

                    const float distance = plateDistance(px, py);
                    if (distance > 0.0f) continue; // outside the plate: transparent

                    Color color = distance > -RIM_WIDTH ? colors.rim : colors.plate;
                    for (std::size_t slot = 0; slot < SLOT_COUNT; ++slot) {
                        const float cx = 0.5f + (static_cast<float>(slot) - 1.0f) * DOT_SPACING;
                        const float dx = px - cx;
                        const float dy = py - 0.5f;
                        if (dx * dx + dy * dy <= DOT_RADIUS * DOT_RADIUS) {
                            color = (connected && static_cast<int>(slot) == activeSlot) ? colors.activeDot : colors.idleDot;
                        }
                    }

                    sumR += static_cast<float>(color.r);
                    sumG += static_cast<float>(color.g);
                    sumB += static_cast<float>(color.b);
                    coverage += 1.0f;
                }
            }

            if (coverage <= 0.0f) continue;

            const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4;
            image.rgba[offset + 0] = static_cast<std::uint8_t>(std::lround(sumR / coverage));
            image.rgba[offset + 1] = static_cast<std::uint8_t>(std::lround(sumG / coverage));
            image.rgba[offset + 2] = static_cast<std::uint8_t>(std::lround(sumB / coverage));
            image.rgba[offset + 3] = static_cast<std::uint8_t>(std::lround(255.0f * coverage / (SUPERSAMPLE * SUPERSAMPLE)));
        }
    }
    return image;
}

std::vector<TrayImage> renderTrayIcons(const Theme& theme, const int activeSlot, const bool connected) {
    std::vector<TrayImage> images;
    images.reserve(TRAY_ICON_SIZES.size());
    for (const int size : TRAY_ICON_SIZES) {
        images.push_back(renderTrayIcon(size, theme, activeSlot, connected));
    }
    return images;
}

TrayImage scaleTrayImage(const TrayImage& source, const int size) {
    TrayImage scaled;
    if (size <= 0 || source.width <= 0 || source.height <= 0) return scaled;

    scaled.width  = size;
    scaled.height = size;
    scaled.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4, 0);

    for (int y = 0; y < size; ++y) {
        const int y0 = y * source.height / size;
        const int y1 = std::max(y0 + 1, (y + 1) * source.height / size);

        for (int x = 0; x < size; ++x) {
            const int x0 = x * source.width / size;
            const int x1 = std::max(x0 + 1, (x + 1) * source.width / size);

            // Average in premultiplied space so transparent pixels do not bleed their colour into the edge.
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            int   count = 0;
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    const std::size_t i = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(source.width) + static_cast<std::size_t>(sx)) * 4;
                    const float alpha = static_cast<float>(source.rgba[i + 3]) / 255.0f;
                    r += static_cast<float>(source.rgba[i + 0]) * alpha;
                    g += static_cast<float>(source.rgba[i + 1]) * alpha;
                    b += static_cast<float>(source.rgba[i + 2]) * alpha;
                    a += alpha;
                    ++count;
                }
            }

            const std::size_t o = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4;
            if (a > 0.0f) {
                scaled.rgba[o + 0] = static_cast<std::uint8_t>(std::lround(r / a));
                scaled.rgba[o + 1] = static_cast<std::uint8_t>(std::lround(g / a));
                scaled.rgba[o + 2] = static_cast<std::uint8_t>(std::lround(b / a));
                scaled.rgba[o + 3] = static_cast<std::uint8_t>(std::lround(255.0f * a / static_cast<float>(count)));
            }
        }
    }
    return scaled;
}

} // namespace ui
