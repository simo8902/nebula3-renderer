// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_FRAME_GATE_H
#define NDEVC_FRAME_GATE_H

#include <condition_variable>
#include <mutex>

// ── FrameGate ─────────────────────────────────────────────────────────────────
// Single-producer (main thread) / single-consumer (render thread) synchronization
// for the two-barrier pipeline:
//
//   Main:   PostFrame → WaitDrawlistsDone ──── [Tick N+1] ──── WaitFrameDone → PostFrame...
//   Render: WaitFrame → PrepareDrawLists → SignalDrawlistsDone → GL+Swap → SignalFrameDone
//
// States:
//   Idle            — render thread is waiting for work
//   FrameReady      — main posted a frame; render thread waking up
//   DrawlistsDone   — render finished PrepareDrawLists; main can start next Tick
//   FrameDone       — render finished SwapBuffers; main can post next frame
class FrameGate {
public:
    void PostFrame() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return state_ == State::Idle || stop_; });
        if (stop_) return;
        state_ = State::FrameReady;
        cv_.notify_all();
    }

    // Returns false when stop is requested.
    bool WaitFrame() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return state_ == State::FrameReady || stop_; });
        return !stop_;
    }

    void SignalDrawlistsDone() {
        std::unique_lock<std::mutex> lock(mtx_);
        state_ = State::DrawlistsDone;
        cv_.notify_all();
    }

    void WaitDrawlistsDone() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return state_ == State::DrawlistsDone || stop_; });
    }

    void SignalFrameDone() {
        std::unique_lock<std::mutex> lock(mtx_);
        state_ = State::Idle;
        cv_.notify_all();
    }

    void WaitFrameDone() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return state_ == State::Idle || stop_; });
    }

    void Stop() {
        std::unique_lock<std::mutex> lock(mtx_);
        stop_ = true;
        cv_.notify_all();
    }

    bool IsStopped() const {
        std::unique_lock<std::mutex> lock(mtx_);
        return stop_;
    }

private:
    enum class State { Idle, FrameReady, DrawlistsDone };
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    State state_ = State::Idle;
    bool  stop_  = false;
};

#endif // NDEVC_FRAME_GATE_H
