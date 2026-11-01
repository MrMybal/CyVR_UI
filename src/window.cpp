// SPDX-License-Identifier: MIT
#include "cyvr/window.hpp"

namespace cyvr {
namespace {
Quat multiply(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Vec3 inverseRotate(Quat q, Vec3 p) {
    return rotate({-q.x, -q.y, -q.z, q.w}, p);
}
float finite(float v, float fallback) {
    return std::isfinite(v) ? v : fallback;
}
WindowOptions safe(WindowOptions o) {
    o.minWidth = std::clamp(finite(o.minWidth, .45f), .2f, 5.f);
    o.maxWidth = std::clamp(finite(o.maxWidth, 3.f), o.minWidth, 6.f);
    o.minDistance = std::clamp(finite(o.minDistance, .65f), .3f, 5.f);
    o.maxDistance = std::clamp(finite(o.maxDistance, 4.f), o.minDistance, 8.f);
    o.edgePixels = std::clamp(finite(o.edgePixels, 28), 4.f, 96.f);
    o.moveSpeed = std::clamp(finite(o.moveSpeed, 1.3f), 0.f, 5.f);
    o.turnSpeed = std::clamp(finite(o.turnSpeed, 1.6f), 0.f, 4.f);
    o.stickDeadZone = std::clamp(finite(o.stickDeadZone, .18f), 0.f, .8f);
    return o;
}
float axis(float v, float dead) {
    v = std::clamp(finite(v, 0), -1.f, 1.f);
    return std::abs(v) <= dead ? 0 : std::copysign((std::abs(v) - dead) / (1 - dead), v);
}
bool validRay(const WindowPointer &p) {
    return p.valid &&
           std::isfinite(p.origin.x + p.origin.y + p.origin.z + p.direction.x + p.direction.y +
                         p.direction.z) &&
           length(p.direction) > .001f;
}
Vec3 limited(Vec3 center, Vec3 head, const WindowOptions &o) {
    auto delta = center - head;
    float distance = length(delta);
    return head + normalized(delta) * std::clamp(distance, o.minDistance, o.maxDistance);
}
} // namespace
float handGrabStrength(const HandJoints &joints) {
    float strength = 1;
    for (int base : {7, 12, 17, 22}) {
        for (int j = base; j <= base + 3; j++)
            if (!joints[j].valid)
                return 0;
        auto p = [&](int j) { return joints[j].pose.position; };
        float chain = length(p(base + 1) - p(base)) + length(p(base + 2) - p(base + 1)) +
                      length(p(base + 3) - p(base + 2));
        float span = length(p(base + 3) - p(base));
        if (!std::isfinite(chain + span) || chain < .01f)
            return 0;
        strength = std::min(strength, std::clamp((1 - span / chain - .15f) / .45f, 0.f, 1.f));
    }
    return strength;
}
bool HandGrabRay::update(Pose wrist, bool wristValid, Pose aim, bool aimValid, float grip, Pose &ray) {
    if (!wristValid) {
        reset();
        return false;
    }
    bool closed = grip > (closed_ ? .35f : .65f);
    if (aimValid && (!closed_ || !closed)) {
        wrist_ = wrist;
        aim_ = aim;
        haveAim_ = true;
    }
    closed_ = closed;
    if (!closed_) {
        if (!aimValid) {
            reset();
            return false;
        }
        ray = aim;
        return true;
    }
    if (!haveAim_)
        return false;
    // Keep the last open-hand ray relative to the wrist while fingers occlude aim tracking.
    Quat delta = multiply(wrist.orientation, {-wrist_.orientation.x, -wrist_.orientation.y,
                                              -wrist_.orientation.z, wrist_.orientation.w});
    ray.position = wrist.position + rotate(delta, aim_.position - wrist_.position);
    ray.orientation = multiply(delta, aim_.orientation);
    return true;
}
uint8_t windowEdgesAt(Vec2 p, const PanelGeometry &panel, const WindowOptions &options) {
    if (!options.resizable || !std::isfinite(p.x + p.y) || p.x < 0 || p.y < 0 || p.x > panel.pixelsW ||
        p.y > panel.pixelsH)
        return 0;
    float edge = safe(options).edgePixels;
    uint8_t result = 0;
    if (p.x <= edge)
        result |= EdgeLeft;
    else if (p.x >= panel.pixelsW - edge)
        result |= EdgeRight;
    if (p.y <= edge)
        result |= EdgeTop;
    else if (p.y >= panel.pixelsH - edge)
        result |= EdgeBottom;
    return result;
}
Quat WindowMotion::facing(Vec3 center, Vec3 viewer) const {
    return multiply(faceViewer(center, viewer), yawRotation(yawOffset_));
}
void WindowMotion::cancel() {
    hand_ = -1;
    edges_ = 0;
    gripArmed_.fill(false);
    pointerArmed_.fill(false);
    gripHeld_.fill(false);
}
bool WindowMotion::resizeReference(const WindowPointer &input, const PanelGeometry &panel) {
    auto hit = intersectPanel(input.origin, input.direction, panel, false);
    if (!hit.valid)
        return false;
    start_ = panel;
    auto p = inverseRotate(panel.pose.orientation, hit.world - panel.pose.position);
    resizeStart_ = {p.x, p.y};
    anchorUnit_ = {(edges_ & EdgeLeft)    ? .5f
                   : (edges_ & EdgeRight) ? -.5f
                                          : 0,
                   (edges_ & EdgeTop)      ? -.5f
                   : (edges_ & EdgeBottom) ? .5f
                                           : 0};
    fixedAnchor_ = panel.pose.position + rotate(panel.pose.orientation, {anchorUnit_.x * panel.width,
                                                                         anchorUnit_.y * panel.height, 0});
    return true;
}
WindowMotionResult WindowMotion::update(int hand, const WindowPointer &input, PanelGeometry &panel,
                                        Pose viewer, bool viewerValid, const WindowOptions &rawOptions) {
    WindowMotionResult result;
    if (hand < 0 || hand > 1)
        return result;
    auto options = safe(rawOptions);
    bool valid = validRay(input) && viewerValid &&
                 std::isfinite(viewer.position.x + viewer.position.y + viewer.position.z);
    bool held = valid && input.grip > (gripHeld_[hand] ? .35f : .65f);
    bool gripBegin = held && !gripHeld_[hand] && gripArmed_[hand];
    bool pointerBegin = input.pointerDrag && pointerArmed_[hand];
    gripHeld_[hand] = held;
    if (!valid) {
        gripArmed_[hand] = false;
        pointerArmed_[hand] = false;
    } else {
        if (!held)
            gripArmed_[hand] = true;
        else
            gripArmed_[hand] = false;
        if (!input.pointerDrag)
            pointerArmed_[hand] = true;
        else
            pointerArmed_[hand] = false;
    }
    if (!active() && valid && input.hit.valid &&
        std::isfinite(input.hit.world.x + input.hit.world.y + input.hit.world.z) &&
        (gripBegin || pointerBegin)) {
        uint8_t edges = gripBegin && input.onPanel ? windowEdgesAt(input.hit.pixels, panel, options) : 0;
        if ((edges && options.resizable) || options.movable) {
            hand_ = hand;
            pointerDrag_ = pointerBegin && !gripBegin;
            edges_ = pointerDrag_ ? 0 : edges;
            depth_ = length(input.hit.world - input.origin);
            localGrab_ = inverseRotate(panel.pose.orientation, input.hit.world - panel.pose.position);
            start_ = panel;
            if (edges_ && !resizeReference(input, panel)) {
                hand_ = -1;
                edges_ = 0;
                return result;
            }
            result.started = true;
        }
    }
    if (hand_ != hand)
        return result;
    result.consumed = true;
    result.pointerDrag = pointerDrag_;
    result.resizing = edges_ != 0;
    if (!valid || (edges_ ? !options.resizable : !options.movable) ||
        (pointerDrag_ ? !input.pointerDrag : !held)) {
        result.ended = true;
        hand_ = -1;
        edges_ = 0;
        return result;
    }
    // Starting a gesture must preserve the panel pose exactly.
    if (result.started)
        return result;
    const auto previous = panel;
    float dt = std::clamp(finite(input.deltaSeconds, 0), 0.f, .05f);
    float push = axis(input.stick.y, options.stickDeadZone) * options.moveSpeed * dt;
    float turn = axis(input.stick.x, options.stickDeadZone) * options.turnSpeed * dt;
    if (!edges_) {
        depth_ = std::clamp(depth_ + push, options.minDistance, options.maxDistance + length(localGrab_));
        Vec3 point = input.origin + normalized(input.direction) * depth_;
        Vec3 center = point - rotate(panel.pose.orientation, localGrab_);
        if (options.faceViewerWhileMoving) {
            for (int iteration = 0; iteration < 3; iteration++) {
                panel.pose.orientation = facing(center, viewer.position);
                center = point - rotate(panel.pose.orientation, localGrab_);
            }
        }
        panel.pose.position =
            limited(point - rotate(panel.pose.orientation, localGrab_), viewer.position, options);
        if (turn) {
            yawOffset_ = std::remainder(yawOffset_ + turn, 2 * pi);
            panel.pose.orientation = multiply(panel.pose.orientation, yawRotation(turn));
            // Manual yaw rotates about the panel centre, independently of the grabbed point.
            localGrab_ = inverseRotate(panel.pose.orientation, point - panel.pose.position);
        }
    } else {
        auto hit = intersectPanel(input.origin, input.direction, start_, false);
        if (hit.valid) {
            auto current = inverseRotate(start_.pose.orientation, hit.world - start_.pose.position);
            float w = start_.width, h = start_.height;
            if (edges_ & (EdgeLeft | EdgeRight))
                w += (current.x - resizeStart_.x) * ((edges_ & EdgeRight) ? 1 : -1);
            if (edges_ & (EdgeTop | EdgeBottom))
                h += (current.y - resizeStart_.y) * ((edges_ & EdgeTop) ? 1 : -1);
            float factor;
            if (!(edges_ & (EdgeTop | EdgeBottom)))
                factor = w / start_.width;
            else if (!(edges_ & (EdgeLeft | EdgeRight)))
                factor = h / start_.height;
            else
                factor = (w * start_.width + h * start_.height) /
                         (start_.width * start_.width + start_.height * start_.height);
            panel.width = std::clamp(start_.width * factor, options.minWidth, options.maxWidth);
            panel.height = panel.width * start_.height / start_.width;
            panel.pose.orientation = start_.pose.orientation;
            panel.pose.position =
                fixedAnchor_ - rotate(panel.pose.orientation,
                                      {anchorUnit_.x * panel.width, anchorUnit_.y * panel.height, 0});
        }
        if (push || turn) {
            panel.pose.position =
                limited(panel.pose.position + normalized(input.direction) * push, viewer.position, options);
            if (turn) {
                panel.pose.orientation = multiply(panel.pose.orientation, yawRotation(turn));
                yawOffset_ = std::remainder(yawOffset_ + turn, 2 * pi);
            }
            resizeReference(input, panel);
        }
    }
    result.changed = length(previous.pose.position - panel.pose.position) > .00001f ||
                     std::abs(previous.width - panel.width) > .00001f ||
                     std::abs(previous.pose.orientation.x - panel.pose.orientation.x) +
                             std::abs(previous.pose.orientation.y - panel.pose.orientation.y) +
                             std::abs(previous.pose.orientation.z - panel.pose.orientation.z) +
                             std::abs(previous.pose.orientation.w - panel.pose.orientation.w) >
                         .00001f;
    return result;
}
} // namespace cyvr
