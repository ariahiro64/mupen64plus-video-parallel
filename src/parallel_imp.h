
#include "gfx_m64p.h"
#include "glguts.h"
#include "gfxstructdefs.h"


#ifdef __cplusplus
extern "C"
{
#endif

    extern int32_t vk_rescaling;
    extern bool vk_ssreadbacks;
    extern bool vk_ssdither;

    void vk_rasterize();
    void vk_process_commands();
    bool vk_init();
    void vk_destroy();

#ifdef __cplusplus
}
#endif
