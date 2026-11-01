// SPDX-License-Identifier: MIT
#pragma once
#include "cyvr/core.hpp"
#include "cyvr/spatial.hpp"

namespace cyvr {
// Four-finger closure; an open-hand index/thumb selection pinch stays separate.
float handGrabStrength(const HandJoints &joints);
class HandGrabRay {
  public:
    bool update(Pose wrist, bool wristValid, Pose aim, bool aimValid, float grip, Pose &ray);
    void reset() { haveAim_ = closed_ = false; }

  private:
    Pose wrist_{}, aim_{};
    bool haveAim_{}, closed_{};
};
uint8_t windowEdgesAt(Vec2 pixels, const PanelGeometry &panel, const WindowOptions &options);
struct WindowPointer {
    Vec3 origin{}, direction{0, 0, -1};
    Hit hit;
    Vec2 stick{};
    float grip{}, deltaSeconds{1.f / 72};
    bool valid{}, onPanel{}, pointerDrag{};
};
struct WindowMotionResult {
    bool consumed{}, started{}, ended{}, changed{}, resizing{}, pointerDrag{};
};
class WindowMotion {
  public:
    WindowMotionResult update(int hand, const WindowPointer &input, PanelGeometry &panel, Pose viewer,
                              bool viewerValid, const WindowOptions &options);
    void cancel();
    void recenter() {
        cancel();
        yawOffset_ = 0;
    }
    bool active() const { return hand_ >= 0; }
    int hand() const { return hand_; }
    uint8_t edges() const { return edges_; }
    Quat facing(Vec3 center, Vec3 viewer) const;

  private:
    int hand_{-1};
    bool pointerDrag_{};
    uint8_t edges_{};
    std::array<bool, 2> gripHeld_{}, gripArmed_{}, pointerArmed_{};
    float depth_{}, yawOffset_{};
    Vec3 localGrab_{}, fixedAnchor_{};
    Vec2 resizeStart_{}, anchorUnit_{};
    PanelGeometry start_;
    bool resizeReference(const WindowPointer &input, const PanelGeometry &panel);
};
} // namespace cyvr
