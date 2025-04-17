#pragma once
#include "Honey.h"

#ifdef HN_PLATFORM_WINDOWS

   extern HONEY_API Honey::Application* Honey::create_application();

   int main(int argc, char** argv) {
       //std::cout << "Honey Engine started\n";

       Honey::Log::init();
       HN_CORE_WARN("Initialized core log!");

       int a = 69;
       HN_INFO("Helo: var= {0}", a);

       auto app = Honey::create_application();
       app->run();
       delete app;
       return 0;
   }


#endif
