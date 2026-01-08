#include "settings.h"

namespace Honey {

    static EngineSettings s_settings;

    EngineSettings& get_settings() { return s_settings; }

}