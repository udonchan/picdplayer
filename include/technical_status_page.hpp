#pragma once

#include <string_view>

// Embedded, read-only diagnostic UI. Keeping these assets in the daemon makes
// the API build self-contained on Raspberry Pi OS and future Buildroot images.
std::string_view technical_status_html();
std::string_view technical_status_css();
std::string_view technical_status_javascript();
