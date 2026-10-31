#include "artist_background_image.hpp"
#include "metadata_cache.hpp"
#include "http_security_policy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
constexpr std::size_t image_limit = artist_background_image_limit;
constexpr MetadataCachePolicy image_policy{std::chrono::days(30), 64ULL * 1024 * 1024};

bool admitted_dimensions(std::uint32_t width, std::uint32_t height) {
    return width != 0 && height != 0 && width <= 4096 && height <= 4096 &&
        static_cast<std::uint64_t>(width) * height <= 8388608;
}

bool allowed_candidate(std::string_view mbid, const ArtistBackgroundCandidate& candidate) {
    if (mbid.size() != 36 || candidate.width == 0 || candidate.height == 0 ||
        candidate.id.empty() || candidate.id.size() > 64 ||
        !std::all_of(candidate.id.begin(), candidate.id.end(), [](unsigned char c) {
            return std::isdigit(c);
        })) return false;
    for (std::size_t i = 0; i < mbid.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (mbid[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(mbid[i]))) return false;
    }
    return is_allowed_artist_background_url(candidate.url);
}

std::uint32_t png_u32(std::string_view bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
    return value;
}

unsigned jpeg_u16(std::string_view bytes, std::size_t offset) {
    return (static_cast<unsigned char>(bytes[offset]) << 8) |
        static_cast<unsigned char>(bytes[offset + 1]);
}

