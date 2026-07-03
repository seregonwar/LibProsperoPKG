#include "app_state.hpp"

namespace prospero::gui {

AppState& get_app_state() {
    static AppState instance;
    return instance;
}

}
