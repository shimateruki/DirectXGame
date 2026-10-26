#include "game/actor/player/movement/PlayerLocomotion.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static bool Near(float a, float b) { return std::abs(a - b) < 0.0002f; }
static engine::Vector3 Simulate(const PlayerLocomotionSettings& settings, engine::Vector3 velocity,
                              const engine::Vector3& target, float seconds, int fps, bool grounded = true) {
    float elapsed = 0.0f;
    while (seconds - elapsed > 0.000001f) {
        const float step = (std::min)(1.0f / fps, seconds - elapsed);
        velocity = settings.Apply(velocity, target, 10.0f, grounded, step);
        elapsed += step;
    }
    return velocity;
}
int main() {
    try {
        PlayerLocomotionSettings settings;
        const auto first = settings.Apply({0, -7, 0}, {10, 0, 0}, 10, true, 1.0f / 60);
        Require(first.x > 0 && first.x < 1 && first.y == -7, "startup snapped to maximum or changed gravity");
        Require(Near(Simulate(settings, {}, {10, 0, 0}, 0.4f, 60).x, 10), "maximum not reached at configured time");
        Require(Near(Simulate(settings, {10, -7, 0}, {}, 0.1f, 60).x, 0), "braking did not stop promptly");
        const auto reverse = settings.Apply({10, -7, 0}, {-10, 0, 0}, 10, true, 1.0f / 60);
        Require(reverse.x > 0 && reverse.x < 10, "reverse input changed direction instantly");
        Require(Near(Simulate(settings, {10, 0, 0}, {-10, 0, 0}, 0.5f, 60).x, -10), "reverse did not reach new maximum");
        for (int fps : {30, 60, 120, 144}) {
            const auto accelerating = Simulate(settings, {}, {10, 0, 0}, 0.15f, fps);
            const auto reversing = Simulate(settings, {10, 0, 0}, {-10, 0, 0}, 0.15f, fps);
            Require(Near(accelerating.x, 10.0f * 0.15f / 0.4f), "acceleration depends on frame rate");
            Require(Near(reversing.x, -10.0f * 0.05f / 0.4f), "reverse timing depends on frame rate");
        }
        const auto diagonal = Simulate(settings, {}, {10, 0, 10}, 1.0f, 60);
        Require(Near(std::hypot(diagonal.x, diagonal.z), 10), "diagonal movement exceeded maximum");
        Require(Near(Simulate(settings, {}, {2.5f, 0, 0}, 1.0f, 60).x, 2.5f), "analog input magnitude was lost");
        const auto air = settings.Apply({0, 9, 0}, {10, 0, 0}, 10, false, 1.0f / 60);
        Require(air.x > 0 && air.x < first.x && air.y == 9, "air control or vertical velocity changed");
        const auto paused = settings.Apply({3, -4, 5}, {-10, 0, 0}, 10, true, 0);
        Require(paused.x == 3 && paused.y == -4 && paused.z == 5, "pause changed momentum");
        const auto invalid = settings.Apply({3, -4, 5}, {}, 10, true, std::numeric_limits<float>::quiet_NaN());
        Require(invalid.x == 3 && invalid.z == 5, "invalid delta time changed momentum");
        settings.accelerationTime = settings.brakingTime = 0;
        Require(settings.Apply({}, {10, 0, 0}, 10, true, 1.0f / 60).x == 10, "zero-time setting did not restore instant movement");
        settings.accelerationTime = std::numeric_limits<float>::quiet_NaN(); settings.brakingTime = -1; settings.airControl = 9;
        settings.Normalize();
        Require(settings.accelerationTime == 0.4f && settings.brakingTime == 0 && settings.airControl == 1,
                "invalid editor/json settings not normalized");
        std::cout << "PASS: acceleration, braking, reversal, 30/60/120/144fps, diagonal limit, analog input, air, pause and settings\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}