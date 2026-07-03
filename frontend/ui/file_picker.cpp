#include "file_picker.hpp"

#include <nfd.h>

namespace prospero::gui {

bool init_file_dialog() {
    return NFD_Init() == NFD_OKAY;
}

void shutdown_file_dialog() {
    NFD_Quit();
}

}