bool admitted_jpeg(std::string_view bytes) {
    if (bytes.size() < 4 || bytes.size() > image_limit || bytes.substr(0, 2) != "\xff\xd8") return false;
    struct Component { unsigned id, h, v, q; };
    std::array<Component, 4> components{};
    std::array<bool, 4> quant{}, quant_used{};
    std::array<std::array<unsigned char, 64>, 4> quant_values{};
    std::array<bool, 8> huffman{};
    std::array<std::array<int, 64>, 4> planes;
    for (auto& plane : planes) plane.fill(-1);
    unsigned count = 0, process = 0, scans = 0, restart = 0, next_restart = 0;
    bool entropy = false, segment_data = false;
    std::size_t offset = 2;
    while (offset < bytes.size()) {
        if (entropy && static_cast<unsigned char>(bytes[offset]) != 255) {
            segment_data = true;
            ++offset;
            continue;
        }
        if (static_cast<unsigned char>(bytes[offset++]) != 255) return false;
        unsigned fill = 0;
        while (offset < bytes.size() && static_cast<unsigned char>(bytes[offset]) == 255) {
            ++offset;
            ++fill;
        }
        if (offset == bytes.size()) return false;
        const auto marker = static_cast<unsigned char>(bytes[offset++]);
        if (entropy && marker == 0) {
            if (fill != 0) return false; // Only FF00 is a stuffed data byte.
            segment_data = true;
            continue;
        }
        if (entropy && marker >= 0xd0 && marker <= 0xd7) {
            if (!restart || !segment_data || marker != 0xd0 + next_restart) return false;
            next_restart = (next_restart + 1) % 8;
            segment_data = false;
            continue;
        }
        if (entropy) {
            if (!segment_data) return false;
            entropy = false;
        }
        if (marker == 0xd9) {
            if (!process || !scans || offset != bytes.size()) return false;
            for (unsigned i = 0; i < count; ++i) if (planes[i][0] < 0) return false;
            return true;
        }
        if (marker == 0 || marker == 0xd8 || marker == 0xdc || marker == 0xde || marker == 0xdf ||
            marker == 1 || (marker >= 0xd0 && marker <= 0xd7)) return false;
        if (bytes.size() - offset < 2) return false;
        const auto length = jpeg_u16(bytes, offset);
        if (length < 2 || length > bytes.size() - offset) return false;
        const auto payload = bytes.substr(offset + 2, length - 2);
        offset += length;
        if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2) {
            if (process || payload.size() < 6 || payload[0] != 8) return false;
            count = static_cast<unsigned char>(payload[5]);
            if ((count != 1 && count != 3 && count != 4) || payload.size() != 6 + 3 * count ||
                !admitted_dimensions(jpeg_u16(payload, 3), jpeg_u16(payload, 1))) return false;
            std::array<bool, 256> ids{};
            for (unsigned i = 0; i < count; ++i) {
                const auto id = static_cast<unsigned char>(payload[6 + 3 * i]);
                const unsigned sampling = static_cast<unsigned char>(payload[7 + 3 * i]);
                const auto q = static_cast<unsigned char>(payload[8 + 3 * i]);
                if (ids[id] || (sampling >> 4) == 0 || (sampling >> 4) > 4 ||
                    (sampling & 15) == 0 || (sampling & 15) > 4 || q > 3) return false;
                ids[id] = true;
                components[i] = {id, sampling >> 4, sampling & 15, q};
            }
            process = marker;
        } else if (marker == 0xdb) {
            if (payload.empty()) return false;
            for (std::size_t p = 0; p < payload.size();) {
                if (payload.size() - p < 65) return false;
                const auto table = static_cast<unsigned char>(payload[p++]);
                if (table > 3) return false; // Product profile: only 8-bit quantizers.
                std::array<unsigned char, 64> values{};
                for (auto& value : values) {
                    value = static_cast<unsigned char>(payload[p++]);
                    if (value == 0) return false;
                }
                if (process == 0xc2 && quant_used[table] && quant_values[table] != values) return false;
                quant[table] = true;
                quant_values[table] = values;
            }
        } else if (marker == 0xc4) {
            if (payload.empty()) return false;
            for (std::size_t p = 0; p < payload.size();) {
                if (payload.size() - p < 17) return false;
                const auto table = static_cast<unsigned char>(payload[p++]);
                if ((table >> 4) > 1 || (table & 15) > 3) return false;
                unsigned symbols = 0;
                int available = 1;
                for (unsigned i = 0; i < 16; ++i) {
                    const auto codes = static_cast<unsigned char>(payload[p++]);
                    available = available * 2 - codes;
                    symbols += codes;
                    if (available < 0) return false;
                }
                if (available == 0 || symbols == 0 || symbols > 256 || symbols > payload.size() - p) return false;
                p += symbols;
                huffman[(table >> 4) * 4 + (table & 15)] = true;
            }
        } else if (marker == 0xdd) {
            if (payload.size() != 2) return false;
            restart = jpeg_u16(payload, 0);
        } else if (marker == 0xda) {
            if (!process || payload.size() < 6) return false;
            const unsigned n = static_cast<unsigned char>(payload[0]);
            if (n == 0 || n > count || payload.size() != 4 + 2 * n) return false;
            const auto ss = static_cast<unsigned char>(payload[1 + 2 * n]);
            const auto se = static_cast<unsigned char>(payload[2 + 2 * n]);
            const auto approx = static_cast<unsigned char>(payload[3 + 2 * n]);
            const unsigned ah = approx >> 4, al = approx & 15;
            if (process == 0xc2) {
                if (ss > se || se > 63 || (ss == 0 ? se != 0 : n != 1) || ah > 13 || al > 13 ||
                    (ah != 0 && al + 1 != ah)) return false;
            } else if (ss != 0 || se != 63 || approx != 0) return false;
            unsigned previous = 0, blocks = 0;
            for (unsigned i = 0; i < n; ++i) {
                const auto id = static_cast<unsigned char>(payload[1 + 2 * i]);
                const auto tables = static_cast<unsigned char>(payload[2 + 2 * i]);
                unsigned component = 0;
                while (component < count && components[component].id != id) ++component;
                if (component == count || (i != 0 && component <= previous)) return false;
                previous = component;
                blocks += components[component].h * components[component].v;
                const unsigned dc = tables >> 4, ac = tables & 15;
                const unsigned limit = process == 0xc0 ? 1 : 3;
                if (dc > limit || ac > limit || !quant[components[component].q]) return false;
                if ((process != 0xc2 || (ss == 0 && ah == 0)) && !huffman[dc]) return false;
                if ((process != 0xc2 || ss != 0) && !huffman[4 + ac]) return false;
                auto& plane = planes[component];
                if (process == 0xc2) {
                    if (ss != 0 && plane[0] < 0) return false;
                    for (unsigned k = ss; k <= se; ++k) {
                        if (ah == 0 ? plane[k] != -1 : plane[k] != static_cast<int>(ah)) return false;
                        plane[k] = static_cast<int>(al);
                    }
                } else {
                    if (plane[0] != -1) return false;
                    plane.fill(0);
                }
                quant_used[components[component].q] = true;
            }
            if (n > 1 && blocks > 10) return false;
            ++scans;
            entropy = true;
            segment_data = false;
            next_restart = 0;
        } else if (!(marker >= 0xe0 && marker <= 0xef) && marker != 0xfe) {
            return false;
        }
    }
    return false;
}

std::uint32_t png_crc(std::string_view bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return ~crc;
}

