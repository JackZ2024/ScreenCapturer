#ifdef SCREENCAPTURER_WITH_SPARKLE
#include "UpdateChecker.h"
#include "UpdatePrompt.h"

#include <QDialog>
#include <QDebug>
#include <QMessageBox>
#include <QPointer>

#import <AppKit/AppKit.h>
#import <Sparkle/Sparkle.h>

static void activateApp()
{
    if (@available(macOS 14.0, *)) [NSApp activate];
    else [NSApp activateIgnoringOtherApps:YES];
}

@interface ScreenCapturerUpdateDriver : SPUStandardUserDriver
@property(nonatomic) BOOL manualCheck;
@end

@implementation ScreenCapturerUpdateDriver {
    QPointer<QDialog> _prompt;
    BOOL _approved;
}

- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancellation
{
    if (self.manualCheck) [super showUserInitiatedUpdateCheckWithCancellation:cancellation];
}

- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem *)item state:(SPUUserUpdateState *)state
                                reply:(void (^)(SPUUserUpdateChoice))reply
{
    Q_UNUSED(state);
    if (self.manualCheck) [super dismissUpdateInstallation];
    const auto versionKey = QString::fromNSString(item.versionString);
    if (!self.manualCheck && UpdatePrompt::wasSkipped(versionKey)) {
        reply(SPUUserUpdateChoiceDismiss);
        return;
    }
    if (item.informationOnlyUpdate) {
        if (self.manualCheck) {
            QMessageBox::information(nullptr, UpdatePrompt::tr("Software Update"),
                UpdatePrompt::tr("Version %1 has no verifiable installer for this computer.")
                    .arg(QString::fromNSString(item.displayVersionString)));
        }
        reply(SPUUserUpdateChoiceDismiss);
        return;
    }

    auto *dialog = UpdatePrompt::create(QString::fromNSString(item.displayVersionString));
    _prompt = dialog;
    __weak ScreenCapturerUpdateDriver *weakSelf = self;
    QObject::connect(dialog, &QDialog::finished, dialog, [weakSelf, versionKey, reply](int result) {
        ScreenCapturerUpdateDriver *driver = weakSelf;
        if (!driver) return;
        driver->_prompt = nullptr;
        driver->_approved = result == QDialog::Accepted;
        if (!driver->_approved) UpdatePrompt::rememberSkipped(versionKey);
        reply(driver->_approved ? SPUUserUpdateChoiceInstall : SPUUserUpdateChoiceDismiss);
    });
    activateApp();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

- (void)showUpdateNotFoundWithError:(NSError *)error acknowledgement:(void (^)(void))acknowledgement
{
    if (self.manualCheck) [super showUpdateNotFoundWithError:error acknowledgement:acknowledgement];
    else acknowledgement();
}

- (void)showUpdaterError:(NSError *)error acknowledgement:(void (^)(void))acknowledgement
{
    if (self.manualCheck || _approved) [super showUpdaterError:error acknowledgement:acknowledgement];
    else acknowledgement();
}

- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply
{
    reply(SPUUserUpdateChoiceInstall);
}

- (void)showUpdateInFocus
{
    if (_prompt) {
        activateApp();
        _prompt->raise();
        _prompt->activateWindow();
    } else {
        [super showUpdateInFocus];
    }
}

- (void)dismissUpdateInstallation
{
    if (auto *dialog = _prompt.data()) {
        _prompt = nullptr;
        QObject::disconnect(dialog, nullptr, dialog, nullptr);
        dialog->reject();
    }
    _approved = NO;
    [super dismissUpdateInstallation];
}
@end

namespace {
ScreenCapturerUpdateDriver *driver = nil;
SPUUpdater *updater = nil;

void check(bool manual, QWidget *parent)
{
    if (!updater) {
        NSBundle *bundle = NSBundle.mainBundle;
        driver = [[ScreenCapturerUpdateDriver alloc] initWithHostBundle:bundle delegate:nil];
        SPUUpdater *candidate = [[SPUUpdater alloc] initWithHostBundle:bundle applicationBundle:bundle
                                                           userDriver:driver delegate:nil];
        NSError *error = nil;
        if (![candidate startUpdater:&error]) {
            qWarning() << QString::fromNSString(error.localizedDescription);
            if (manual) {
                QMessageBox::information(parent, UpdatePrompt::tr("Software Update"),
                                         UpdatePrompt::tr("Could not check for updates. Please try again."));
            }
            return;
        }
        updater = candidate;
    }

    // Use Sparkle's foreground check for selection so the exact skipped versions are
    // controlled by the dialog. The driver keeps the startup check silent until an offer.
    driver.manualCheck = manual || (updater.sessionInProgress && driver.manualCheck);
    [updater checkForUpdates];
}
}

void update_checker::checkOnStartup()
{
    check(false, nullptr);
}

void update_checker::checkManually(QWidget *parent)
{
    check(true, parent);
}
#endif
