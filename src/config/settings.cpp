#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <glyphshift/config/settings.hpp>
#include <mutex>
#include <string>

namespace glyphshift {
namespace {
std::string_view trim(std::string_view text) {
    constexpr auto whitespace = " \t\r";
    const auto start = text.find_first_not_of(whitespace);
    if (start == std::string_view::npos)
        return {};
    return text.substr(start, text.find_last_not_of(whitespace) - start + 1);
}

std::string_view uncomment(std::string_view text) {
    bool quoted = false, escaped = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\' && quoted) {
            escaped = true;
            continue;
        }
        if (c == '"')
            quoted = !quoted;
        if (c == '#' && !quoted)
            return text.substr(0, i);
    }
    return text;
}

bool string_value(std::string_view text, std::array<char, 1025>& output) {
    output.fill(0);
    const bool quoted = !text.empty() && text.front() == '"';
    if (quoted) {
        if (text.size() < 2 || text.back() != '"')
            return false;
        text = text.substr(1, text.size() - 2);
    }
    std::size_t at = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quoted && c == '\\') {
            if (++i == text.size() || (text[i] != '\\' && text[i] != '"'))
                return false;
            c = text[i];
        } else if (quoted && c == '"')
            return false;
        if (static_cast<unsigned char>(c) < 32 || at + 1 >= output.size())
            return false;
        output[at++] = c;
    }
    return true;
}

bool cell(std::string_view text, unsigned& output) {
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 2 || value > 128)
        return false;
    output = value;
    return true;
}

bool number(std::string_view text, float& output, float low, float high) {
    float value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value) ||
        value < low || value > high)
        return false;
    output = value;
    return true;
}

bool rgb(std::string_view text, std::array<float, 3>& output) {
    std::array<float, 3> value{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto comma = text.find(',');
        if ((i < 2) != (comma != std::string_view::npos) ||
            !number(trim(text.substr(0, comma)), value[i], 0, 1))
            return false;
        if (i < 2)
            text.remove_prefix(comma + 1);
    }
    output = value;
    return true;
}

void warning(const char* message, std::size_t size) noexcept {
    const auto ignored = write(STDERR_FILENO, message, size);
    (void)ignored;
}

template <std::size_t N>
void warning(const char (&message)[N]) noexcept {
    warning(message, N - 1);
}

struct Stamp {
    dev_t device{};
    ino_t inode{};
    off_t size{};
    timespec modified{};
    int error = -1;

    bool operator==(const Stamp& other) const {
        return device == other.device && inode == other.inode && size == other.size &&
               modified.tv_sec == other.modified.tv_sec &&
               modified.tv_nsec == other.modified.tv_nsec && error == other.error;
    }
};

class Runtime {
public:
    Runtime() {
        if (const char* path = std::getenv("GLYPHSHIFT_CONFIG"))
            path_ = path;
    }

    Settings get() {
        if (path_.empty())
            return {};
        const std::lock_guard lock(mutex_);
        struct stat status{};
        Stamp stamp;
        if (stat(path_.c_str(), &status) != 0)
            stamp.error = errno;
        else
            stamp = {status.st_dev, status.st_ino, status.st_size, status.st_mtim, 0};
        if (stamp == previous_)
            return current_;
        previous_ = stamp;
        current_ = {};
        if (stamp.error || !S_ISREG(status.st_mode) || status.st_size > 4096) {
            warning(
                "glyphshift: config must be a readable regular file of at most 4096 "
                "bytes; using defaults\n");
            return current_;
        }
        const int fd = open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
            warning("glyphshift: cannot read config; using defaults\n");
            return current_;
        }
        char buffer[4097];
        struct stat opened{};
        const auto count = fstat(fd, &opened) == 0 && S_ISREG(opened.st_mode)
                               ? read(fd, buffer, sizeof(buffer))
                               : -1;
        close(fd);
        if (count < 0 || count > 4096) {
            warning(
                "glyphshift: cannot read config or config too large; using "
                "defaults\n");
            return current_;
        }
        const auto parsed = parse_settings({buffer, static_cast<std::size_t>(count)});
        current_ = parsed.value;
        if (parsed.warnings & invalid_art)
            warning("glyphshift: invalid illustration setting; using safe defaults\n");
        if (parsed.warnings & invalid_width)
            warning("glyphshift: invalid cell_width; using 8 (range 2..128)\n");
        if (parsed.warnings & invalid_height)
            warning("glyphshift: invalid cell_height; using 12 (range 2..128)\n");
        if (parsed.warnings & invalid_color)
            warning("glyphshift: invalid color_mode; using source (source/mono)\n");
        if (parsed.warnings & invalid_characters)
            warning("glyphshift: invalid character selection; using classic\n");
        if (parsed.warnings & invalid_strength)
            warning("glyphshift: invalid glyph_strength; using 0.45 (range 0..1)\n");
        if (parsed.warnings & invalid_enabled)
            warning("glyphshift: invalid enabled; using true (true/false/1/0)\n");
        if (parsed.warnings & unknown_setting)
            warning("glyphshift: unknown/malformed setting ignored\n");
        return current_;
    }

