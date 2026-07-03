#pragma once

struct GLFWwindow;

namespace prospero::gui {

class Window {
public:
    Window();
    ~Window();
    
    bool init(int width, int height, const char* title);
    void shutdown();
    
    bool should_close() const;
    void poll_events();
    void begin_frame();
    void end_frame();
    
    GLFWwindow* get_glfw_window() const { return window_; }
    float get_dpi_scale() const { return dpi_scale_; }
    
private:
    GLFWwindow* window_ = nullptr;
    float dpi_scale_ = 1.0f;
};

}
