#include <windows.h>  // <-- This must be the first Windows header
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Xml.Dom.h>

#include "ffi_api.h"
#include "plugin.hpp"
#include "utils.hpp"

using winrt::Windows::Data::Xml::Dom::XmlDocument;

static const winrt::hstring kDefaultGroup = L"flnpGroupNull";

// A long-lived cached ToastNotifier can have its COM proxy disconnected (Show then throws
// CO_E_OBJNOTCONNECTED, 0x800401FD). CreateToastNotifier is cheap, so mint a fresh one per Show.
static ToastNotifier makeNotifier(NativePlugin* plugin) {
  return plugin->hasIdentity
    ? ToastNotificationManager::CreateToastNotifier()
    : ToastNotificationManager::CreateToastNotifier(plugin->aumid);
}

bool hasPackageIdentity() {
  if (!IsWindows8OrGreater()) return false;
  uint32_t length = 0;
  int error = GetCurrentPackageFullName(&length, nullptr);
  return error != APPMODEL_ERROR_NO_PACKAGE;
}

NativePlugin* createPlugin() { return new NativePlugin(); }

void disposePlugin(NativePlugin* plugin) { delete plugin; }

bool init(
  NativePlugin* plugin, char* appName, char* aumId, char* guid, char* iconPath,
  NativeNotificationCallback callback
) {
  string icon;
  if (iconPath != nullptr) icon = string(iconPath);
  const auto didRegister = plugin->registerApp(aumId, appName, guid, icon, callback);
  if (!didRegister) return false;
  plugin->hasIdentity = hasPackageIdentity();
  plugin->aumid = winrt::to_hstring(aumId);
  plugin->history = ToastNotificationManager::History();
  plugin->isReady = true;
  return true;
}

bool isValidXml(char* xml) {
  XmlDocument doc = XmlDocument();
  try {
    doc.LoadXml(winrt::to_hstring(xml));
    return true;
  } catch (winrt::hresult_error error) {
    return false;
  }
}

bool showNotification(NativePlugin* plugin, int id, char* xml, NativeStringMap bindings, bool suppressPopup) {
  if (!plugin->isReady) return false;
  try {
    const XmlDocument doc;
    doc.LoadXml(winrt::to_hstring(xml));
    const ToastNotification notification(doc);
    const auto data = dataFromMap(bindings);
    notification.Tag(winrt::to_hstring(id));
    if (!plugin->hasIdentity) notification.Group(kDefaultGroup);
    notification.Data(data);
    if (suppressPopup) notification.SuppressPopup(true);
    makeNotifier(plugin).Show(notification);
    return true;
  } catch (const winrt::hresult_error&) {
    return false;
  }
}

bool scheduleNotification(NativePlugin* plugin, int id, char* xml, int time) {
  if (!plugin->isReady) return false;
  try {
    const XmlDocument doc;
    doc.LoadXml(winrt::to_hstring(xml));
    const ScheduledToastNotification notification(doc, winrt::clock::from_time_t(time));
    notification.Tag(winrt::to_hstring(id));
    if (!plugin->hasIdentity) notification.Group(kDefaultGroup);
    makeNotifier(plugin).AddToSchedule(notification);
    return true;
  } catch (const winrt::hresult_error&) {
    return false;
  }
}

NativeUpdateResult updateNotification(NativePlugin* plugin, int id, NativeStringMap bindings) {
  if (!plugin->isReady) return failed;
  try {
    const auto tag = winrt::to_hstring(id);
    const auto data = dataFromMap(bindings);
    const auto notifier = makeNotifier(plugin);
    const auto result = plugin->hasIdentity
      ? notifier.Update(data, tag)
      : notifier.Update(data, tag, kDefaultGroup);
    return static_cast<NativeUpdateResult>(result);
  } catch (const winrt::hresult_error&) {
    return failed;
  }
}

void cancelAll(NativePlugin* plugin) {
  if (!plugin->isReady) return;
  try {
    const auto history = ToastNotificationManager::History();
    if (plugin->hasIdentity) {
      history.Clear();
    } else {
      history.Clear(plugin->aumid);
    }
  } catch (const winrt::hresult_error&) {
    // Never let a WinRT failure cross the FFI boundary.
  }
  try {
    const auto notifier = makeNotifier(plugin);
    for (const auto notification : notifier.GetScheduledToastNotifications()) {
      notifier.RemoveFromSchedule(notification);
    }
  } catch (const winrt::hresult_error&) {
    // Never let a WinRT failure cross the FFI boundary.
  }
}

void cancelNotification(NativePlugin* plugin, int id) {
  if (!plugin->isReady) return;
  const auto tag = winrt::to_hstring(id);
  try {
    if (plugin->hasIdentity) plugin->history.value().Remove(tag);
    else plugin->history.value().Remove(tag, kDefaultGroup, plugin->aumid);
  } catch (winrt::hresult_error&) {
    // Toast not found
  }
  try {
    const auto notifier = makeNotifier(plugin);
    for (const auto notification : notifier.GetScheduledToastNotifications()) {
      if (notification.Tag() == tag) {
        notifier.RemoveFromSchedule(notification);
        return;
      }
    }
  } catch (const winrt::hresult_error&) {
    // Never let a WinRT failure cross the FFI boundary.
  }
}

NativeNotificationDetails* getActiveNotifications(NativePlugin* plugin, int* size) {
  // TODO: Get more details here
  *size = 0;
  if (!plugin->isReady) return nullptr;
  try {
    const auto history = ToastNotificationManager::History();
    const auto active = plugin->hasIdentity
      ? history.GetHistory()
      : history.GetHistory(plugin->aumid);
    const auto result = new NativeNotificationDetails[active.Size()];
    for (const auto notification : active) {
      result[(*size)++].id = std::stoi(winrt::to_string(notification.Tag()));
    }
    return result;
  } catch (const winrt::hresult_error&) {
    *size = 0;
    return nullptr;
  }
}

NativeNotificationDetails* getPendingNotifications(NativePlugin* plugin, int* size) {
  // TODO: Get more details here
  *size = 0;
  if (!plugin->isReady) return nullptr;
  try {
    const auto pending = makeNotifier(plugin).GetScheduledToastNotifications();
    const auto result = new NativeNotificationDetails[pending.Size()];
    for (const auto notification : pending) {
      result[(*size)++].id = std::stoi(winrt::to_string(notification.Tag()));
    }
    return result;
  } catch (const winrt::hresult_error&) {
    *size = 0;
    return nullptr;
  }
}

void freeDetailsArray(NativeNotificationDetails* ptr) { delete[] ptr; }

void freeLaunchDetails(NativeLaunchDetails details) {
  if (details.payload != nullptr) delete[] details.payload;
  for (int index = 0; index < details.data.size; index++) {
    const auto pair = details.data.entries[index];
    delete pair.key;
    delete pair.value;
  }
  if (details.data.entries != nullptr) delete[] details.data.entries;
}
