#include <UI/Widgets/RowDrag.h>

#include <DesignSystem/DesignSystem.h>
#include <algorithm>
#include <cmath>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

// Seconds for a row to travel to its new slot. Token-driven (milliseconds, like
// every other duration in the system) so the whole app's motion stays tunable
// from one place.
float SlideSeconds() {
    try {
        return DS::DesignSystem::Instance().GetFloat(Tok::C_ListRow_ReorderDuration)
               * 0.001f;
    } catch (...) { return 0.1f; }
}

// The slot row `index` occupies once the grabbed row has moved to slot `land`:
// everything between the two ends shuffles one place towards the vacated slot.
float SlotOf(int index, int src, int land) {
    if (index == src) return (float)land;
    if (src < land && index > src && index <= land) return (float)(index - 1);
    if (land < src && index >= land && index < src) return (float)(index + 1);
    return (float)index;
}
} // namespace

RowDrag::RowDrag(const char* id, int count, float pitch)
    : count_(count), pitch_(pitch) {
    st_ = ImGui::GetStateStorage();
    ImGui::PushID(id);
    kSrc_  = ImGui::GetID("##rdSrc");
    kLand_ = ImGui::GetID("##rdLand");
    kAnim_ = ImGui::GetID("##rdAnim");
    ImGui::PopID();

    h_.assign((std::size_t)std::max(0, count), pitch);
    src_  = st_->GetInt(kSrc_, -1);
    land_ = st_->GetInt(kLand_, -1);
    anim_ = st_->GetFloat(kAnim_, 0.0f);

    // The gesture belongs to ImGui: when the payload dies — dropped, cancelled,
    // or the window lost — so does the displacement. A stale index from a list
    // that shrank goes the same way.
    if (ImGui::GetDragDropPayload() == nullptr || src_ >= count_ || count_ <= 0) {
        src_ = land_ = -1;
        st_->SetInt(kSrc_, -1);
        st_->SetInt(kLand_, -1);
        return;
    }

    // Ease the fractional slot towards the landing one. Exponential, framerate
    // independent, and tuned to cover ~95 % of the distance in the token's
    // duration.
    const float dt = ImGui::GetIO().DeltaTime;
    const float sec = SlideSeconds();
    const float target = (float)std::clamp(land_, 0, count_ - 1);
    if (sec <= 0.0f || dt <= 0.0f) {
        anim_ = target;
    } else {
        anim_ += (target - anim_) * (1.0f - std::exp(-dt * 3.0f / sec));
        if (std::fabs(target - anim_) < 0.001f) anim_ = target;
    }
    st_->SetFloat(kAnim_, anim_);
}

void RowDrag::SetRowHeights(const float* heights, int count) {
    if (!heights || count != count_) return;   // stale table: keep the pitch
    h_.assign(heights, heights + count);
    built_ = false;
}

// The offsets for ONE whole arrangement: every row's top in the list as it
// would be once the grabbed row sat in slot `land`, minus its top now.
std::vector<float> RowDrag::Arrange(int land) const {
    std::vector<int> slot((std::size_t)count_), rowAt((std::size_t)count_);
    for (int i = 0; i < count_; ++i) slot[(std::size_t)i] = (int)SlotOf(i, src_, land);
    for (int i = 0; i < count_; ++i) rowAt[(std::size_t)slot[(std::size_t)i]] = i;
    std::vector<float> top((std::size_t)count_ + 1, 0.0f);
    std::vector<float> newTop((std::size_t)count_ + 1, 0.0f);
    for (int i = 0; i < count_; ++i) {
        top[(std::size_t)i + 1] = top[(std::size_t)i] + h_[(std::size_t)i];
        newTop[(std::size_t)i + 1] =
            newTop[(std::size_t)i] + h_[(std::size_t)rowAt[(std::size_t)i]];
    }
    std::vector<float> out((std::size_t)count_);
    for (int i = 0; i < count_; ++i)
        out[(std::size_t)i] = newTop[(std::size_t)slot[(std::size_t)i]] -
                              top[(std::size_t)i];
    return out;
}

