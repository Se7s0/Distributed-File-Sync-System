# Phase 4 Reference - Sync Engine & Control Plane

## Overview

**Phase:** 4 – Sync Engine & Control Plane  
**Duration:** 5-7 days  
**Difficulty:** Hard  
**Prerequisites:** Phase 1 (HTTP Server) ✅ | Phase 2 (Metadata & DDL) ✅ | Phase 3 (Event System) ✅  
**Status:** 📝 Planning

Phase 4 turns the metadata-driven, event-based server into a real file synchronisation platform. We will design the full sync pipeline (change detection → diffing → transfer → metadata update) for clients and the server, expose it over HTTP, and make use of the Phase 3 EventBus for observability, progress tracking, and orchestration.

---

## Table of Contents

1. [Where We Stand After Phase 3](#where-we-stand-after-phase-3)
2. [Why We Need a Sync Engine](#why-we-need-a-sync-engine)
3. [Phase 4 Goals at a Glance](#phase-4-goals-at-a-glance)
4. [Architecture Overview](#architecture-overview)
5. [Task Breakdown](#task-breakdown)
6. [Task 1: Sync Domain Model](#task-1-sync-domain-model)
7. [Task 2: Change Detection](#task-2-change-detection)
8. [Task 3: Metadata Diff (Merkle Trees)](#task-3-metadata-diff-merkle-trees)
9. [Task 4: Sync Session State Machine](#task-4-sync-session-state-machine)
10. [Task 5: Chunk-Based File Transfer](#task-5-chunk-based-file-transfer)
11. [Task 6: HTTP Control Plane](#task-6-http-control-plane)
12. [Task 7: Client Sync Workflow](#task-7-client-sync-workflow)
13. [Task 8: Conflict Detection & Resolution](#task-8-conflict-detection--resolution)
14. [Task 9: Events, Observability & Metrics](#task-9-events-observability--metrics)
15. [Task 10: Testing Strategy](#task-10-testing-strategy)
16. [Task 11: Performance & Resilience](#task-11-performance--resilience)
17. [Files to Create / Update](#files-to-create--update)
18. [Learning Objectives](#learning-objectives)
19. [Risks & Mitigations](#risks--mitigations)
20. [Next Steps (Phase 5 Preview)](#next-steps-phase-5-preview)

---

## Where We Stand After Phase 3

### Capabilities in Place
- **HTTP server & router** from Phase 1 handle REST-style endpoints. (`examples/metadata_server_events_example.cpp`)
- **Metadata stack** from Phase 2 parses DDL, stores `FileMetadata`, and exposes metadata APIs. (`include/dfs/metadata/*`)
- **EventBus & components** from Phase 3 decouple logic: `LoggerComponent`, `MetricsComponent`, and `SyncComponent` subscribe to file events (`include/dfs/events/components.hpp`).
- **SyncComponent queue** already captures paths that need syncing; Phase 4 will drain this queue to trigger actual file transfers (`docs/phase_3_code_reference.md:546`).

### Limitations
- Metadata changes do not move bytes yet.
- No mechanism to compare client/server state beyond raw metadata snapshots.
- Conflicts are only theoretical; no runtime detection or resolution.
- Observability is limited to metadata changes, not transfer progress.

---

## Why We Need a Sync Engine

Without a sync engine, clients would have to re-upload or download entire directory trees on every change. Phase 4 delivers:
- **Smart change detection** to avoid unnecessary transfers.
- **Efficient diffing** (Merkle tree) so large trees sync quickly.
- **Reliable transfer pipeline** with chunking, resume support, and retries.
- **Conflict handling** so concurrent edits do not silently overwrite each other.
- **Control plane APIs** enabling clients to negotiate sync sessions over HTTP.

This is the phase where the system begins behaving like Dropbox/rsync: change events trigger real replication across machines via the server hub (`DEVELOPMENT_GUIDE.md:441`).

---

## Phase 4 Goals at a Glance

- Detect filesystem changes on the client side and classify them (added/modified/deleted).
- Build Merkle trees for local vs remote metadata, requesting only the files that differ.
- Implement a sync session state machine coordinating diff, transfer, and commit.
- Transfer files in resumable chunks over HTTP endpoints (`/api/file/upload`, `/api/file/download`).
- Update server metadata, emit events for progress and completion, and queue downstream syncs.
- Detect conflicting updates and invoke a `ConflictResolver` strategy (`DEVELOPMENT_GUIDE.md:535`).
- Provide integration tests proving one client’s edits replicate to another via the server.

---

## Architecture Overview

```
Client (Phase 4)
┌─────────────────────────────────────────────────────────────┐
│ ChangeDetector → MerkleDiff → SyncSession → FileTransfer     │
│        │               │                │                   │
│        │               │                │                   │
│        ▼               │                ▼                   │
│   Local Metadata   HTTP Control Plane   Chunk Streams        │
└─────────────────────────────────────────────────────────────┘
                 ▲                         │
                 │                         ▼
Server (Phase 4)
┌─────────────────────────────────────────────────────────────┐
│ MetadataStore ⇄ SyncSessionManager ⇄ Storage Backend         │
│        │                    │                               │
│        ▼                    ▼                               │
│   EventBus emit ───▶ Components (Logger, Metrics, Sync, …)   │
└─────────────────────────────────────────────────────────────┘
```

Key control-flow:
1. Client scans for changes, builds diff vs server metadata.
2. Client starts sync session via `/api/sync/start` → receives token/session id.
3. Client uploads diffs and files chunk-by-chunk; server streams chunks to disk and updates metadata.
4. Server emits events (`FileUploadStartedEvent`, `FileChunkReceivedEvent`, `FileUploadCompletedEvent`) to existing components and any new Phase 4 observers.
5. Once all files are applied, server notifies completion; other subscribers (e.g., SyncComponent) enqueue downstream sync work for other replicas.

---

## Task Breakdown

| # | Task | Summary |
|---|------|---------|
| 1 | Sync Domain Model | Define shared structs/enums for sync sessions, messages, and transfer metadata. |
| 2 | Change Detection | Scan directories, produce `FileChange` records, and feed SyncComponent queue. |
| 3 | Metadata Diff | Construct Merkle trees (or hash maps) to identify changed paths efficiently. |
| 4 | Sync Session State Machine | Manage end-to-end sync workflow with explicit states and transitions. |
| 5 | Chunk-Based File Transfer | Read/write files in fixed-size chunks with resume & integrity checks. |
| 6 | HTTP Control Plane | Implement REST endpoints for registration, diff exchange, uploads/downloads, status. |
| 7 | Client Sync Workflow | Build client orchestration layer that drives Tasks 2-6. |
| 8 | Conflict Detection & Resolution | Compare versions/hashes, invoke resolver strategies, emit conflict events. |
| 9 | Events, Observability & Metrics | Extend EventBus usage for progress, bytes transferred, error reporting. |
|10 | Testing Strategy | Unit, integration, and performance/stress tests for all new components. |
|11 | Performance & Resilience | Retry logic, resume support, throttling hooks, resource limits. |

---

## Task 1: Sync Domain Model

### Objectives
- Provide shared C++ types representing sync sessions, file changes, transfer commands, and status responses.
- Ensure both client and server speak the same “protocol” in terms of request/response payloads.

### Key Types (suggested)
```cpp
struct SyncSessionInfo {
    std::string session_id;
    std::string client_id;
    std::chrono::system_clock::time_point started_at;
    SyncState state;                // enum from DEVELOPMENT_GUIDE.md:499
    size_t files_pending;
    size_t bytes_pending;
};

struct DiffRequest {
    std::string session_id;
    std::vector<FileMetadata> local_snapshot;
};

struct DiffResponse {
    std::vector<std::string> files_to_upload;
    std::vector<std::string> files_to_download;
    std::vector<std::string> files_to_delete_remote;
};

struct ChunkEnvelope {
    std::string session_id;
    std::string file_path;
    uint32_t chunk_index;
    uint32_t total_chunks;
    std::vector<uint8_t> data;
    std::string chunk_hash;    // optional integrity check
};
```

### Deliverables
- Header under `include/dfs/sync/types.hpp` (new directory).
- Serialization helpers (JSON/CBOR) for HTTP payloads.
- Documentation comments describing who produces/consumes each struct.

---

## Task 2: Change Detection

### Objectives
- Track local filesystem changes since last sync.
- Produce structured `FileChange` events consumed by the Sync engine.

### Steps
1. Implement `ChangeDetector` (`DEVELOPMENT_GUIDE.md:466`). Keep a cache of last-known metadata (hash, size, mtimes).
2. Provide APIs: `scan_directory(path)` and `diff_since(last_snapshot)` returning `std::vector<FileChange>`.
3. Integrate with existing `SyncComponent` by pushing file paths that need upload into the queue when local changes are detected.
4. Emit `FileModifiedEvent` / `FileAddedEvent` for local changes so server-side metrics/loggers remain consistent (use EventBus on the client side too if appropriate).

### Considerations
- For Phase 4 we can allow manual scans (triggered by CLI). Phase 5 will replace this with OS watchers.
- Use hashes from Phase 2 to avoid false positives.
- Record the base version/hash for conflict detection (store alongside `FileChange`).

---

## Task 3: Metadata Diff (Merkle Trees)

### Objectives
- Avoid transferring entire metadata lists for large directories.
- Efficiently determine which subtrees differ between client and server.

### Approach
1. Build Merkle trees on both sides (`DEVELOPMENT_GUIDE.md:483`). Each node hash could be computed using child hashes (e.g., `hash = SHA256(child0 || child1 || …)`).
2. Diff algorithm: client sends its root hash. If server root matches, we are done. Otherwise, recursively request hashes for subdirectories until the differing files are identified.
3. Provide fallback to simple hash map diff for smaller directories (configurable).

### Deliverables
- `include/dfs/sync/merkle_tree.hpp` + `src/sync/merkle_tree.cpp` with build/diff APIs.
- Unit tests covering identical trees, single-file changes, subtree differences.
- Documentation on tree layout (breadth-first? sorted children?).

---

## Task 4: Sync Session State Machine

### Objectives
- Formalize the sync workflow into explicit states to handle retries and errors cleanly.

### States (from `DEVELOPMENT_GUIDE.md:499`)
1. `IDLE`
2. `COMPUTING_DIFF`
3. `REQUESTING_METADATA`
4. `TRANSFERRING_FILES`
5. `RESOLVING_CONFLICTS`
6. `APPLYING_CHANGES`
7. `COMPLETE`

### Implementation Notes
- Server-side `SyncSessionManager` tracks active sessions (map from `session_id` to `SyncSession` object).
- Each session stores participating client id, target paths, outstanding files, and last activity timestamp.
- Transition helper `transition_to(new_state)` emits `SyncStateChangedEvent` to the EventBus for observability.
- Handle timeouts: if no progress for configurable duration, abort session and emit failure event.

### Deliverables
- `include/dfs/sync/session.hpp` + `src/sync/session.cpp` implementing state machine.
- Unit tests verifying legal transitions and error handling.

---

## Task 5: Chunk-Based File Transfer

### Objectives
- Transfer files reliably using moderate chunk sizes (default 64 KiB as per `DEVELOPMENT_GUIDE.md:523`).
- Support resume after failures by re-requesting missing chunks.

### Key APIs
```cpp
class FileTransferService {
public:
    Result<void> send_file(const std::string& session_id,
                           const std::string& path,
                           HttpClient& client);

    Result<void> receive_chunk(const ChunkEnvelope& chunk,
                               std::ofstream& dest);

    Result<void> finalize_file(const std::string& session_id,
                               const std::string& path,
                               const std::string& expected_hash);
};
```

### Features
- Compute chunk hashes to detect corruption; re-upload missing/invalid chunks.
- Maintain upload manifest per session to resume.
- Emit progress events after each chunk (`FileChunkReceivedEvent`, `docs/phase_3_reference.md:1793`).

### Deliverables
- `include/dfs/sync/transfer.hpp` + `src/sync/transfer.cpp`.
- Integration with HTTP endpoints (Task 6).

---

## Task 6: HTTP Control Plane

### Objectives
- Expose REST endpoints so clients can negotiate sessions, convey diffs, and transfer chunks (`DEVELOPMENT_GUIDE.md:552`).

### Endpoints (initial version)
| Method | Path | Purpose |
|--------|------|---------|
| POST | `/api/register` | Register client node, receive `client_id` + auth token (Phase 4 can stub auth). |
| POST | `/api/sync/start` | Initiate session, return `session_id`, current server snapshot summary. |
| POST | `/api/sync/diff` | Submit client diff; server responds with required actions. |
| POST | `/api/file/upload` | Upload a chunk (body contains `ChunkEnvelope`). |
| GET | `/api/file/download` | Stream chunk(s) for requested file. |
| GET | `/api/sync/status` | Poll session progress (pending files, bytes, state). |
| POST | `/api/conflict` | Submit resolution choice / merged file metadata. |

### Implementation Notes
- Add router entries similar to metadata handlers (`examples/metadata_server_events_example.cpp`).
- Responses should be JSON for control plane, binary for chunk data.
- Ensure idempotency: `sync/start` can safely be retried (returns same session if active).

### Deliverables
- `src/server/sync_handlers.cpp` with new handlers.
- Update `HttpRouter` registration.
- Extend tests with HTTP client stubs.

---

## Task 7: Client Sync Workflow

### Objectives
- Build a CLI/daemon workflow orchestrating Tasks 2-6 from the client perspective.

### Steps
1. CLI command `dfs_client sync --server URL --dir PATH` (skeleton exists in guides).
2. Flow:
   - Register client (if needed) → store client id locally.
   - Scan directory → gather `FileChange`s + local metadata snapshot.
   - Start sync session and send diff.
   - Upload required files using `FileTransferService`.
   - Download files the server requests.
   - Apply deletions, finalize metadata.
3. Persist last-known hashes to disk to avoid full rescan on each run.
4. Report progress to user (percentage, ETA).

### Deliverables
- `src/client/sync_client.cpp` (new) or enhancements to existing example.
- Unit/integration tests using temp directories.

---

## Task 8: Conflict Detection & Resolution

### Objectives
- Detect when client & server both modified a file based on divergent base versions.
- Expose resolution options: `LAST_WRITE_WINS`, `MANUAL`, `MERGE` (`DEVELOPMENT_GUIDE.md:536`).

### Detection Flow
1. Client includes `base_version` / `base_hash` in diff upload.
2. Server compares with stored metadata:
   - If current server hash == base hash → safe to apply client update.
   - Otherwise, conflict: store both versions (`FileMetadata::has_conflict()`), emit `FileConflictDetectedEvent`.
3. Conflict resolver decides outcome:
   - `LAST_WRITE_WINS`: take newest `modified_time`.
   - `MANUAL`: queue for human intervention; mark file as `SyncState::CONFLICT`.
   - `MERGE`: attempt three-way merge (see user guidance).

### Deliverables
- `include/dfs/sync/conflict.hpp` + `src/sync/conflict.cpp`.
- Event type `FileConflictDetectedEvent` + `FileConflictResolvedEvent` (extend `include/dfs/events/events.hpp`).
- HTTP handler `/api/conflict` to accept resolution decisions.
- Unit tests simulating divergent edits.

---

## Task 9: Events, Observability & Metrics

### Objectives
- Extend EventBus usage to cover sync life-cycle events so existing components get richer data (`docs/phase_3_reference.md:1793`).

### New Events (extend `include/dfs/events/events.hpp`)
```cpp
struct FileUploadStartedEvent { std::string file_path; size_t total_bytes; };
struct FileChunkReceivedEvent { std::string file_path; size_t chunk_index; size_t bytes_received; size_t total_bytes; };
struct FileUploadCompletedEvent { std::string file_path; std::string hash; size_t total_bytes; std::chrono::milliseconds duration; };
struct FileDownloadCompletedEvent { std::string file_path; size_t total_bytes; };
struct FileConflictDetectedEvent { std::string file_path; metadata::FileMetadata local; metadata::FileMetadata remote; };
struct FileConflictResolvedEvent { std::string file_path; ConflictResolver::Strategy strategy; };
```

### Component Updates
- `LoggerComponent`: log transfer progress and conflicts.
- `MetricsComponent`: track bytes uploaded/downloaded, conflict counts, average transfer speed.
- New optional components: `ProgressTracker`, `HashVerifier` (see `docs/phase_3_code_reference.md:1215`).
- `SyncComponent`: when upload completes, mark file as synced and remove from queue.

### Observability Hooks
- Provide structured logging (JSON) for session transitions.
- Expose `/api/sync/status` metrics for dashboards.

---

## Task 10: Testing Strategy

### Unit Tests
- `ChangeDetector` – detect added/modified/deleted files.
- `MerkleTree` – diff scenarios.
- `SyncSession` – state transitions and error recovery.
- `FileTransferService` – chunk ordering, resume, corruption detection.
- `ConflictResolver` – strategies produce expected metadata.

### Integration Tests
- **Full Sync Cycle** from `DEVELOPMENT_GUIDE.md:566`: client A → server → client B share files.
- Concurrent clients uploading different files.
- Conflict scenario requiring manual resolution.
- Resume after network interruption (simulate by dropping connection mid-transfer).

### Performance / Stress
- Benchmark syncing 1k files (1 GiB) per `DEVELOPMENT_GUIDE.md:590`.
- Profile CPU & memory usage when computing diffs on 10k files.
- Load test `/api/file/upload` with multiple concurrent chunk streams.

### Tooling
- Use Google Test for unit/integration.
- Provide helper scripts under `tests/scripts/` for stress runs.
- Consider using `fs::temp_directory_path()` for isolated integration tests.

---

## Task 11: Performance & Resilience

- **Retry & Resume:** store chunk acknowledgements; client only retransmits missing indices.
- **Back-pressure:** respect server responses indicating rate limits or storage quotas.
- **Timeouts:** session manager expires idle sessions (configurable, default 5 min).
- **Parallelism:** allow multiple files/chunks in flight (configurable concurrency); use `ThreadSafeQueue` for worker threads (`docs/phase_3_code_reference.md:549`).
- **Security placeholders:** even if full auth waits for later phases, design endpoints to accept auth headers/tokens for future upgrade.
- **Disk Safety:** write chunks to temporary files, fsync after completion, rename atomically to final path.

---

## Files to Create / Update

```
include/dfs/sync/
  ├── types.hpp            # Shared sync structs
  ├── change_detector.hpp  # Task 2 API
  ├── merkle_tree.hpp      # Task 3
  ├── session.hpp          # Task 4
  ├── transfer.hpp         # Task 5
  ├── conflict.hpp         # Task 8

src/sync/
  ├── change_detector.cpp
  ├── merkle_tree.cpp
  ├── session.cpp
  ├── transfer.cpp
  ├── conflict.cpp

src/server/
  ├── sync_handlers.cpp    # Task 6
  ├── CMakeLists.txt       # Update to include new files

src/client/
  ├── sync_client.cpp      # Task 7 (new or enhanced)

include/dfs/events/events.hpp   # Extend with sync events
include/dfs/events/components.hpp # Logger/Metrics updates

examples/
  ├── sync_workflow_example.cpp  # Optional walkthrough demo

tests/sync/
  ├── change_detector_test.cpp
  ├── merkle_tree_test.cpp
  ├── session_test.cpp
  ├── transfer_test.cpp
  ├── conflict_test.cpp
  ├── integration_full_sync_test.cpp
```

---

## Learning Objectives

- Build distributed-state reconciliation systems (diff, merge, conflict resolution).
- Design HTTP control planes and binary transfer protocols.
- Apply concurrency primitives (queues, worker pools) to background processing.
- Practice robust error handling and observability in long-lived sessions.

---

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Large directory diff is slow | Delays sync start | Use Merkle tree diff; cache hashes; parallelize hashing. |
| Chunk corruption/duplication | Data loss or inconsistency | Include chunk hashes; verify on server; request retransmit. |
| Session timeout mid-transfer | Stale locks or partial files | Store progress, allow resume, clean temp files on timeout. |
| Conflicts ignored | Users lose data | Emit conflict events, require explicit resolution before marking complete. |
| Event storm | Metrics spam | Batch events or throttle logging for frequent chunk events. |

---

## Next Steps (Phase 5 Preview)

Phase 5 adds OS-native file watchers to feed the change detector in real time (`DEVELOPMENT_GUIDE.md:628`). To prepare:
- Keep `ChangeDetector` interface abstract so future watchers can push events.
- Separate platform-specific file monitoring code from sync logic.
- Ensure Sync engine can accept externally triggered file changes without rescanning entire directories.

---

**Phase 4 is where the project becomes a true distributed file sync system: metadata intelligence, network transfer, conflict safety, and operational visibility all come together.** 🚀
