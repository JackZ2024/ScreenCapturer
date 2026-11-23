#ifndef CAPTURER_CLOSED_PIN_HISTORY_H
#define CAPTURER_CLOSED_PIN_HISTORY_H

#include "PinSession.h"
#include <QDateTime>
#include <optional>

struct ClosedPin
{
    quint64 id;
    PinnedImageState state;
    QDateTime closedAt;
};

// This history is memory-only. It must never be included in the startup session
// or registered with the clipboard history.
class ClosedPinHistory
{
public:
    void setLimit(int limit);
    bool remember(PinnedImageState state);
    std::optional<ClosedPin> entry(quint64 id) const;
    std::optional<ClosedPin> take(quint64 id);
    const QList<ClosedPin> &entries() const { return entries_; }
    bool empty() const { return entries_.isEmpty(); }
    quint64 lastId() const { return empty() ? 0 : entries_.back().id; }
    void clear() { entries_.clear(); }

private:
    int limit_{10};
    quint64 nextId_{1};
    QList<ClosedPin> entries_; // Oldest first; restore the newest by default.
};
#endif
