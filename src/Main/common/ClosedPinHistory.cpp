#include "ClosedPinHistory.h"
#include <algorithm>
#include <utility>

void ClosedPinHistory::setLimit(int limit)
{
    limit_ = std::clamp(limit, 0, 50);
    while (entries_.size() > limit_) entries_.removeFirst();
}

bool ClosedPinHistory::remember(PinnedImageState state)
{
    if (limit_ == 0 || state.image.isNull() || !state.geometry.isValid()) return false;
    entries_.push_back({nextId_++, std::move(state), QDateTime::currentDateTime()});
    while (entries_.size() > limit_) entries_.removeFirst();
    return true;
}

std::optional<ClosedPin> ClosedPinHistory::entry(quint64 id) const
{
    for (const auto &closed : entries_) if (closed.id == id) return closed;
    return std::nullopt;
}

std::optional<ClosedPin> ClosedPinHistory::take(quint64 id)
{
    for (qsizetype i = 0; i < entries_.size(); ++i) {
        if (entries_[i].id == id) return entries_.takeAt(i);
    }
    return std::nullopt;
}
