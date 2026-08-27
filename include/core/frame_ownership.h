#pragma once

namespace core::frame {

enum class State {
  kUnavailable,
  kInvalid,
  kComposing,
  kDisplayTransfer,
  kValid,
  kPngScratch,
};

/**
 * Pure state machine for the one buffer shared by radar composition, SPI DMA,
 * and the terrain PNG decoder. The platform owns the actual synchronization;
 * this class makes illegal reuse explicit and host-testable.
 */
class Ownership {
 public:
  State state() const { return state_; }

  void allocated() { state_ = State::kInvalid; }

  bool beginComposition() {
    if (state_ != State::kInvalid && state_ != State::kValid) return false;
    state_ = State::kComposing;
    return true;
  }

  bool beginTransfer() {
    if (state_ != State::kComposing) return false;
    state_ = State::kDisplayTransfer;
    return true;
  }

  bool finishTransfer() {
    if (state_ != State::kDisplayTransfer) return false;
    state_ = State::kValid;
    return true;
  }

  bool acquireScratch() {
    if (state_ != State::kInvalid && state_ != State::kValid) return false;
    state_ = State::kPngScratch;
    return true;
  }

  bool releaseScratch() {
    if (state_ != State::kPngScratch) return false;
    // PNG work bytes are not pixels. A complete composition is mandatory
    // before this buffer may enter another display transfer.
    state_ = State::kInvalid;
    return true;
  }

 private:
  State state_ = State::kUnavailable;
};

}  // namespace core::frame
