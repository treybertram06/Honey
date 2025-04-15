#include <iostream>
#include <Honey.h>
#include <Honey/entry_point.h>



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