void RowDrag::Build() const {
    if (built_ || src_ < 0) return;
    built_ = true;
    // The animation sits BETWEEN two whole arrangements; blending them keeps
    // every row's travel continuous even when the rows are of unequal height.
    const int lo = std::clamp((int)std::floor(anim_), 0, count_ - 1);
    const int hi = std::clamp(lo + 1, 0, count_ - 1);
    const float t = std::clamp(anim_ - (float)lo, 0.0f, 1.0f);
    const std::vector<float> a = Arrange(lo);
    const std::vector<float> b = Arrange(hi);
    off_.resize((std::size_t)count_);
    for (int i = 0; i < count_; ++i)
        off_[(std::size_t)i] = a[(std::size_t)i] * (1.0f - t) +
                               b[(std::size_t)i] * t;
}

void RowDrag::SetSource(int index) {
    if (index < 0 || index >= count_) return;
    if (src_ != index) {
        // A fresh grab starts from rest: the row sits in its own slot and
        // nothing has stepped aside yet.
        src_ = index;
        land_ = index;
        anim_ = (float)index;
        st_->SetInt(kSrc_, index);
        st_->SetInt(kLand_, index);
        st_->SetFloat(kAnim_, anim_);
        built_ = false;
    }
    if (newLand_ < 0) newLand_ = index;
}

void RowDrag::SetLanding(int index) {
    if (src_ < 0) return;
    newLand_ = std::clamp(index, 0, count_ - 1);
}

void RowDrag::SetLandingAtBoundary(int boundary) {
    if (src_ < 0) return;
    const int b = std::clamp(boundary, 0, count_);
    // Pulling the row out first shifts everything below it up by one, so an
    // insertion boundary past the row's own position lands one slot earlier.
    SetLanding(src_ < b ? b - 1 : b);
}

void RowDrag::SetNoGap() {
    if (src_ < 0) return;
    newLand_ = src_;
}

float RowDrag::Offset(int index) const {
    if (src_ < 0 || index < 0 || index >= count_) return 0.0f;
    if (index == src_) {
        // The grabbed row tracks the cursor exactly — no easing, no snapping to
        // slots. It is the thing being held.
        const ImGuiIO& io = ImGui::GetIO();
        return io.MousePos.y - io.MouseClickedPos[0].y;
    }
    Build();
    return off_.empty() ? 0.0f : off_[(std::size_t)index];
}

float RowDrag::GapOffset(int index) const {
    if (src_ < 0 || index < 0 || index >= count_) return 0.0f;
    Build();
    return off_.empty() ? 0.0f : off_[(std::size_t)index];
}

int RowDrag::Slot(int index) const {
    if (src_ < 0 || index < 0 || index >= count_) return index;
    return (int)SlotOf(index, src_, std::clamp(land_, 0, count_ - 1));
}

void RowDrag::DrawSlot(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius) {
    if (!dl || b.x <= a.x || b.y <= a.y) return;
    auto& ds = DS::DesignSystem::Instance();
    ImVec4 c(0.95f, 0.55f, 0.15f, 1.0f);
    float ghost = 0.55f;
    try { c = ds.GetColor(Tok::C_ListRow_DropSlot); } catch (...) {}
    try { ghost = ds.GetFloat(Tok::C_ListRow_DragAlpha); } catch (...) {}
    c.w *= ghost;
    dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(c), radius);
}

void RowDrag::End() {
    if (src_ < 0) return;
    // Nothing published means the cursor left every target: hold the last known
    // landing rather than snapping the list shut and open again as it grazes
    // the edge.
    if (newLand_ >= 0) {
        land_ = newLand_;
        st_->SetInt(kLand_, land_);
    }
}

} // namespace UI
