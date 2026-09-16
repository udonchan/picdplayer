#include "metadata_session.hpp"

namespace {
bool same_toc(const DiscToc& left, const DiscToc& right) {
    if (left.leadout_lba != right.leadout_lba || left.tracks.size() != right.tracks.size()) return false;
    for (std::size_t i = 0; i < left.tracks.size(); ++i) {
        const auto& a = left.tracks[i]; const auto& b = right.tracks[i];
        if (a.number != b.number || a.start_lba != b.start_lba || a.length_frames != b.length_frames) return false;
    }
    return true;
}
}
MetadataRequest MetadataSession::begin(const DiscToc& toc) {
    ++generation_; toc_ = toc; snapshot_ = {}; snapshot_.status = MetadataStatus::loading;
    return {generation_, toc};
}
void MetadataSession::invalidate() { ++generation_; toc_.reset(); snapshot_ = {}; }
bool MetadataSession::apply(MetadataWorkerResult result) {
    if (result.generation != generation_ || !toc_ || !same_toc(result.toc, *toc_)) return false;
    snapshot_ = std::move(result.metadata); return true;
}
