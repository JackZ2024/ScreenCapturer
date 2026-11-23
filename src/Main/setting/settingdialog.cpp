#include "settingdialog.h"

#include "Capturer.h"
#include "colorpanel.h"
#include "combobox.h"
#include "GlobalSettings.h"
#include "UpdateChecker.h"
#include "scrollwidget.h"
#include "shortcutinput.h"
#include "version.h"

#include <QCheckBox>
#include <QDir>
#include <QFormLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

static const std::vector<std::pair<std::underlying_type_t<Qt::PenStyle>, QString>> PENSTYLES = {
    { Qt::SolidLine, "SolidLine" },
    { Qt::DashLine, "DashLine" },
    { Qt::DotLine, "DotLine" },
    { Qt::DashDotLine, "DashDotLine" },
    { Qt::DashDotDotLine, "DashDotDotLine" },
};

SettingWindow::SettingWindow(QWidget *parent)
    : FramelessWindow(parent, Qt::WindowMinMaxButtonsHint | Qt::WindowStaysOnTopHint)
{
    setMinimumSize(700, 400);
    setContentsMargins({});

    setAttribute(Qt::WA_DeleteOnClose);

    setWindowTitle(tr("Settings"));
    setWindowIcon(QIcon(":/icons/logo"));

    //
    const auto layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins({});
    setLayout(layout);

    {
        const auto menu = new QListWidget();
        menu->setFocusPolicy(Qt::NoFocus);
        menu->addItem(tr("General"));
        menu->addItem(tr("Hotkeys"));
        menu->addItem(tr("Screenshot"));
        menu->addItem(tr("About"));
        menu->setMinimumWidth(200);
        menu->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
        layout->addWidget(menu);

        const auto vlayout = new QVBoxLayout();
        vlayout->setSpacing(0);
        vlayout->setContentsMargins({});
        {
            const auto close_btn = new QCheckBox();
            close_btn->setObjectName("close-btn");
            close_btn->setCheckable(false);
            connect(close_btn, &QCheckBox::clicked, this, &QWidget::close);

            vlayout->addWidget(close_btn, 0, Qt::AlignRight);

            const auto pages = new QStackedWidget();

            pages->addWidget(setupGeneralWidget());
            pages->addWidget(setupHotkeyWidget());
            pages->addWidget(setupSnipWidget());
            pages->addWidget(setupAboutWidget());

            vlayout->addWidget(pages);

            connect(menu, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
        }
        layout->addLayout(vlayout);

        menu->setCurrentRow(0);
    }

    connect(this, &SettingWindow::closed, [] { g_Settings->save(); });
}

static QLabel *LABEL(const QString& text, int width)
{
    const auto label = new QLabel(text);
    label->setMinimumWidth(width);
    return label;
}

QWidget *SettingWindow::setupGeneralWidget()
{
    const auto page = new ScrollWidget();
    {
        const auto form = page->addForm(tr("General"));

        const auto autorun = new QCheckBox();
        autorun->setChecked(g_Settings->autorun);
        connect(autorun, &QCheckBox::toggled, [](bool checked) { g_Settings->setAutorun(checked); });
        form->addRow(LABEL(tr("Run on Startup"), 175), autorun);

        //
        const auto language = new ComboBox({
            { "en_US", "English" },
            { "zh_CN", "简体中文" },
        });
        language->select(g_Settings->language);
        language->onselected([](auto value) { g_Settings->language = value.toString(); });
        form->addRow(tr("Language"), language);

        //
        const auto file = new QLineEdit(g_Settings->filePath());
        file->setContextMenuPolicy(Qt::NoContextMenu);
        file->setReadOnly(true);
        form->addRow(tr("Settings File"), file);

        //
        const auto theme = new ComboBox({
            { "auto", tr("Auto") },
            { "dark", tr("Dark") },
            { "light", tr("Light") },
        });
        theme->onselected([](auto value) { g_Settings->setTheme(value.toString()); });
        theme->select(g_Settings->theme);
        form->addRow(tr("Theme"), theme);
    }

    {
        const auto form = page->addForm(tr("Pinned Images"));
        const auto addMouseAction = [form](const QString &label, QString *value) {
            auto *combo = new ComboBox({
                {"thumbnail", tr("Toggle Thumbnail")}, {"reset", tr("Reset View")},
                {"fit", tr("Fit to Screen")}, {"close", tr("Close")}, {"none", tr("Do Nothing")}
            });
            combo->select(*value);
            combo->onselected([value](auto selected) { *value = selected.toString(); g_Settings->save(); });
            form->addRow(label, combo);
        };
        addMouseAction(tr("Double-click"), &g_Settings->pinDoubleClick);
        addMouseAction(tr("Shift + Double-click"), &g_Settings->pinShiftDoubleClick);
        addMouseAction(tr("Middle-click"), &g_Settings->pinMiddleClick);
        auto *size = new QSpinBox();
        size->setRange(32, 240);
        size->setSuffix(tr(" px"));
        size->setValue(g_Settings->pinThumbnailSize);
        connect(size, &QSpinBox::valueChanged, [](int value) { g_Settings->pinThumbnailSize = value; g_Settings->save(); });
        form->addRow(tr("Thumbnail Size"), size);
        auto *historyLimit = new QSpinBox();
        historyLimit->setRange(0, 50);
        historyLimit->setSpecialValueText(tr("Disabled"));
        historyLimit->setValue(g_Settings->closedPinsLimit);
        historyLimit->setToolTip(tr("Recently closed pins are kept only until the application exits."));
        connect(historyLimit, &QSpinBox::valueChanged, [](int value) { g_Settings->setClosedPinsLimit(value); });
        form->addRow(tr("Recently Closed Pins Limit"), historyLimit);
    }
    page->addSpacer();
    return page;
}

QWidget *SettingWindow::setupHotkeyWidget()
{
    const auto page = new ScrollWidget();
    {
        const auto form = page->addForm(tr("Hotkeys"));

        const auto snip = new ShortcutInput(g_Settings->screenshot);
        connect(snip, &ShortcutInput::changed, [](auto ks) { g_Settings->screenshot = ks; });
        connect(snip, &ShortcutInput::changed, [] { App()->UpdateHotkeys(); });
        form->addRow(LABEL(tr("Screenshot"), 175), snip);

        const auto preview = new ShortcutInput(g_Settings->preview);
        connect(preview, &ShortcutInput::changed, [](auto&& ks) { g_Settings->preview = ks; });
        connect(preview, &ShortcutInput::changed, [] { App()->UpdateHotkeys(); });
        form->addRow(tr("Preview Clipboard"), preview);
        const auto restore = new ShortcutInput(g_Settings->restore_closed_pin);
        connect(restore, &ShortcutInput::changed, [](auto ks) { g_Settings->restore_closed_pin = ks; });
        connect(restore, &ShortcutInput::changed, [] { App()->UpdateHotkeys(); });
        form->addRow(tr("Restore Last Closed Pin"), restore);

        const auto tp = new ShortcutInput(g_Settings->toggle_previews);
        connect(tp, &ShortcutInput::changed, [](auto ks) { g_Settings->toggle_previews = ks; });
        connect(tp, &ShortcutInput::changed, [] { App()->UpdateHotkeys(); });
        form->addRow(tr("Toggle Previews"), tp);

#if _WIN32
        const auto qlook = new ShortcutInput(g_Settings->quick_look);
        connect(qlook, &ShortcutInput::changed, [](auto&& ks) { g_Settings->quick_look = ks; });
        connect(qlook, &ShortcutInput::changed, [] { App()->UpdateHotkeys(); });
        form->addRow(tr("Quick Look"), qlook);
#endif

        const auto trans = new ShortcutInput(g_Settings->transparent_input);
        connect(trans, &ShortcutInput::changed, [](auto ks) { g_Settings->transparent_input = ks; });
        connect(trans, &ShortcutInput::changed, [] { App()->UpdateHotkeys(); });
        form->addRow(tr("Transparent Input"), trans);
        const auto disable = new ShortcutInput(g_Settings->disable_transparent_input);
        connect(disable, &ShortcutInput::changed, [](auto ks) { g_Settings->disable_transparent_input = ks; });
        connect(disable, &ShortcutInput::changed, [] { App()->UpdateHotkeys(); });
        form->addRow(tr("Disable All Click-through"), disable);
    }

    page->addSpacer();
    return page;
}

QWidget *SettingWindow::setupSnipWidget()
{
    const auto page = new ScrollWidget();
    {
        const auto form = page->addForm(tr("Appearance"));

        const auto bwidth = new QSpinBox();
        bwidth->setRange(1, 6);
        bwidth->setContextMenuPolicy(Qt::NoContextMenu);
        bwidth->setValue(g_Settings->style.border_width);
        connect(bwidth, QOverload<int>::of(&QSpinBox::valueChanged),
                [](int w) { g_Settings->style.border_width = w; });
        connect(bwidth, QOverload<int>::of(&QSpinBox::valueChanged),
                [] { App()->UpdateScreenshotStyle(); });
        form->addRow(LABEL(tr("Border Width"), 175), bwidth);

        const auto bcolor = new ColorDialogButton(g_Settings->style.border_color);
        connect(bcolor, &ColorDialogButton::changed,
                [](auto&& c) { g_Settings->style.border_color = c; });
        connect(bcolor, &ColorDialogButton::changed, [] { App()->UpdateScreenshotStyle(); });
        form->addRow(tr("Border Color"), bcolor);

        const auto bstyle = new ComboBox();
        bstyle->add(PENSTYLES);
        bstyle->onselected([&](auto value) {
            g_Settings->style.border_style = static_cast<Qt::PenStyle>(value.toInt());
        });
        connect(bstyle, &ComboBox::selected, [] { App()->UpdateScreenshotStyle(); });
        bstyle->select(static_cast<int>(g_Settings->style.border_style));
        form->addRow(tr("Border Style"), bstyle);

        const auto mcolor = new ColorDialogButton(g_Settings->style.mask_color);
        connect(mcolor, &ColorDialogButton::changed, [&](auto&& c) { g_Settings->style.mask_color = c; });
        connect(mcolor, &ColorDialogButton::changed, [] { App()->UpdateScreenshotStyle(); });
        form->addRow(tr("Mask Color"), mcolor);

        const auto moveOpaque = new QDoubleSpinBox();
        moveOpaque->setRange(0.01f, 1);
        moveOpaque->setSingleStep(0.1);
        moveOpaque->setContextMenuPolicy(Qt::NoContextMenu);
        moveOpaque->setValue(g_Settings->moveopaque);
        connect(moveOpaque, &::QDoubleSpinBox::valueChanged, [](float w) { g_Settings->moveopaque = w; });
        form->addRow(LABEL(tr("Moving Opaque"), 175), moveOpaque);
    }

    const auto form = page->addForm(tr("File"));
    {
        const auto path = new QLineEdit(g_Settings->path);
        path->setContextMenuPolicy(Qt::NoContextMenu);
        path->setReadOnly(true);
        form->addRow(LABEL(tr("Save Path"), 175), path);
    }

    page->addSpacer();
    return page;
}

QWidget *SettingWindow::setupAboutWidget()
{
    const auto page    = new QWidget();
    const auto vlayout = new QVBoxLayout();
    vlayout->setContentsMargins(35, 0, 35, 15);

    {
        vlayout->addStretch(1);

        const auto icon = new QLabel();
        QPixmap pix = QPixmap(":/icons/logo");
        pix = pix.scaled(256, 256);
        icon->setPixmap(pix);
        icon->setAlignment(Qt::AlignCenter);
        vlayout->addWidget(icon);

        const auto name = new QLabel("ScreenCapturer");
        name->setAlignment(Qt::AlignCenter);
        name->setObjectName("about-name");
        vlayout->addWidget(name);

        const auto version = new QLabel("Version : " + QString(CAPTURER_VERSION));
        version->setAlignment(Qt::AlignCenter);
        version->setObjectName("about-version");
        vlayout->addWidget(version);

        const auto checkUpdates = new QPushButton(tr("Check for Updates"), page);
        connect(checkUpdates, &QPushButton::clicked, this, [this] {
            update_checker::checkManually(this);
        });
        vlayout->addWidget(checkUpdates, 0, Qt::AlignHCenter);

        vlayout->addStretch(2);

        const auto copyright = new QLabel("Copyright © 2026 Jack");
        copyright->setAlignment(Qt::AlignCenter);
        copyright->setObjectName("about-copyright");
        vlayout->addWidget(copyright);
    }
    page->setLayout(vlayout);

    return page;
}
