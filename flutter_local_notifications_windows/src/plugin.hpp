#pragma once

#include <string>
#include <optional>

#include <windows.h>  // <-- This must be the first Windows header
#include <winrt/Windows.UI.Notifications.h>

#include "ffi_api.h"

using std::optional;
using std::string;
using namespace winrt::Windows::UI::Notifications;

/// The C++ container object for WinRT handles.
///
/// Note that this must be a struct as it was forward-declared as a struct in
/// `ffi_api.h`, which cannot use classes as it must be C-compatible.
struct NativePlugin {
  /// Whether the plugin has been properly initialized.
  bool isReady = false;

  /// Whether the current application has package identity (ie, was packaged with an MSIX).
  ///
  /// See [hasPackageIdentity].
  bool hasIdentity = false;

  /// The app user model ID. Used instead of package identity when [hasIdentity] is false.
  ///
  /// For more details, see https://learn.microsoft.com/en-us/windows/win32/shell/appids
  winrt::hstring aumid;

  /// The API responsible for querying shown notifications. Null if [isReady] is false.
  optional<ToastNotificationHistory> history;

  /// A callback to run when a notification is pressed, when the app is or is not running.
  NativeNotificationCallback callback;

  /// The activator CLSID, retained so the class object can be re-registered.
  string activatorGuid;

  /// Cookie from CoRegisterClassObject, needed to revoke before re-registering.
  DWORD classRegistration = 0;

  NativePlugin() {}
  ~NativePlugin() {}

  /// Registers the given [callback] to run when a notification is pressed.
  bool registerApp(
    const string& aumid, const string& appName, const string& guid,
    const optional<string>& iconPath, NativeNotificationCallback callback
  );

  /// Revokes and re-registers the activator class object.
  ///
  /// Called before every Show for the same reason makeNotifier mints a fresh ToastNotifier:
  /// a long-lived COM registration in this process stops servicing calls after a while. The
  /// shell still finds the class registered — so it never falls back to LocalServer32 — but
  /// the Activate call never arrives, and the click is dropped with the app merely
  /// foregrounded.
  bool registerActivator();
};