// Admission inspects chunks, not the compressed pixel or ancillary payloads.
bool admitted_png(std::string_view bytes) {
    if (bytes.size() > image_limit || bytes.substr(0, 8) != "\x89PNG\r\n\x1a\n") return false;
    std::size_t offset = 8;
    unsigned depth = 0, color = 0;
    bool header = false, palette = false, data = false, data_ended = false, nonempty_data = false;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 12) return false;
        const auto length = png_u32(bytes, offset);
        if (length > 0x7fffffffU || length > bytes.size() - offset - 12) return false;
        const auto type = bytes.substr(offset + 4, 4);
        for (const unsigned char letter : type)
            if (!((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z'))) return false;
        if (type[2] < 'A' || type[2] > 'Z') return false;
        const auto payload = bytes.substr(offset + 8, length);
        if (png_crc(bytes.substr(offset + 4, length + 4)) != png_u32(bytes, offset + 8 + length))
            return false;
        offset += length + 12;
        if (!header && type != "IHDR") return false;
        if (type == "IHDR") {
            if (header || length != 13) return false;
            const auto width = png_u32(payload, 0), height = png_u32(payload, 4);
            if (!admitted_dimensions(width, height)) return false;
            depth = static_cast<unsigned char>(payload[8]);
            color = static_cast<unsigned char>(payload[9]);
            const bool legal_depth = (color == 0 || color == 3) ?
                (depth == 1 || depth == 2 || depth == 4 || depth == 8) :
                ((color == 2 || color == 4 || color == 6) && depth == 8);
            if (!legal_depth || payload[10] != 0 || payload[11] != 0 ||
                (payload[12] != 0 && payload[12] != 1)) return false;
            header = true;
        } else if (type == "IDAT") {
            if (data_ended || (color == 3 && !palette)) return false;
            data = true;
            nonempty_data |= !payload.empty();
        } else {
            if (data) data_ended = true;
            if (type == "PLTE") {
                if (palette || data || color == 0 || color == 4 || length == 0 ||
                    length > 768 || length % 3 != 0 ||
                    (color == 3 && length / 3 > (1U << depth))) return false;
                palette = true;
            } else if (type == "IEND") {
                return length == 0 && data && nonempty_data && offset == bytes.size();
            } else if (type == "acTL" || type == "fcTL" || type == "fdAT" ||
                       (type[0] >= 'A' && type[0] <= 'Z')) {
                return false;
            }
        }
    }
    return false;
}

std::uint32_t webp_uint(std::string_view bytes, std::size_t offset, unsigned count) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    return value;
}

bool admitted_webp(std::string_view bytes) {
    if (bytes.size() < 12 || bytes.size() > image_limit || bytes.substr(0, 4) != "RIFF" ||
        bytes.substr(8, 4) != "WEBP" || webp_uint(bytes, 4, 4) != bytes.size() - 8) return false;
    std::size_t offset = 12;
    std::uint32_t canvas_width = 0, canvas_height = 0;
    unsigned flags = 0;
    bool extended = false, image = false, lossless = false, alpha = false;
    bool icc = false, exif = false, xmp = false;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 8) return false;
        const auto type = bytes.substr(offset, 4);
        const auto length = webp_uint(bytes, offset + 4, 4);
        if (length > bytes.size() - offset - 8) return false;
        const auto payload = bytes.substr(offset + 8, length);
        const bool first = offset == 12;
        offset += 8 + static_cast<std::size_t>(length);
        if (length & 1U) {
            if (offset == bytes.size() || bytes[offset] != 0) return false;
            ++offset;
        }
        if (type == "ANIM" || type == "ANMF") return false;
        if (type == "VP8X") {
            if (!first || extended || length != 10) return false;
            flags = static_cast<unsigned char>(payload[0]);
            if ((flags & 0xc3U) || payload[1] != 0 || payload[2] != 0 || payload[3] != 0) return false;
            canvas_width = webp_uint(payload, 4, 3) + 1;
            canvas_height = webp_uint(payload, 7, 3) + 1;
            if (!admitted_dimensions(canvas_width, canvas_height)) return false;
            extended = true;
        } else if (type == "VP8 " || type == "VP8L") {
            if (image) return false;
            std::uint32_t width = 0, height = 0;
            lossless = type == "VP8L";
            if (lossless) {
                if (alpha || length <= 5 || static_cast<unsigned char>(payload[0]) != 0x2f) return false;
                const auto bits = webp_uint(payload, 1, 4);
                if (bits >> 29) return false;
                width = (bits & 0x3fffU) + 1;
                height = ((bits >> 14) & 0x3fffU) + 1;
                // alpha_is_used is only a hint, not a dimension or admission rule.
            } else {
                if (length <= 10 || payload.substr(3, 3) != "\x9d\x01\x2a") return false;
                const auto tag = webp_uint(payload, 0, 3);
                const auto partition = tag >> 5;
                if ((tag & 1U) || !(tag & 16U) || ((tag >> 1) & 7U) > 3 ||
                    partition == 0 || partition > length - 10) return false;
                width = webp_uint(payload, 6, 2);
                height = webp_uint(payload, 8, 2);
                if ((width | height) & 0xc000U) return false;
            }
            if (!admitted_dimensions(width, height) ||
                (extended && (width != canvas_width || height != canvas_height))) return false;
            image = true;
        } else if (type == "ALPH") {
            if (!extended || image || alpha || !(flags & 16U) || payload.empty()) return false;
            const auto header = static_cast<unsigned char>(payload[0]);
            if ((header & 0xc0U) || ((header >> 4) & 3U) > 1 || (header & 3U) > 1) return false;
            if ((header & 3U) == 0 ? length != 1 + canvas_width * canvas_height : length <= 1)
                return false;
            alpha = true;
        } else if (type == "ICCP" || type == "EXIF" || type == "XMP ") {
            if (!extended || payload.empty()) return false;
            if (type == "ICCP") {
                if (icc || image || alpha || !(flags & 32U)) return false;
                icc = true;
            } else if (type == "EXIF") {
                if (exif || !(flags & 8U)) return false;
                exif = true;
            } else {
                if (xmp || !(flags & 4U)) return false;
                xmp = true;
            }
        } else if (!extended && !image) {
            return false;
        }
    }
    return image && (!extended ||
        (((flags & 32U) != 0) == icc && ((flags & 8U) != 0) == exif && ((flags & 4U) != 0) == xmp &&
         (lossless ? !alpha : ((flags & 16U) != 0) == alpha)));
}

