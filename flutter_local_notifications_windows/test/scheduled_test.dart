import 'package:flutter_local_notifications_windows/flutter_local_notifications_windows.dart';
import 'package:flutter_local_notifications_platform_interface/flutter_local_notifications_platform_interface.dart';
import 'package:test/test.dart';
import 'package:timezone/data/latest_all.dart';
import 'package:timezone/standalone.dart';

const WindowsInitializationSettings settings = WindowsInitializationSettings(
  appName: 'Test app',
  appUserModelId: 'com.test.test',
  guid: 'a8c22b55-049e-422f-b30f-863694de08c8',
);

void main() => group('Schedules', () {
  final FlutterLocalNotificationsWindows plugin =
      FlutterLocalNotificationsWindows();
  setUpAll(initializeTimeZones);
  setUpAll(() => plugin.initialize(settings: settings));
  tearDownAll(() async {
    await plugin.cancelAll();
    plugin.dispose();
  });

  Future<int> countPending() async =>
      (await plugin.pendingNotificationRequests()).length;
  late final Location location = getLocation('US/Eastern');

  test('do not work with earlier time', () async {
    final TZDateTime now = TZDateTime.now(location);
    final TZDateTime earlier = now.subtract(const Duration(days: 1));
    await plugin.cancelAll();
    expect(await countPending(), 0);
    expect(
      plugin.zonedSchedule(
        id: 302,
        title: null,
        body: null,
        scheduledDate: now,
        payload: null,
      ),
      throwsArgumentError,
    );
    expect(
      plugin.zonedSchedule(
        id: 302,
        title: null,
        body: null,
        scheduledDate: earlier,
        payload: null,
      ),
      throwsArgumentError,
    );
  });

  test('report their contents and time', () async {
    final TZDateTime later = TZDateTime.now(
      location,
    ).add(const Duration(hours: 1));
    await plugin.cancelAll();
    await plugin.zonedSchedule(
      id: 303,
      title: 'Title',
      body: 'Body',
      scheduledDate: later,
      payload: 'payload',
    );
    final List<PendingNotificationRequest> pending = await plugin
        .pendingNotificationRequests();
    expect(pending, hasLength(1));
    final WindowsPendingNotificationRequest request =
        pending.single as WindowsPendingNotificationRequest;
    expect(request.id, 303);
    expect(request.title, 'Title');
    expect(request.body, 'Body');
    expect(request.payload, 'payload');
    // Scheduling has second resolution.
    expect(
      request.scheduledDate.difference(later).inSeconds.abs(),
      lessThanOrEqualTo(1),
    );
    await plugin.cancel(id: 303);
    expect(await countPending(), 0);
  });
});
