#include "ChainScreen.h"

#include <algorithm>

#include "widgets/Popover.h"

namespace t3k::ui {

ChainScreen::ChainScreen(Services& services)
    : services_(services), gallery_(services), node_(services) {
  gallery_.onOpenBlock = [this](const std::string& id) { openDetail(id); };
  gallery_.onSelectTone = [this](ChainSide side, const std::string& id) {
    if (onSelectTone) onSelectTone(side, id);
  };
  // A tone node clicked in the grid opens its detail card (the grid is a
  // views layer; the card is where a tone block's settings live).
  node_.onOpenTone = [this](const std::string& id) { openDetail(id); };
  addAndMakeVisible(gallery_);
  addAndMakeVisible(node_);
  node_.setNodeView(false);
  // Cold start in the GALLERY view never calls setView (nodeView_ is already
  // false), so the NodeView container — a full-bounds sibling added AFTER the
  // gallery, thus on top — must be hidden explicitly. setNodeView(false) only
  // hides its children; a visible empty container still eats every click and
  // wheel over the gallery, so the restart-in-gallery app looked like the node
  // view was overlaying it (user m03562). setView(true) will setVisible(true)
  // when the pref is on.
  node_.setVisible(false);

  // The view mode is a UiPref (the ticket's views layer: not a block param,
  // no four-state-place work).
  const bool nodeView = services_.prefs.getBool(UiPrefs::kNodeView, false);
  if (nodeView) setView(true);

  // Reopen the card a swap / OAuth round trip left behind, unless its block
  // is gone (the chain changed underneath the round trip).
  const auto saved = services_.prefs.session.find(UiPrefs::kDetailBlockId);
  if (saved != services_.prefs.session.end() && saved->second.isNotEmpty()) {
    const auto id = saved->second.toStdString();
    if (services_.chain.state().findBlock(id) != nullptr)
      openDetail(id);
    else
      services_.prefs.session.erase(UiPrefs::kDetailBlockId);
  }
}

ChainScreen::~ChainScreen() = default;

void ChainScreen::openDetail(const std::string& blockId) {
  services_.prefs.session[UiPrefs::kDetailBlockId] = juce::String(blockId);
  detail_ = std::make_unique<BlockDetail>(services_, blockId);
  // The card may ask to close from inside a chain listener callback (its
  // block vanished); tear it down once that callback has unwound.
  detail_->onBack = [this] {
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ChainScreen>(this)] {
      if (safe != nullptr) safe->closeDetail();
    });
  };
  detail_->onSwap = [this](const std::string& id) {
    // The swap keeps the block id; the detail id stays saved for the return.
    const auto& state = services_.chain.state();
    const bool right = state.chainRight && std::any_of(state.chainRight->begin(), state.chainRight->end(),
                                                       [&](const ChainItem& b) { return b.blockId == id; });
    if (onSelectTone) onSelectTone(right ? ChainSide::right : ChainSide::left, id);
  };
  addAndMakeVisible(*detail_);
  // The takeover hides BOTH views; closeDetail restores the one the user
  // was in (nodeView_ decides).
  gallery_.setVisible(false);
  node_.setVisible(false);
  resized();
}

void ChainScreen::closeDetail() {
  services_.prefs.session.erase(UiPrefs::kDetailBlockId);
  if (!detail_) return;
  detail_.reset();
  // The container (not just its children) must be visible for the view the
  // user was in — a visible empty container would swallow gallery clicks and
  // wheel (m00157 item 7: "can't select / can't scroll in the gallery").
  gallery_.setVisible(!nodeView_);
  node_.setNodeView(nodeView_);
  resized();
}

void ChainScreen::setView(bool nodeView) {
  if (nodeView_ == nodeView) return;
  nodeView_ = nodeView;
  gallery_.setVisible(!nodeView);
  // The NodeView (added AFTER the gallery, so in front) must disappear from
  // the hit-test, not just have its children hidden: a visible empty
  // container still eats every click and wheel event over the gallery.
  node_.setVisible(nodeView);
  // A strip mode/type menu is a Popover parented to the PluginRoot overlay
  // layer; hiding the strip does not reach it (the anchor's ANCESTOR is
  // what goes off), so a panel would ghost over the new view. Sweep the
  // whole layer at the toggle.
  if (!nodeView) {
    if (auto* host = findParentComponentOfClass<OverlayHost>())
      Popover::dismissAll(host->overlayLayer());
  }
  node_.setNodeView(nodeView);
  resized();
}

void ChainScreen::returnToGallery() {
  closeDetail();
  gallery_.returnToGallery();
}

void ChainScreen::resized() {
  gallery_.setBounds(getLocalBounds());
  node_.setBounds(getLocalBounds());
  if (detail_) detail_->setBounds(getLocalBounds());
}

}  // namespace t3k::ui
