#ifndef MACHELPER_H
#define MACHELPER_H

#include <QString>

#ifdef __APPLE__

void setDockIconVisible(bool visible);
void activateApp();
bool isDarkMode();
bool setAutoStart(bool enable);
bool isAutoStartEnabled();
QString getStatusDescription();
bool requiresApproval();
void openSystemSettings();

void onTopWindow(WId winId);

#endif


#endif
