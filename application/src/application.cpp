#include <iostream>
#include <Honey.h>



class Application : public Honey::Engine {
public:

       Application() {
       }
       ~Application() {
       }
};

Honey::Engine* Honey::create_application() {
    return new Application();
}


