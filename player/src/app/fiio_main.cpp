#include "FiioCli.h"

#include <memory>

int main(int argc, char **argv)
{
    return microdap::runCli(argc, argv, [] { return std::make_unique<microdap::UsbfsIo>(); }, microdap::fiioPathsFromEnv());
}
