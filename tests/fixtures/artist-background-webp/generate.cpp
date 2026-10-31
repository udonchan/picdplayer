#include <webp/encode.h>
#include <webp/mux.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

Bytes encode(int width, int height, bool lossless, bool alpha = false) {
    const int channels = alpha ? 4 : 3;
    Bytes pixels(static_cast<std::size_t>(width) * height * channels, 0);
    if (alpha)
        for (std::size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = (i / 4) % 2 ? 255 : 128;
    std::uint8_t* data = nullptr;
    const auto size = alpha ?
        (lossless ? WebPEncodeLosslessRGBA(pixels.data(), width, height, width * 4, &data) :
                    WebPEncodeRGBA(pixels.data(), width, height, width * 4, 75, &data)) :
        (lossless ? WebPEncodeLosslessRGB(pixels.data(), width, height, width * 3, &data) :
                    WebPEncodeRGB(pixels.data(), width, height, width * 3, 75, &data));
    if (size == 0 || data == nullptr) throw std::runtime_error("WebP encode failed");
    Bytes result(data, data + size);
    WebPFree(data);
    return result;
}

Bytes extended(const Bytes& bytes) {
    const WebPData input{bytes.data(), bytes.size()};
    std::unique_ptr<WebPMux, decltype(&WebPMuxDelete)> mux(WebPMuxCreate(&input, 1), WebPMuxDelete);
    const std::string xmp = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
        "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"/></x:xmpmeta>";
    const WebPData metadata{reinterpret_cast<const std::uint8_t*>(xmp.data()), xmp.size()};
    if (!mux || WebPMuxSetChunk(mux.get(), "XMP ", &metadata, 1) != WEBP_MUX_OK)
        throw std::runtime_error("WebP metadata failed");
    WebPData output;
    WebPDataInit(&output);
    if (WebPMuxAssemble(mux.get(), &output) != WEBP_MUX_OK)
        throw std::runtime_error("WebP mux failed");
    Bytes result(output.bytes, output.bytes + output.size);
    WebPDataClear(&output);
    return result;
}

int main() {
    const auto write = [](const std::string& name, const Bytes& bytes) {
        std::ofstream out(name + ".webp", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("WebP fixture write failed");
        std::cout << name << ": " << bytes.size() << " bytes\n";
    };
    write("lossy-2x2", encode(2, 2, false));
    write("lossless-2x2", encode(2, 2, true));
    write("lossy-alpha-2x2", encode(2, 2, false, true));
    write("lossless-alpha-2x2", encode(2, 2, true, true));
    write("extended-lossy-2x2", extended(encode(2, 2, false)));
    write("extended-lossless-2x2", extended(encode(2, 2, true)));
    for (const auto [width, height] : {std::pair{4096, 1}, {1, 4096}, {4096, 2048},
             {2048, 4096}, {3840, 2160}, {4097, 1}, {1, 4097}, {4096, 2049}})
        write(std::to_string(width) + "x" + std::to_string(height), encode(width, height, true));
}
