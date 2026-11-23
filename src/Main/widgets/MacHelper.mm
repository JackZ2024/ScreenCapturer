#include "MacHelper.h"
#import <Cocoa/Cocoa.h>
#include <ServiceManagement/ServiceManagement.h>
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <QDebug>


void setDockIconVisible(bool visible)
{
    if (visible) {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    } else {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    }
}

void activateApp()
{
    [NSApp activateIgnoringOtherApps:YES];
}

bool isDarkMode()
{
    NSAppearance* appearance = [NSApp effectiveAppearance];
    return [[appearance name] containsString:@"Dark"];
}

bool setAutoStart(bool enable)
{
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            NSString *bundleId = [[NSBundle mainBundle] bundleIdentifier];

            if (!bundleId || [bundleId length] == 0) {
                qWarning() << "无法获取 Bundle Identifier";
                return false;
            }

            qDebug() << "Bundle Identifier:" << QString::fromNSString(bundleId);

            SMAppService *service = [SMAppService mainAppService];

            if (!service) {
                qWarning() << "无法创建 SMAppService";
                return false;
            }

            // 先检查当前状态
            SMAppServiceStatus currentStatus = [service status];
            qDebug() << "当前状态:" << currentStatus;

            NSError *error = nil;
            BOOL success = NO;

            if (enable) {
                // 如果已经启用，直接返回成功
                if (currentStatus == SMAppServiceStatusEnabled) {
                    qDebug() << "登录项已经启用";
                    return true;
                }

                // 如果需要批准，提示用户
                if (currentStatus == SMAppServiceStatusRequiresApproval) {
                    qWarning() << "需要用户在系统设置中批准";
                    // 尝试注册，这会提示用户去系统设置批准
                    success = [service registerAndReturnError:&error];
                    if (!success && error) {
                        qWarning() << "注册失败:"
                                  << QString::fromNSString([error localizedDescription]);
                    }
                    return false; // 返回 false，让界面提示用户去批准
                }

                // 注册登录项
                success = [service registerAndReturnError:&error];

                if (success) {
                    qDebug() << "成功注册登录项";
                    return true;
                } else {
                    if (error) {
                        NSInteger errorCode = [error code];
                        qWarning() << "注册登录项失败:"
                                  << QString::fromNSString([error localizedDescription])
                                  << "错误码:" << errorCode;

                        // 处理各种错误情况
                        switch (errorCode) {
                            case 1: // SMAppServiceErrorAlreadyRegistered
                                qDebug() << "登录项已经注册，尝试检查状态";
                                return (currentStatus == SMAppServiceStatusEnabled);
                            case 2: // SMAppServiceErrorRequiresApproval
                                qWarning() << "需要用户批准";
                                return false;
                            default:
                                break;
                        }
                    }
                    return false;
                }
            } else {
                // 禁用登录项

                // 如果已经未注册，直接返回成功
                if (currentStatus == SMAppServiceStatusNotRegistered ||
                    currentStatus == SMAppServiceStatusNotFound) {
                    qDebug() << "登录项已经未注册";
                    return true;
                }

                // 如果状态是需要批准，说明实际上没有生效，也算成功
                if (currentStatus == SMAppServiceStatusRequiresApproval) {
                    qDebug() << "登录项处于待批准状态，视为未启用";
                    // 尝试取消注册
                    [service unregisterAndReturnError:nil];
                    return true;
                }

                // 取消注册
                success = [service unregisterAndReturnError:&error];

                if (success) {
                    qDebug() << "成功取消注册登录项";
                    return true;
                } else {
                    if (error) {
                        NSInteger errorCode = [error code];
                        qWarning() << "取消注册登录项失败:"
                                  << QString::fromNSString([error localizedDescription])
                                  << "错误码:" << errorCode;

                        // 错误码 1 = Operation not permitted
                        // 这通常意味着需要用户在系统设置中手动操作
                        if (errorCode == 1) {
                            qWarning() << "需要用户在系统设置中手动禁用";
                            return false;
                        }
                    }
                    return false;
                }
            }
        } else {
            qWarning() << "需要 macOS 13.0 或更高版本";
            return false;
        }
    }

    // 这个方法能加上，但是添加到后台启动项目里，不是添加到登录启动项里
    // if (@available(macOS 13.0, *)) {
    //     NSError *error = nil;
    //     SMAppService *service = [SMAppService mainAppService];

    //     if (enable) {
    //         return [service registerAndReturnError:&error];
    //     } else {
    //         return [service unregisterAndReturnError:&error];
    //     }
    // }
    // return false;
}

bool isAutoStartEnabled()
{
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            NSString *bundleId = [[NSBundle mainBundle] bundleIdentifier];

            if (!bundleId || [bundleId length] == 0) {
                qWarning() << "无法获取 Bundle Identifier";
                return false;
            }

            SMAppService *service = [SMAppService mainAppService];

            if (!service) {
                qWarning() << "无法创建 SMAppService";
                return false;
            }

            SMAppServiceStatus status = [service status];

            qDebug() << "登录项状态码:" << status;

            // 只有真正启用才返回 true
            bool enabled = (status == SMAppServiceStatusEnabled);

            return enabled;
        } else {
            qWarning() << "需要 macOS 13.0 或更高版本";
            return false;
        }
    }

    // if (@available(macOS 13.0, *)) {
    //     SMAppService *service = [SMAppService mainAppService];
    //     return service.status == SMAppServiceStatusEnabled;
    // }
    // return false;
}

QString getStatusDescription()
{
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            SMAppService *service = [SMAppService mainAppService];

            if (!service) {
                return "无法创建服务";
            }

            SMAppServiceStatus status = [service status];

            switch (status) {
                case SMAppServiceStatusNotRegistered:
                    return "未注册";
                case SMAppServiceStatusEnabled:
                    return "已启用";
                case SMAppServiceStatusRequiresApproval:
                    return "需要用户批准（请在系统设置中允许）";
                case SMAppServiceStatusNotFound:
                    return "未找到";
                default:
                    return QString("未知状态(%1)").arg((int)status);
            }
        } else {
            return "系统版本过低";
        }
    }
}

// 添加一个新方法：检查是否需要用户批准
bool requiresApproval()
{
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            SMAppService *service = [SMAppService mainAppService];
            if (!service) return false;

            SMAppServiceStatus status = [service status];
            return (status == SMAppServiceStatusRequiresApproval);
        }
        return false;
    }
}

// 打开系统设置到登录项页面
void openSystemSettings()
{
    @autoreleasepool {
        // 打开系统设置的登录项页面
        NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.LoginItems-Settings.extension"];
        [[NSWorkspace sharedWorkspace] openURL:url];
    }
}

void onTopWindow(WId winId)
{
    NSView *view = (NSView *)winId;
    NSWindow *window = [view window];

    if (window) {
        // 关键修改：使用 NSStatusWindowLevel
        // Level = 25。
        // 它高于 Dock (20) 和 菜单栏背景 (24)，
        // 但低于 右键菜单和顶部下拉菜单 (101)。
        [window setLevel:NSStatusWindowLevel];

        // 依然建议开启这个行为，保证在全屏应用或者多桌面切换时窗口依然可见
        [window setCollectionBehavior:
            (NSWindowCollectionBehaviorCanJoinAllSpaces |
             NSWindowCollectionBehaviorStationary |
             NSWindowCollectionBehaviorIgnoresCycle)];
    }
}