std::optional<std::string> image_mime(std::string_view bytes) {
    if (admitted_jpeg(bytes)) return "image/jpeg";
    if (admitted_png(bytes)) return "image/png";
    if (admitted_webp(bytes)) return "image/webp";
    return std::nullopt;
}

std::string url_hash(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : value) hash = (hash ^ c) * 1099511628211ULL;
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}
bool matching_content_type(std::string_view content_type, std::string_view mime) {
    return content_type.starts_with(mime) &&
        (content_type.size() == mime.size() || content_type[mime.size()] == ';');
}
}

std::optional<ArtistBackgroundImage> load_artist_background_image(
    std::string_view artist_mbid, const ArtistBackgroundCandidate& candidate,
    const std::filesystem::path& cache_directory,
    const std::function<bool()>& cancelled, const ArtistImageHttpGet& get) {
    if (!allowed_candidate(artist_mbid, candidate) || (cancelled && cancelled())) return std::nullopt;
    const auto path = cache_directory.empty() ? std::filesystem::path{} :
        cache_directory / "artist-background" /
        (std::string(artist_mbid) + "-" + candidate.id + "-" + url_hash(candidate.url) + ".image");
    std::optional<MetadataCacheEntry> cached;
    if (!cache_directory.empty()) {
        cached = read_metadata_cache(path, image_limit, image_policy);
        if (cached) {
            const auto mime = image_mime(cached->body);
            if (!mime) {
                invalidate_metadata_cache(path);
                cached.reset();
            } else if (cached->fresh) return ArtistBackgroundImage{*mime, std::move(cached->body)};
        }
    }
    try {
        auto response = get ? get(candidate.url, image_limit, cancelled) :
            HttpClient{}.get(candidate.url, image_limit, cancelled, RedirectPolicy::fanart_asset);
        if (cancelled && cancelled()) return std::nullopt;
        if (response.status == 403 || response.status == 404 || response.status == 410) {
            if (!cache_directory.empty()) invalidate_metadata_cache(path);
            return std::nullopt;
        }
        if (response.status != 200 || response.body.empty() || response.body.size() > image_limit)
            throw std::runtime_error("artist background image is unavailable");
        const auto mime = image_mime(response.body);
        if (!mime || !matching_content_type(response.content_type, *mime)) {
            if (!cache_directory.empty()) invalidate_metadata_cache(path);
            return std::nullopt;
        }
        if (!cache_directory.empty())
            write_metadata_cache(cache_directory, path, response.body, image_policy);
        return ArtistBackgroundImage{*mime, std::move(response.body)};
    } catch (const std::exception&) {
        if (cached && !(cancelled && cancelled())) {
            if (const auto mime = image_mime(cached->body))
                return ArtistBackgroundImage{*mime, std::move(cached->body)};
        }
        return std::nullopt;
    }
}
