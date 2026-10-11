#pragma once

#include "Fiio.h"

#include <functional>
#include <memory>

namespace casdap {

using UsbIoFactory = std::function<std::unique_ptr<UsbIo>()>;

int runCli(int argc, char **argv, UsbIoFactory factory, const FiioPaths &paths);

}
