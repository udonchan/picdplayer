#include <cstddef>
#include <cstdio>
#include <jpeglib.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

void encode(const std::string& name, int width, int height, int mode = 0, int channels = 3,
            bool restart = false, bool noisy = false) {
    jpeg_compress_struct encoder{};
    jpeg_error_mgr errors{};
    encoder.err = jpeg_std_error(&errors);
    jpeg_create_compress(&encoder);
    unsigned char* encoded = nullptr;
    unsigned long size = 0;
    jpeg_mem_dest(&encoder, &encoded, &size);
    encoder.image_width = width;
    encoder.image_height = height;
    encoder.input_components = channels;
    encoder.in_color_space = channels == 1 ? JCS_GRAYSCALE : channels == 4 ? JCS_CMYK : JCS_RGB;
    jpeg_set_defaults(&encoder);
    jpeg_set_quality(&encoder, mode == 3 ? 1 : noisy ? 95 : 75, mode == 3 ? FALSE : TRUE);
    if (mode == 1) {
        // Huffman destination 2 requires SOF1 without wide quantization tables.
        encoder.dc_huff_tbl_ptrs[2] = jpeg_alloc_huff_table(reinterpret_cast<j_common_ptr>(&encoder));
        encoder.ac_huff_tbl_ptrs[2] = jpeg_alloc_huff_table(reinterpret_cast<j_common_ptr>(&encoder));
        *encoder.dc_huff_tbl_ptrs[2] = *encoder.dc_huff_tbl_ptrs[0];
        *encoder.ac_huff_tbl_ptrs[2] = *encoder.ac_huff_tbl_ptrs[0];
        encoder.comp_info[0].dc_tbl_no = encoder.comp_info[0].ac_tbl_no = 2;
    }
    if (mode == 2) jpeg_simple_progression(&encoder);
    if (restart) encoder.restart_interval = 1;
    jpeg_start_compress(&encoder, TRUE);
    std::vector<unsigned char> row(static_cast<std::size_t>(width) * channels, 0);
    while (encoder.next_scanline < encoder.image_height) {
        if (noisy)
            for (int x = 0; x < width; ++x)
                for (int c = 0; c < channels; ++c)
                    row[x * channels + c] = static_cast<unsigned char>(x * 37 + encoder.next_scanline * 17 + c * 83);
        auto* line = row.data();
        jpeg_write_scanlines(&encoder, &line, 1);
    }
    jpeg_finish_compress(&encoder);
    std::ofstream out(name + ".jpg", std::ios::binary);
    out.write(reinterpret_cast<const char*>(encoded), static_cast<std::streamsize>(size));
    if (!out) throw std::runtime_error("JPEG fixture write failed");
    unsigned sof = 0;
    for (std::size_t offset = 2; offset + 4 <= size;) {
        if (encoded[offset] != 255) throw std::runtime_error("unexpected JPEG header");
        const auto marker = encoded[offset + 1];
        if (marker == 0xda) break;
        if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2) sof = marker;
        const auto length = static_cast<std::size_t>((encoded[offset + 2] << 8) | encoded[offset + 3]);
        if (length < 2 || length > size - offset - 2) throw std::runtime_error("JPEG segment bounds");
        offset += 2 + length;
    }
    const auto expected = mode == 1 || mode == 3 ? 0xc1U : mode == 2 ? 0xc2U : 0xc0U;
    if (sof != expected) throw std::runtime_error("unexpected JPEG coding process");
    std::cout << name << ": " << size << " bytes, SOF 0x" << std::hex << sof << std::dec << '\n';
    jpeg_destroy_compress(&encoder);
    std::free(encoded);
}

int main() {
    encode("baseline-2x2", 2, 2);
    encode("sequential-2x2", 2, 2, 1);
    encode("progressive-2x2", 2, 2, 2);
    encode("wide-quantization-2x2", 2, 2, 3);
    encode("grayscale-2x2", 2, 2, 0, 1);
    encode("cmyk-2x2", 2, 2, 0, 4);
    encode("restart-32x32", 32, 32, 0, 3, true);
    encode("stuffed-64x64", 64, 64, 0, 3, false, true);
    for (const auto [width, height] : {std::pair{4096, 1}, {1, 4096}, {4096, 2048},
             {2048, 4096}, {3840, 2160}, {4097, 1}, {1, 4097}, {4096, 2049}})
        encode(std::to_string(width) + "x" + std::to_string(height), width, height);
}
