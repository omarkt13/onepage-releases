import EventKit
import Foundation
import Observation
import OnePageKit

/// Reads the week ahead from EventKit and sends it to the device.
@Observable
final class CalendarService {
    @ObservationIgnored let store = EKEventStore()
    @ObservationIgnored private weak var device: DeviceManager?
    @ObservationIgnored private var observer: NSObjectProtocol?
    @ObservationIgnored private var syncTask: Task<Void, Never>?

    private(set) var authorization: EKAuthorizationStatus = EKEventStore.authorizationStatus(for: .event)
    private(set) var calendars: [EKCalendar] = []
    private(set) var isSyncing = false
    private(set) var lastSync: Date? = Prefs.lastCalendarSync
    private(set) var lastEventCount = 0
    var lastError: UserFacingError?

    var isOn: Bool = Prefs.calendarSyncOn {
        didSet { Prefs.calendarSyncOn = isOn }
    }

    var selectedIDs: Set<String> {
        get { Prefs.selectedCalendarIDs ?? Set(calendars.map(\.calendarIdentifier)) }
        set { Prefs.selectedCalendarIDs = newValue }
    }

    var hasFullAccess: Bool { authorization == .fullAccess }

    init(device: DeviceManager) {
        self.device = device
        observer = NotificationCenter.default.addObserver(forName: .EKEventStoreChanged, object: store, queue: .main) { [weak self] _ in
            guard let self, self.isOn, self.hasFullAccess else { return }
            self.reloadCalendars()
            self.scheduleSync()
        }
        if hasFullAccess { reloadCalendars() }
    }

    deinit {
        if let observer { NotificationCenter.default.removeObserver(observer) }
    }

    /// Asks for full calendar access. Called when the user turns the feature on.
    @MainActor
    func requestAccess() async -> Bool {
        do {
            let granted = try await store.requestFullAccessToEvents()
            authorization = EKEventStore.authorizationStatus(for: .event)
            if granted { reloadCalendars() }
            return granted
        } catch {
            authorization = EKEventStore.authorizationStatus(for: .event)
            lastError = UserFacingError(title: "Calendar access failed",
                                        message: "iOS did not give calendar access. Open Settings > Privacy & Security > Calendars and allow full access for OnePage.")
            return false
        }
    }

    func reloadCalendars() {
        calendars = store.calendars(for: .event).sorted { $0.title.localizedCompare($1.title) == .orderedAscending }
    }

    /// Collapses a burst of calendar changes into one sync.
    func scheduleSync() {
        syncTask?.cancel()
        syncTask = Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 2_000_000_000)
            guard !Task.isCancelled else { return }
            await self?.sync()
        }
    }

    /// Events from the next 7 days, plus events that started earlier and are
    /// still running, from the chosen calendars.
    func upcomingEvents(now: Date = Date()) -> [CalendarEvent] {
        let cal = Calendar.current
        let startOfToday = cal.startOfDay(for: now)
        guard let end = cal.date(byAdding: .day, value: 7, to: startOfToday),
              let lookBack = cal.date(byAdding: .day, value: -7, to: startOfToday) else { return [] }
        let chosen = calendars.filter { selectedIDs.contains($0.calendarIdentifier) }
        guard !chosen.isEmpty else { return [] }
        let predicate = store.predicateForEvents(withStart: lookBack, end: end, calendars: chosen)
        let nowLocal = LocalSeconds.from(now)
        let events = store.events(matching: predicate).compactMap { e -> CalendarEvent? in
            guard let s = e.startDate, let f = e.endDate else { return nil }
            var start = s, finish = f
            if e.isAllDay {
                // Midnight at the start of the first day, and midnight after the last day.
                start = cal.startOfDay(for: s)
                let lastDay = cal.startOfDay(for: f)
                finish = lastDay == f ? f : (cal.date(byAdding: .day, value: 1, to: lastDay) ?? f)
                if finish <= start { finish = cal.date(byAdding: .day, value: 1, to: start) ?? f }
            }
            return CalendarEvent(start: LocalSeconds.from(start), end: LocalSeconds.from(finish),
                                 allDay: e.isAllDay,
                                 title: e.title ?? "",
                                 place: (e.location ?? "").replacingOccurrences(of: "\n", with: ", "))
        }
        return CalendarBlob.prepare(events, now: nowLocal)
    }

    /// Sends the whole week. The device swaps in the new copy only after a
    /// complete, checked transfer.
    @MainActor
    func sync() async {
        guard isOn, hasFullAccess, let device, device.state.isConnected, !isSyncing else { return }
        isSyncing = true
        defer { isSyncing = false }
        do {
            let events = upcomingEvents()
            let now = Date()
            let blob = CalendarBlob.encode(generatedAt: LocalSeconds.from(now), events: events)
            guard blob.count <= CalendarBlob.maxBlobBytes else {
                throw LinkError.rejected("The week has too much text for the device. Pick fewer calendars.")
            }
            try await send(blob: blob, version: LocalSeconds.from(now), device: device)
            lastSync = now
            Prefs.lastCalendarSync = now
            lastEventCount = events.count
            lastError = nil
        } catch {
            lastError = UserFacingError("The calendar did not sync", error)
        }
    }

    /// Erases the calendar on the device, for when the user turns sync off.
    @MainActor
    func clearOnDevice() async {
        guard let device, device.state.isConnected else { return }
        device.calendarAcks.clear()
        do {
            try await device.write(CalendarCommand.clear.encode(), to: DeviceManager.calendarUUID)
            _ = try await device.calendarAcks.next(timeout: 5)
        } catch {
            lastError = UserFacingError("The device calendar was not erased", error)
        }
    }

    @MainActor
    private func send(blob: [UInt8], version: UInt32, device: DeviceManager) async throws {
        let mailbox = device.calendarAcks
        mailbox.clear()
        await device.setFastLink(true)
        defer { Task { @MainActor in await device.setFastLink(false) } }

        let begin = CalendarCommand.begin(version: version, totalLength: UInt32(blob.count), crc32: CRC32.checksum(blob))
        try await device.write(begin.encode(), to: DeviceManager.calendarUUID)
        var ack = try await mailbox.next(timeout: 5)
        guard ack.status == .ok else { throw LinkError.rejected(ack.status.message) }

        let chunk = max(1, device.maxWriteLength - CalendarCommand.dataOverhead)
        var offset = 0
        var restarts = 0
        while offset < blob.count {
            let end = min(blob.count, offset + chunk)
            try await device.write(CalendarCommand.data(offset: UInt32(offset), bytes: blob[offset..<end]).encode(),
                                   to: DeviceManager.calendarUUID)
            offset = end
            // The device replies to DATA only when something is wrong.
            for a in mailbox.drain() where a.status != .ok {
                guard a.status == .outOfOrder, restarts < 3 else { throw LinkError.rejected(a.status.message) }
                restarts += 1
                offset = Int(a.nextOffset)
            }
        }

        try await device.write(CalendarCommand.commit(version: version).encode(), to: DeviceManager.calendarUUID)
        ack = try await mailbox.next(timeout: 5)
        guard ack.status == .ok else { throw LinkError.rejected(ack.status.message) }
    }
}
