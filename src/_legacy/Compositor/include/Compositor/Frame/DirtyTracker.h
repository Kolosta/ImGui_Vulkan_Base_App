#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor - Frame/DirtyTracker (Lot 13-1a): per-shape change detection.
//
//  Today a view rebuilds+re-uploads its WHOLE geometry whenever its single global
//  signature changes — even if only one shape moved among thousands. The first
//  step to fixing that (the persistent per-shape pool, 13-1b) is to know exactly
//  WHICH shapes changed. This tracker keeps last build's per-shape content hash
//  (Shape::id → hash) for a view and, on the next build, diffs against it: how
//  many ids were ADDED, CHANGED, or REMOVED. The count feeds the HUD now and
//  drives incremental upload later; the map is the per-view baseline to compare to.
//
//  It is intentionally tiny + owned per ViewTarget (each view has its own detail
//  bucket, so its per-shape hashes differ). The signature the renderer already
//  used to gate rebuilds is a by-product (fold the same walk into one pass).
// ─────────────────────────────────────────────────────────────────────────────

namespace Comp {

struct DirtyTracker {
    // Shape::id → the content hash at the last build. Rebuilt each diff.
    std::unordered_map<uint64_t, uint64_t> prevHash;

    // Result of one diff against `prevHash`.
    struct Diff {
        int added   = 0;
        int changed = 0;
        int removed = 0;
        int Dirty() const { return added + changed + removed; }
    };

    // Begin a fresh diff pass: callers Feed() every current shape, then call
    // Finish() to fold in removals and swap in the new baseline.
    void Begin() { cur_.clear(); diff_ = Diff{}; dirtyIds.clear(); }

    // Record one current shape (id + its content hash). Classifies it against the
    // previous baseline as added (unseen id) or changed (same id, new hash); an
    // unchanged shape contributes nothing to the dirty count. An added/changed id is
    // remembered in `dirtyIds` so the pool re-uploads exactly those (13-1b).
    void Feed(uint64_t id, uint64_t hash) {
        cur_[id] = hash;
        auto it = prevHash.find(id);
        if (it == prevHash.end())      { ++diff_.added;   dirtyIds.insert(id); }
        else if (it->second != hash)   { ++diff_.changed; dirtyIds.insert(id); }
    }

    // The set of ids added or changed this pass (the ones the pool must re-upload);
    // valid after Finish(). Unchanged ids are absent → the pool just Touch()es them.
    std::unordered_set<uint64_t> dirtyIds;
    bool IsDirty(uint64_t id) const { return dirtyIds.count(id) != 0; }

    // Finish the pass: any id present last time but not fed this time was REMOVED.
    // Swaps the current map in as the new baseline and returns the diff.
    Diff Finish() {
        for (const auto& kv : prevHash)
            if (cur_.find(kv.first) == cur_.end()) ++diff_.removed;
        prevHash.swap(cur_);
        cur_.clear();
        return diff_;
    }

private:
    std::unordered_map<uint64_t, uint64_t> cur_;
    Diff diff_;
};

} // namespace Comp
