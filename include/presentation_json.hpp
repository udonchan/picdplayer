#pragma once
#include "presentation_model.hpp"
#include <string>
std::string serialize_presentation_model(const PresentationModel& model);
bool presentation_json_equal_ignoring_revision(std::string_view left, std::string_view right);
