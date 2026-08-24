#ifndef CAMERA_H
#define CAMERA_H

#include <string>

class Camera {
public:
    virtual ~Camera() = default;

    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    // Optional board-specific policy shown to the model before it requests a frame.
    virtual std::string GetCaptureInstructions() const { return {}; }
    // Optional fast gate for physical consent or capture-quality checks.
    virtual bool PrepareCapture(std::string& reason) {
        reason.clear();
        return true;
    }
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool SetSwapBytes(bool /*enabled*/) { return false; }  // Optional, default no-op
    virtual std::string Explain(const std::string& question) = 0;
};

#endif  // CAMERA_H