private:
    std::string path_;
    std::mutex mutex_;
    Stamp previous_;
    Settings current_;
};
}  // namespace

ParsedSettings parse_settings(std::string_view text) noexcept {
    ParsedSettings parsed;
    bool explicit_set = false, custom_text = false, explicit_order = false;
    const Settings defaults;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        line = trim(uncomment(line));
        if (line.empty())
            continue;
        const auto equal = line.find('=');
        if (equal == std::string_view::npos) {
            parsed.warnings |= unknown_setting;
            continue;
        }
        const auto key = trim(line.substr(0, equal));
        const auto value = trim(line.substr(equal + 1));
        if (key == "cell_width") {
            parsed.value.cell_width = defaults.cell_width;
            if (!cell(value, parsed.value.cell_width))
                parsed.warnings |= invalid_width;
        } else if (key == "cell_height") {
            parsed.value.cell_height = defaults.cell_height;
            if (!cell(value, parsed.value.cell_height))
                parsed.warnings |= invalid_height;
        } else if (key == "color_mode") {
            parsed.value.source_color = defaults.source_color;
            if (value == "mono")
                parsed.value.source_color = false;
            else if (value != "source")
                parsed.warnings |= invalid_color;
        } else if (key == "glyph_strength") {
            float strength = defaults.glyph_strength;
            const auto [parsed_end, error] =
                std::from_chars(value.data(), value.data() + value.size(), strength);
            parsed.value.glyph_strength = defaults.glyph_strength;
            if (error != std::errc{} || parsed_end != value.data() + value.size() ||
                !std::isfinite(strength) || strength < 0 || strength > 1)
                parsed.warnings |= invalid_strength;
            else
                parsed.value.glyph_strength = strength;
        } else if (key == "render_style") {
            parsed.value.art.illustrated = value == "illustrated";
            if (value != "classic" && value != "illustrated")
                parsed.warnings |= invalid_art;
        } else if (key == "tone_exposure" || key == "tone_contrast" || key == "tone_gamma" ||
                   key == "edge_strength" || key == "edge_threshold" || key == "shape_threshold" ||
                   key == "scene_paper") {
            float* target = key == "tone_exposure"     ? &parsed.value.art.exposure
                            : key == "tone_contrast"   ? &parsed.value.art.contrast
                            : key == "tone_gamma"      ? &parsed.value.art.gamma
                            : key == "edge_strength"   ? &parsed.value.art.edge_strength
                            : key == "shape_threshold" ? &parsed.value.art.shape_threshold
                            : key == "scene_paper"     ? &parsed.value.art.scene_paper
                                                       : &parsed.value.art.edge_threshold;
            const float fallback = key == "edge_threshold"    ? defaults.art.edge_threshold
                                   : key == "shape_threshold" ? defaults.art.shape_threshold
                                   : key == "scene_paper"     ? defaults.art.scene_paper
                                                              : 1;
            *target = fallback;
            const bool edge =
                key.starts_with("edge_") || key == "shape_threshold" || key == "scene_paper";
            if (!number(value, *target, edge ? 0 : .25F, edge ? 1 : 4))
                parsed.warnings |= invalid_art;
        } else if (key == "saturation" || key == "glow") {
            const bool glow = key == "glow";
            float& target = glow ? parsed.value.art.glow : parsed.value.art.saturation;
            target = glow ? defaults.art.glow : defaults.art.saturation;
            if (!number(value, target, 0, glow ? 1 : 2))
                parsed.warnings |= invalid_art;
        } else if (key == "fill_enabled" || key == "contour_overlay" || key == "shape_matching" ||
                   key == "paper_wash") {
            auto& target = key == "fill_enabled"     ? parsed.value.art.fill
                           : key == "shape_matching" ? parsed.value.art.shape_matching
                           : key == "paper_wash"     ? parsed.value.art.paper_wash
                                                     : parsed.value.art.contour_overlay;
            target = key != "shape_matching" && key != "paper_wash";
            if (value != "false" && value != "0" && value != "true" && value != "1")
                parsed.warnings |= invalid_art;
            else
                target = value == "true" || value == "1";
        } else if (key == "foreground" || key == "background") {
            auto& target =
                key == "foreground" ? parsed.value.art.foreground : parsed.value.art.background;
            target = key == "foreground" ? defaults.art.foreground : defaults.art.background;
            if (!rgb(value, target))
                parsed.warnings |= invalid_art;
        } else if (key == "edge_characters") {
            if (!string_value(value, parsed.value.glyphs.edges))
                parsed.warnings |= invalid_characters;
        } else if (key == "shape_characters") {
            if (!string_value(value, parsed.value.glyphs.shapes))
                parsed.warnings |= invalid_characters;
        } else if (key == "character_set") {
            explicit_set = true;
            bool found = false;
            for (unsigned i = 0; i < character_sets.size(); ++i)
                if (value == character_sets[i]) {
                    parsed.value.glyphs.set = i;
                    found = true;
                }
            if (!found)
                parsed.warnings |= invalid_characters;
        } else if (key == "characters") {
            custom_text = true;
            if (!string_value(value, parsed.value.glyphs.text))
                parsed.warnings |= invalid_characters;
        } else if (key == "glyph_font") {
            if (!string_value(value, parsed.value.glyphs.font))
                parsed.warnings |= invalid_characters;
        } else if (key == "glyph_weight") {
            if (!number(value, parsed.value.glyphs.weight, 0, 1))
                parsed.warnings |= invalid_characters;
        } else if (key == "glyph_order") {
            explicit_order = true;
            if (value == "density")
                parsed.value.glyphs.density_order = true;
            else if (value == "manual")
                parsed.value.glyphs.density_order = false;
            else
                parsed.warnings |= invalid_characters;
        } else if (key == "glyph_reverse") {
            if (value == "true" || value == "1")
                parsed.value.glyphs.reverse = true;
            else if (value == "false" || value == "0")
                parsed.value.glyphs.reverse = false;
            else
                parsed.warnings |= invalid_characters;
        } else if (key == "enabled") {
            parsed.value.enabled = defaults.enabled;
            if (value == "false" || value == "0")
                parsed.value.enabled = false;
            else if (value != "true" && value != "1")
                parsed.warnings |= invalid_enabled;
        } else
            parsed.warnings |= unknown_setting;
    }
    if (custom_text && !explicit_set)
        parsed.value.glyphs.set = 6;
    if (!explicit_order && (parsed.value.glyphs.set == 1 || parsed.value.glyphs.set == 2 ||
                               parsed.value.glyphs.set == 5))
        parsed.value.glyphs.density_order = true;
    if (!valid_characters(parsed.value.glyphs))
        parsed.warnings |= invalid_characters;
    if (parsed.warnings & invalid_characters)
        parsed.value.glyphs = {};
    return parsed;
}

bool valid_art(const ArtSettings& a) noexcept {
    if (!std::isfinite(a.saturation) || a.saturation < 0 || a.saturation > 2)
        return false;
    for (const float v : {a.exposure, a.contrast, a.gamma})
        if (!std::isfinite(v) || v < .25F || v > 4)
            return false;
    for (const float v : {a.edge_strength,
             a.edge_threshold,
             a.shape_threshold,
             a.scene_paper,
             a.glow,
             a.foreground[0],
             a.foreground[1],
             a.foreground[2],
             a.background[0],
             a.background[1],
             a.background[2]})
        if (!std::isfinite(v) || v < 0 || v > 1)
            return false;
    return true;
}

Settings runtime_settings() noexcept {
    try {
        static Runtime runtime;
        return runtime.get();
    } catch (...) {
        static const bool reported = [] {
            warning("glyphshift: settings unavailable; using defaults\n");
            return true;
        }();
        (void)reported;
        return {};
    }
}
}  // namespace glyphshift
