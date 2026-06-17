#pragma once

#include <functional>
#include <string>
#include <vector>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  UndoStack — a snapshot-based undo/redo history.
//
//  Generic over the SNAPSHOT type (a self-contained copy of the editable state):
//   • the viewport history snapshots a `Renderer::Document` (by value);
//   • the Preferences history snapshots the design-system overrides + keymap as
//     a serialised string.
//
//  Each window owns its OWN UndoStack (Blender-style: undoing in the viewport
//  must not touch the Preferences history and vice-versa).
//
//  Model: a linear list of snapshots with a cursor. index_ points at the CURRENT
//  state. Commit() captures the live state and pushes it as a new step, dropping
//  any redo tail and trimming the oldest steps past `capacity_`. Undo()/Redo()
//  move the cursor and hand the snapshot to the restore callback.
//
//  The baseline (the state before the first edit) is itself step 0, seeded by
//  Reset(), so the very first Undo returns to the as-loaded document.
// ─────────────────────────────────────────────────────────────────────────────

template <class Snapshot>
class UndoStack {
public:
    using CaptureFn = std::function<Snapshot()>;             // serialise live state
    using RestoreFn = std::function<void(const Snapshot&)>;  // apply a snapshot

    void Configure(CaptureFn capture, RestoreFn restore) {
        capture_ = std::move(capture);
        restore_ = std::move(restore);
    }

    // Number of steps kept (the current state + its undo history). Trimming keeps
    // the most recent steps. Default 256 (configurable in Preferences ▸ General).
    void SetCapacity(int n) {
        capacity_ = n < 2 ? 2 : n;
        Trim();
    }
    int Capacity() const { return capacity_; }

    // Drop all history and seed the baseline snapshot from the live state.
    void Reset() {
        steps_.clear();
        labels_.clear();
        index_ = -1;
        if (capture_) {
            steps_.push_back(capture_());
            labels_.push_back("Original");
            index_ = 0;
        }
    }

    // Capture the live state as a new step. Drops the redo tail (everything after
    // index_), appends, advances the cursor, then trims to capacity.
    void Commit(const std::string& label) {
        if (!capture_ || index_ < 0) { Reset(); if (index_ < 0) return; }
        // Drop redo tail.
        if ((int)steps_.size() > index_ + 1) {
            steps_.resize((size_t)index_ + 1);
            labels_.resize((size_t)index_ + 1);
        }
        steps_.push_back(capture_());
        labels_.push_back(label);
        index_ = (int)steps_.size() - 1;
        Trim();
    }

    bool CanUndo() const { return index_ > 0; }
    bool CanRedo() const { return index_ >= 0 && index_ + 1 < (int)steps_.size(); }

    // Step back one snapshot and restore it. Returns the label of the step we
    // UNDID (the one we left), or "" if nothing to undo.
    std::string Undo() {
        if (!CanUndo()) return "";
        std::string undone = labels_[(size_t)index_];
        --index_;
        if (restore_) restore_(steps_[(size_t)index_]);
        return undone;
    }

    // Step forward one snapshot and restore it. Returns the label of the step we
    // REDID, or "" if nothing to redo.
    std::string Redo() {
        if (!CanRedo()) return "";
        ++index_;
        if (restore_) restore_(steps_[(size_t)index_]);
        return labels_[(size_t)index_];
    }

    // The label of the step that the next Undo would revert (for UI hints).
    const std::string& NextUndoLabel() const {
        static const std::string kEmpty;
        return CanUndo() ? labels_[(size_t)index_] : kEmpty;
    }
    const std::string& NextRedoLabel() const {
        static const std::string kEmpty;
        return CanRedo() ? labels_[(size_t)index_ + 1] : kEmpty;
    }

    // Read-only introspection for the debug Dev panel: the step labels and the
    // cursor (the index of the CURRENT state). steps[0..index] are the undo
    // history (index = now); steps[index+1..] are the redo tail.
    const std::vector<std::string>& Labels() const { return labels_; }
    int   CurrentIndex() const { return index_; }
    int   Size() const { return (int)steps_.size(); }

private:
    // Trim the OLDEST steps so the list never exceeds capacity_. The cursor is
    // shifted to keep pointing at the same logical state.
    void Trim() {
        int overflow = (int)steps_.size() - capacity_;
        if (overflow <= 0) return;
        steps_.erase(steps_.begin(), steps_.begin() + overflow);
        labels_.erase(labels_.begin(), labels_.begin() + overflow);
        index_ -= overflow;
        if (index_ < 0) index_ = 0;
    }

    std::vector<Snapshot>    steps_;
    std::vector<std::string> labels_;
    int                      index_ = -1;     // current state (−1 = uninitialised)
    int                      capacity_ = 256;
    CaptureFn                capture_;
    RestoreFn                restore_;
};

} // namespace App
