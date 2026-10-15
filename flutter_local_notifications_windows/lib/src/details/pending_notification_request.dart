import 'package:flutter_local_notifications_platform_interface/flutter_local_notifications_platform_interface.dart';

/// A [PendingNotificationRequest] that also knows when it will be shown.
///
/// Returned by [FlutterLocalNotificationsWindows.pendingNotificationRequests]. [title], [body]
/// and [payload] are read back from the scheduled toast's XML: the first two `text` elements
/// and the `launch` argument.
class WindowsPendingNotificationRequest extends PendingNotificationRequest {
  /// Constructs an instance of [WindowsPendingNotificationRequest].
  const WindowsPendingNotificationRequest(
    super.id,
    super.title,
    super.body,
    super.payload, {
    required this.scheduledDate,
  });

  /// When the notification will be shown.
  final DateTime scheduledDate;
}
