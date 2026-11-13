import OnePageKit
import SwiftUI
import UniformTypeIdentifiers

/// Books on the phone and on the device, with progress.
struct LibraryView: View {
    @Environment(AppModel.self) private var model
    @State private var importing = false

    var body: some View {
        @Bindable var library = model.library
        let device = model.device
        let rows = library.merged(with: device.deviceBooks)
        List {
            if !device.state.isConnected {
                Section { NotConnectedNote(state: device.state) }
            }
            if library.isImporting {
                Section { HStack { ProgressView(); Text("Converting the book…") } }
            }
            if rows.isEmpty {
                ContentUnavailableView("No books",
                                       systemImage: "books.vertical",
                                       description: Text("Add an EPUB or a text file. Books from Apple Books or Kindle have copy protection and cannot be sent."))
            }
            ForEach(rows) { row in
                BookRow(item: row.item, deviceBook: row.device, active: library.activeBookID == row.id)
                    .swipeActions {
                        Button("Delete", role: .destructive) {
                            Task { await library.delete(row.id) }
                        }
                    }
                    .contextMenu {
                        if let item = row.item, item.state == .failed {
                            Button("Try again") { library.retry(item.bookID) }
                        }
                    }
            }
            if let free = device.status.storageFreeKiB {
                Section {
                    LabeledContent("Free on device", value: DeviceView.mb(free))
                }
            }
        }
        .navigationTitle("Library")
        .toolbar {
            Button {
                importing = true
            } label: {
                Label("Add book", systemImage: "plus")
            }
        }
        .fileImporter(isPresented: $importing,
                      allowedContentTypes: [UTType.epub, UTType.plainText],
                      allowsMultipleSelection: true) { result in
            switch result {
            case let .success(urls):
                Task {
                    for url in urls { await library.importFile(at: url) }
                }
            case .failure:
                library.lastError = UserFacingError(title: "No file opened",
                                                    message: "The file could not be opened. Check that it is downloaded to the phone, then try again.")
            }
        }
        .refreshable { try? await device.requestBookList() }
        .errorAlert($library.lastError)
    }
}

struct BookRow: View {
    let item: LibraryItem?
    let deviceBook: DeviceBook?
    let active: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(item?.title ?? deviceBook?.title ?? "Untitled").font(.headline)
            if let author = item?.author, !author.isEmpty {
                Text(author).font(.subheadline).foregroundStyle(.secondary)
            }
            if let item, item.state == .sending || (active && item.state != .onDevice) {
                ProgressView(value: Double(item.sentBytes), total: Double(max(1, item.size))) {
                    Text("Sending")
                }
                .accessibilityValue("\(Int(Double(item.sentBytes) / Double(max(1, item.size)) * 100)) percent sent")
            } else if let item, item.state == .waiting {
                Text("Waiting for the device").font(.caption).foregroundStyle(.secondary)
            } else if let item, item.state == .failed {
                Text(item.failure ?? "Not sent.").font(.caption).foregroundStyle(.red)
                Text("Touch and hold to try again.").font(.caption2).foregroundStyle(.secondary)
            } else if let b = deviceBook {
                ProgressView(value: b.progress) {
                    Text("Read \(Int(b.progress * 100))%")
                }
                .accessibilityValue("\(Int(b.progress * 100)) percent read")
            }
        }
        .padding(.vertical, 2)
    }
}
