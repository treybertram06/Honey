#include <Honey.h>
#include "Honey/entry_point.h"

class Sandbox : public Honey::Application
{
public:
        Sandbox()
        {

        }

        ~Sandbox()
        {

        }

};

Honey::Application* Honey::create_application()
{
        return new Sandbox();
}