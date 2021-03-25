#include "vulkan_headers.hpp"
#include "gfx_m64p.h"
#include "glguts.h"
#include "gfxstructdefs.h"
#include <memory>
#include <vector>

#include "rdp_device.hpp"
#include "context.hpp"
#include "device.hpp"

#ifdef __cplusplus
extern "C" {
#endif

    void vk_rasterize();
    void vk_process_commands();
bool vk_init();
void vk_destroy();

#ifdef __cplusplus
}
#endif

