import Foundation
import Observation
import OnePageKit

/// A converted book kept on the phone, ready to send or resend.
struct LibraryItem: Codable, Identifiable, Equatable {
    enum State: String, Codable { case waiting, sending, onDevice, failed }

    var bookID: UInt32
    var title: String
    var author: String
    var size: Int
    var state: State
    var sentBytes: Int
    var failure: String?

    var id: UInt32 { bookID }
}

/// One line in the Library: a book on the phone, on the device, or both.
struct LibraryRow: Identifiable {
    var item: LibraryItem?
    var device: DeviceBook?
    var id: UInt32 { item?.bookID ?? device?.bookID ?? 0 }
}

/// Imports, converts and sends books. The device keeps the reading
/// position; the app only shows it.
@Observable
final class BookLibrary {
    @ObservationIgnored private weak var device: DeviceManager?
    @ObservationIgnored private var activeTask: Task<Void, Never>?

    private(set) var items: [LibraryItem] = []
    private(set) var isImporting = false
    private(set) var activeBookID: UInt32?
    var lastError: UserFacingError?

    static let folder: URL = {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let dir = base.appendingPathComponent("Books", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }()

    private static var indexURL: URL { folder.appendingPathComponent("index.json") }

    init(device: DeviceManager) {
        self.device = device
        load()
    }

    private func load() {
        guard let data = try? Data(contentsOf: Self.indexURL),
              let list = try? JSONDecoder().decode([LibraryItem].self, from: data) else { return }
        // A send cut short by the app closing resumes from the device's offset.
        items = list.map { var i = $0; if i.state == .sending { i.state = .waiting }; return i }
    }

    private func save() {
        if let data = try? JSONEncoder().encode(items) {
            try? data.write(to: Self.indexURL, options: .atomic)
        }
    }

    private func fileURL(_ id: UInt32) -> URL {
        Self.folder.appendingPathComponent(String(format: "%08X.opb", id))
    }

    /// Merges the list of books from the device with the local list.
    func merged(with deviceBooks: [DeviceBook]) -> [LibraryRow] {
        var out: [LibraryRow] = []
        var seen = Set<UInt32>()
        for item in items {
            out.append(LibraryRow(item: item, device: deviceBooks.first { $0.bookID == item.bookID }))
            seen.insert(item.bookID)
        }
        for b in deviceBooks where !seen.contains(b.bookID) {
            out.append(LibraryRow(item: nil, device: b))
        }
        return out
    }

    // MARK: Import

    /// Imports a file from the Files picker or the share sheet.
    @MainActor
    func importFile(at url: URL) async {
        isImporting = true
        defer { isImporting = false }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        do {
            let data = try Data(contentsOf: url)
            let name = url.lastPathComponent
            let book = try await Task.detached(priority: .userInitiated) {
                try BookConverter.convert(fileName: name, bytes: [UInt8](data))
            }.value
            let file = book.encode()
            let id = CRC32.checksum(file)
            if items.contains(where: { $0.bookID == id }) {
                lastError = UserFacingError(title: "Already in the library",
                                            message: "\"\(book.title)\" is already in the library.")
                return
            }
            try Data(file).write(to: fileURL(id), options: .atomic)
            items.append(LibraryItem(bookID: id, title: book.title, author: book.author,
                                     size: file.count, state: .waiting, sentBytes: 0, failure: nil))
            save()
            sendPending()
        } catch {
            lastError = UserFacingError("The book was not added", error)
        }
    }

    // MARK: Delete

    @MainActor
    func delete(_ bookID: UInt32) async {
        if activeBookID == bookID {
            activeTask?.cancel()
            if let device, device.state.isConnected {
                try? await device.write(TransferCommand.cancel(bookID: bookID).encode(), to: DeviceManager.transferControlUUID)
            }
        }
        if let device, device.state.isConnected, device.deviceBooks.contains(where: { $0.bookID == bookID }) {
            do {
                device.transferReplies.clear()
                try await device.write(TransferCommand.delete(bookID: bookID).encode(), to: DeviceManager.transferControlUUID)
                if case let .result(_, status)? = try? await device.transferReplies.next(timeout: 5), status != .ok, status != .unknownBook {
                    throw LinkError.rejected(status.message)
                }
                try await device.requestBookList()
            } catch {
                lastError = UserFacingError("The book was not deleted from the device", error)
                return
            }
        }
        items.removeAll { $0.bookID == bookID }
        try? FileManager.default.removeItem(at: fileURL(bookID))
        save()
    }

    @MainActor
    func retry(_ bookID: UInt32) {
        update(bookID) { $0.state = .waiting; $0.failure = nil }
        sendPending()
    }

    // MARK: Send

    /// Sends waiting books one at a time. Called on import and on reconnection.
    func sendPending() {
        guard activeTask == nil, let device, device.state.isConnected,
              let next = items.first(where: { $0.state == .waiting }) else { return }
        activeTask = Task { @MainActor [weak self] in
            guard let self else { return }
            // Keep going for as long as iOS allows if the user leaves the app.
            let activity = BackgroundActivity(name: "OnePageBookTransfer")
            await self.send(next.bookID, device: device)
            activity.end()
            self.activeTask = nil
            self.activeBookID = nil
            self.sendPending()
        }
    }

    private func update(_ id: UInt32, _ change: (inout LibraryItem) -> Void) {
        guard let i = items.firstIndex(where: { $0.bookID == id }) else { return }
        change(&items[i])
    }

    @MainActor
    private func send(_ bookID: UInt32, device: DeviceManager) async {
        guard let item = items.first(where: { $0.bookID == bookID }),
              let data = try? Data(contentsOf: fileURL(bookID)) else {
            update(bookID) { $0.state = .failed; $0.failure = "The book file is missing on the phone. Delete it and add it again." }
            save()
            return
        }
        activeBookID = bookID
        update(bookID) { $0.state = .sending; $0.failure = nil }
        let replies = device.transferReplies
        replies.clear()
        await device.acquireFastLink()
        defer { Task { @MainActor in await device.releaseFastLink() } }

        var plan = TransferPlan(bookID: bookID, file: [UInt8](data), maxWriteLength: device.maxWriteLength)
        do {
            try await device.write(TransferCommand.start(bookID: bookID, totalLength: UInt32(plan.totalLength),
                                                         crc32: plan.crc32, title: item.title).encode(),
                                   to: DeviceManager.transferControlUUID)
            let first = try await nextProgress(replies, bookID: bookID)
            plan.apply(deviceOffset: first)

            var rounds = 0
            while true {
                try Task.checkCancellation()
                while let block = plan.nextBlock() {
                    try Task.checkCancellation()
                    try await device.send(block, to: DeviceManager.transferDataUUID)
                    for reply in replies.drain() {
                        try apply(reply, to: &plan, bookID: bookID)
                    }
                    update(bookID) { $0.sentBytes = plan.confirmedOffset }
                }
                // Every byte went out once. Ask where the device is.
                try await device.write(TransferCommand.query(bookID: bookID).encode(), to: DeviceManager.transferControlUUID)
                var offset = try await nextProgress(replies, bookID: bookID)
                // A PROGRESS sent for an earlier batch of 32 blocks can arrive
                // before the QUERY reply. Within one session the device's
                // offset only grows, so the largest one is the newest.
                for case let .progress(id, status, o) in replies.drain()
                    where id == bookID && (status == .ok || status == .badBlock) {
                    offset = max(offset, o)
                }
                plan.apply(deviceOffset: offset)
                update(bookID) { $0.sentBytes = plan.confirmedOffset }
                if plan.confirmedOffset >= plan.totalLength { break }
                rounds += 1
                if rounds > 20 { throw LinkError.rejected(TransferStatus.badBlock.message) }
            }

            try await device.write(TransferCommand.finish(bookID: bookID).encode(), to: DeviceManager.transferControlUUID)
            while true {
                let reply = try await replies.next(timeout: 30)
                if case let .result(id, status) = reply, id == bookID {
                    guard status == .ok else { throw LinkError.rejected(status.message) }
                    break
                }
            }
            update(bookID) { $0.state = .onDevice; $0.sentBytes = plan.totalLength }
            try? await device.requestBookList()
        } catch is CancellationError {
            update(bookID) { $0.state = .waiting }
        } catch LinkError.notConnected {
            // Resumes from the device's offset on reconnection.
            update(bookID) { $0.state = .waiting }
        } catch {
            let message = UserFacingError("The book was not sent", error).message
            update(bookID) { $0.state = .failed; $0.failure = message }
        }
        save()
    }

    /// Waits for a PROGRESS reply for this book and returns its offset.
    @MainActor
    private func nextProgress(_ replies: Mailbox<TransferReply>, bookID: UInt32) async throws -> UInt32 {
        while true {
            let reply = try await replies.next(timeout: 10)
            if case let .progress(id, status, offset) = reply, id == bookID {
                switch status {
                case .ok, .badBlock: return offset
                default: throw LinkError.rejected(status.message)
                }
            }
        }
    }

    private func apply(_ reply: TransferReply, to plan: inout TransferPlan, bookID: UInt32) throws {
        guard case let .progress(id, status, offset) = reply, id == bookID else { return }
        switch status {
        // A routine report lags behind the blocks in flight: confirm only.
        case .ok: plan.confirm(deviceOffset: offset)
        // The device dropped a block and every block after it: resend from its offset.
        case .badBlock: plan.apply(deviceOffset: offset)
        default: throw LinkError.rejected(status.message)
        }
    }
}
