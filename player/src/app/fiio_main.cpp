#include "FiioCli.h"

#include <memory>

int main(int argc, char **argv)
{
    return casdap::runCli(argc, argv, [] { return std::make_unique<casdap::UsbfsIo>(); }, casdap::fiioPathsFromEnv());
}
