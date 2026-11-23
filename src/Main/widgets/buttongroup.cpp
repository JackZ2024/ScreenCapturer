#include "buttongroup.h"

void ButtonGroup::addButton(QAbstractButton *button_ptr, int id)
{
    if (!button_ptr) return;

    if (id == -1) {
        auto iter = std::min_element(mapping_.cbegin(), mapping_.cend());
        if (iter == mapping_.cend()) {
            mapping_[button_ptr] = -2;
        }
        else {
            mapping_[button_ptr] = *iter - 1;
        }
    }
    else {
        mapping_[button_ptr] = id;
    }

    buttons_.push_back(button_ptr);

    connect(button_ptr, &QAbstractButton::clicked, this, [=, this]() {
        emit idClicked(mapping_[button_ptr]);
        emit buttonClicked(button_ptr);

        if (!button_ptr->isCheckable() && exclusive_ == exclusive_t::simiexclusive) {
            clear();
        }
    });

    connect(button_ptr, &QAbstractButton::toggled, this,
            [=, this](bool checked) { ontoggled(button_ptr, checked); });

    connect(button_ptr, &QObject::destroyed, this, [this, button_ptr] {
        buttons_.removeAll(button_ptr);
        mapping_.remove(button_ptr);
        if (checked_ == button_ptr) {
            checked_ = nullptr;
            emit emptied();
        }
    });

    if (button_ptr->isChecked()) ontoggled(button_ptr, true);
}

void ButtonGroup::ontoggled(QAbstractButton *button_ptr, bool checked)
{
    switch (exclusive_) {
    case exclusive_t::exclusive:
        if (!checked && checked_ == button_ptr) {
            button_ptr->setChecked(true);
        }
        else if (checked && checked_ != button_ptr) {
            const auto previous = checked_;
            checked_ = button_ptr;
            if (previous) previous->setChecked(false);
        }
        break;

    case exclusive_t::simiexclusive:
        if (!checked && checked_ == button_ptr) {
            checked_ = nullptr;
            emit emptied();
        }
        else if (checked && checked_ != button_ptr) {
            // Publish the new selection before unchecking the previous button.
            // Its synchronous toggled signal must not emit a transient emptied()
            // and reenter tool/scene updates halfway through this transition.
            const auto previous = checked_;
            checked_ = button_ptr;
            if (previous) previous->setChecked(false);
        }
        break;

    case exclusive_t::nonexclusive:
        if (!checked) {
            checked_ = nullptr;
            for (auto iter = mapping_.begin(); iter != mapping_.end(); ++iter) {
                if (iter.key()->isChecked()) {
                    checked_ = iter.key();
                    break;
                }
            }

            if (!checked_) emit emptied();
        }
        break;

    default: break;
    }

    emit idToggled(mapping_[button_ptr], checked);
    emit buttonToggled(button_ptr, checked);
}

void ButtonGroup::clear()
{
    if (exclusive_ == exclusive_t::exclusive) return;

    checked_ = nullptr;

    for (auto iter = mapping_.begin(); iter != mapping_.end(); ++iter) {
        iter.key()->setChecked(false);
    }

    emit emptied();
}
