#include <windows.h>  // <-- This must be the first Windows header
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <memory>

#include "ffi_api.h"
#include "plugin.hpp"
#include "utils.hpp"

using winrt::Windows::Data::Xml::Dom::XmlDocument;

bool hasPackageIdentity() {
  if (!IsWindows8OrGreater()) return false;
  uint32_t length = 0;
  int error = GetCurrentPackageFullName(&length, nullptr);
  return error != APPMODEL_ERROR_NO_PACKAGE;
}

static ToastNotifier createToastNotifier(NativePlugin* plugin) {
  return plugin->hasIdentity ? ToastNotificationManager::CreateToastNotifier()
                             : ToastNotificationManager::CreateToastNotifier(plugin->aumid);
}

NativePlugin* createPlugin() { return new NativePlugin(); }

void disposePlugin(NativePlugin* plugin) { delete plugin; }

bool init(
  NativePlugin* plugin, char* appName, char* aumId, char* guid, char* iconPath, char* groupName,
  NativeNotificationCallback callback
) {
  try {
    string icon;
    if (iconPath != nullptr) icon = string(iconPath);
    const auto didRegister = plugin->registerApp(aumId, appName, guid, icon, callback);
    if (!didRegister) return false;
    plugin->hasIdentity = hasPackageIdentity();
    plugin->aumid = winrt::to_hstring(aumId);
    plugin->group = winrt::to_hstring(groupName);
    plugin->isReady = true;
    return true;
  } catch (...) {
    plugin->isReady = false;
    return false;
  }
}

bool isValidXml(char* xml) {
  XmlDocument doc = XmlDocument();
  try {
    doc.LoadXml(winrt::to_hstring(xml));
    return true;
  } catch (const winrt::hresult_error&) {
    return false;
  }
}

bool showNotification(
  NativePlugin* plugin, int id, char* xml, NativeStringMap bindings, bool suppressPopup
) {
  if (!plugin->isReady) return false;
  try {
    const XmlDocument doc;
    doc.LoadXml(winrt::to_hstring(xml));
    const ToastNotification notification(doc);
    const auto data = dataFromMap(bindings);
    notification.Tag(winrt::to_hstring(id));
    if (!plugin->hasIdentity) notification.Group(plugin->group);
    notification.Data(data);
    if (suppressPopup) notification.SuppressPopup(true);
    createToastNotifier(plugin).Show(notification);
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
    if (!plugin->hasIdentity) notification.Group(plugin->group);
    createToastNotifier(plugin).AddToSchedule(notification);
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
    const auto notifier = createToastNotifier(plugin);
    const auto result =
      plugin->hasIdentity ? notifier.Update(data, tag) : notifier.Update(data, tag, plugin->group);
    return static_cast<NativeUpdateResult>(result);
  } catch (const winrt::hresult_error&) {
    return failed;
  }
}

void cancelAll(NativePlugin* plugin) {
  if (!plugin->isReady) return;
  try {
    if (plugin->hasIdentity) {
      ToastNotificationManager::History().Clear();
    } else {
      ToastNotificationManager::History().Clear(plugin->aumid);
    }
  } catch (const winrt::hresult_error&) {
    // Never let a WinRT failure cross the FFI boundary
  }
  try {
    const auto notifier = createToastNotifier(plugin);
    for (const auto notification : notifier.GetScheduledToastNotifications()) {
      notifier.RemoveFromSchedule(notification);
    }
  } catch (const winrt::hresult_error&) {
    // Never let a WinRT failure cross the FFI boundary
  }
}

void cancelNotification(NativePlugin* plugin, int id) {
  if (!plugin->isReady) return;
  const auto tag = winrt::to_hstring(id);
  try {
    const auto history = ToastNotificationManager::History();
    if (plugin->hasIdentity)
      history.Remove(tag);
    else
      history.Remove(tag, plugin->group, plugin->aumid);
  } catch (winrt::hresult_error&) {
    // Toast not found
  }
  try {
    const auto notifier = createToastNotifier(plugin);
    for (const auto notification : notifier.GetScheduledToastNotifications()) {
      if (notification.Tag() == tag) {
        notifier.RemoveFromSchedule(notification);
        return;
      }
    }
  } catch (const winrt::hresult_error&) {
    // Never let a WinRT failure cross the FFI boundary
  }
}

/// Fills [details] from a toast's XML: the launch argument and the first two text elements.
/// Fields that can't be read stay null.
static void readToastContent(
  NativeNotificationDetails& details, const winrt::Windows::Data::Xml::Dom::XmlDocument& content
) {
  try {
    const auto launch = content.DocumentElement().GetAttribute(L"launch");
    if (!launch.empty()) details.payload = toNativeString(winrt::to_string(launch));
    const auto texts = content.GetElementsByTagName(L"text");
    if (texts.Size() > 0)
      details.title = toNativeString(winrt::to_string(texts.Item(0).InnerText()));
    if (texts.Size() > 1)
      details.body = toNativeString(winrt::to_string(texts.Item(1).InnerText()));
  } catch (const winrt::hresult_error&) {
    // Never let a WinRT failure cross the FFI boundary
  }
}

NativeNotificationDetails* getActiveNotifications(NativePlugin* plugin, int* size) {
  *size = 0;
  if (!plugin->isReady) return nullptr;
  try {
    const auto history = ToastNotificationManager::History();
    const auto active =
      plugin->hasIdentity ? history.GetHistory() : history.GetHistory(plugin->aumid);
    auto result = std::make_unique<NativeNotificationDetails[]>(active.Size());
    for (const auto notification : active) {
      try {
        result[*size].id = std::stoi(winrt::to_string(notification.Tag()));
        readToastContent(result[*size], notification.Content());
        (*size)++;
      } catch (...) {
        // A different plugin wrote this notification, or it can't be read; ignore it
      }
    }
    return result.release();
  } catch (...) {
    *size = 0;
    return nullptr;
  }
}

NativeNotificationDetails* getPendingNotifications(NativePlugin* plugin, int* size) {
  *size = 0;
  if (!plugin->isReady) return nullptr;
  try {
    const auto pending = createToastNotifier(plugin).GetScheduledToastNotifications();
    auto result = std::make_unique<NativeNotificationDetails[]>(pending.Size());
    for (const auto notification : pending) {
      try {
        result[*size].id = std::stoi(winrt::to_string(notification.Tag()));
        result[*size].scheduledTime = winrt::clock::to_time_t(notification.DeliveryTime());
        // Last: it allocates, and nothing after it can throw.
        readToastContent(result[*size], notification.Content());
        (*size)++;
      } catch (...) {
        // A different plugin wrote this notification, or it can't be read; ignore it
      }
    }
    return result.release();
  } catch (...) {
    *size = 0;
    return nullptr;
  }
}

void freeDetailsArray(NativeNotificationDetails* ptr, int size) {
  if (ptr == nullptr) return;
  for (int index = 0; index < size; index++) {
    delete[] ptr[index].title;
    delete[] ptr[index].body;
    delete[] ptr[index].payload;
  }
  delete[] ptr;
}

void freeLaunchDetails(NativeLaunchDetails details) {
  if (details.payload != nullptr) delete[] details.payload;
  for (int index = 0; index < details.data.size; index++) {
    const auto pair = details.data.entries[index];
    delete pair.key;
    delete pair.value;
  }
  if (details.data.entries != nullptr) delete[] details.data.entries;
}
