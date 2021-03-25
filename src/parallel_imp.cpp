#include "parallel_imp.h"


using namespace Vulkan;
using namespace std;



static int cmd_cur;
static int cmd_ptr;
static uint32_t cmd_data[0x00040000 >> 2];

RDP::CommandProcessor* frontend = nullptr;
Device device;
Context context;

unsigned width, height;
unsigned overscan;
unsigned upscaling = 1;
unsigned downscaling_steps = 0;
bool native_texture_lod = false;
bool native_tex_rect = true;
bool synchronous = true, divot_filter = true, gamma_dither = true;
bool vi_aa = true, vi_scale = true, dither_filter = true;
bool interlacing = true, super_sampled_read_back = false, super_sampled_dither = true;
static const unsigned cmd_len_lut[64] = {
	1, 1, 1, 1, 1, 1, 1, 1, 4, 6, 12, 14, 12, 14, 20, 22,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
	1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,
};

std::vector<RDP::RGBA>cols;
void vk_rasterize()
{
if(frontend)
{
	frontend->set_vi_register(RDP::VIRegister::Control, *GET_GFX_INFO(VI_STATUS_REG));
	frontend->set_vi_register(RDP::VIRegister::Origin, *GET_GFX_INFO(VI_ORIGIN_REG));
	frontend->set_vi_register(RDP::VIRegister::Width, *GET_GFX_INFO(VI_WIDTH_REG));
	frontend->set_vi_register(RDP::VIRegister::Intr, *GET_GFX_INFO(VI_INTR_REG));
	frontend->set_vi_register(RDP::VIRegister::VCurrentLine, *GET_GFX_INFO(VI_V_CURRENT_LINE_REG));
	frontend->set_vi_register(RDP::VIRegister::Timing, *GET_GFX_INFO(VI_V_BURST_REG));
	frontend->set_vi_register(RDP::VIRegister::VSync, *GET_GFX_INFO(VI_V_SYNC_REG));
	frontend->set_vi_register(RDP::VIRegister::HSync, *GET_GFX_INFO(VI_H_SYNC_REG));
	frontend->set_vi_register(RDP::VIRegister::Leap, *GET_GFX_INFO(VI_LEAP_REG));
	frontend->set_vi_register(RDP::VIRegister::HStart, *GET_GFX_INFO(VI_H_START_REG));
	frontend->set_vi_register(RDP::VIRegister::VStart, *GET_GFX_INFO(VI_V_START_REG));
	frontend->set_vi_register(RDP::VIRegister::VBurst, *GET_GFX_INFO(VI_V_BURST_REG));
	frontend->set_vi_register(RDP::VIRegister::XScale, *GET_GFX_INFO(VI_X_SCALE_REG));
	frontend->set_vi_register(RDP::VIRegister::YScale, *GET_GFX_INFO(VI_Y_SCALE_REG));

	frontend->begin_frame_context();

	unsigned width =0;
    unsigned height = 0;
	frontend->scanout_sync(cols, width, height);

	
if(width == 0 || height == 0)
{
screen_swap(true);
return;
}

struct frame_buffer buf = { 0 };
buf.pixels = (video_pixel*)cols.data();
buf.valid = true;
buf.height = height;
buf.width = width;
buf.pitch = width;
screen_write(&buf);
screen_swap(false);
}

}

void vk_process_commands()
{
	const uint32_t DP_CURRENT = *GET_GFX_INFO(DPC_CURRENT_REG) & 0x00FFFFF8;
	const uint32_t DP_END = *GET_GFX_INFO(DPC_END_REG) & 0x00FFFFF8;
	// This works in parallel-n64, but not this repo for some reason.
	// Angrylion does not clear this bit here.
	//*GET_GFX_INFO(DPC_STATUS_REG) &= ~DP_STATUS_FREEZE;

	int length = DP_END - DP_CURRENT;
	if (length <= 0)
		return;

	length = unsigned(length) >> 3;
	if ((cmd_ptr + length) & ~(0x0003FFFF >> 3))
		return;

	uint32_t offset = DP_CURRENT;
	if (*GET_GFX_INFO(DPC_STATUS_REG) & DP_STATUS_XBUS_DMA)
	{
		do
		{
			offset &= 0xFF8;
			cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(SP_DMEM + offset);
			cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(SP_DMEM + offset + 4);
			offset += sizeof(uint64_t);
			cmd_ptr++;
		} while (--length > 0);
	}
	else
	{
		if (DP_END > 0x7ffffff || DP_CURRENT > 0x7ffffff)
		{
			return;
		}
		else
		{
			do
			{
				offset &= 0xFFFFF8;
				cmd_data[2 * cmd_ptr + 0] = *reinterpret_cast<const uint32_t *>(DRAM + offset);
				cmd_data[2 * cmd_ptr + 1] = *reinterpret_cast<const uint32_t *>(DRAM + offset + 4);
				offset += sizeof(uint64_t);
				cmd_ptr++;
			} while (--length > 0);
		}
	}

	while (cmd_cur - cmd_ptr < 0)
	{
		uint32_t w1 = cmd_data[2 * cmd_cur];
		uint32_t command = (w1 >> 24) & 63;
		int cmd_length = cmd_len_lut[command];

		if (cmd_ptr - cmd_cur - cmd_length < 0)
		{
			*GET_GFX_INFO(DPC_START_REG) = *GET_GFX_INFO(DPC_CURRENT_REG) = *GET_GFX_INFO(DPC_END_REG);
			return;
		}

		if (command >= 8 && frontend)
			frontend->enqueue_command(cmd_length * 2, &cmd_data[2 * cmd_cur]);

		if (RDP::Op(command) == RDP::Op::SyncFull)
		{
			// For synchronous RDP:
			if(frontend)frontend->wait_for_timeline(frontend->signal_timeline());
			*gfx.MI_INTR_REG |= DP_INTERRUPT;
			gfx.CheckInterrupts();
		}

		cmd_cur += cmd_length;
	}

	cmd_ptr = 0;
	cmd_cur = 0;
	*GET_GFX_INFO(DPC_START_REG) = *GET_GFX_INFO(DPC_CURRENT_REG) = *GET_GFX_INFO(DPC_END_REG);
}


void vk_destroy()
{
	screen_close();
	if(frontend)
	{
		delete frontend;
		frontend = nullptr;
	}
  
}

bool vk_init()
{
    screen_init();

	if(!::Vulkan::Context::init_loader(nullptr)) return false ;
     if(!context.init_instance_and_device(nullptr, 0, nullptr, 0, ::Vulkan::CONTEXT_CREATION_DISABLE_BINDLESS_BIT)) return false;

	uintptr_t aligned_rdram = reinterpret_cast<uintptr_t>(gfx.RDRAM);
	uintptr_t offset = 0;
	device.set_context(context);
    device.init_frame_contexts(1);
	::RDP::CommandProcessorFlags flags = 0;
	frontend = new RDP::CommandProcessor(device, reinterpret_cast<void *>(aligned_rdram),
				offset, rdram_size, rdram_size / 2, flags);
    if(!frontend->device_is_supported()) {
    delete frontend;
    frontend = nullptr;
	return false;
  }
  return true;
}

static const VkApplicationInfo parallel_app_info = {
	VK_STRUCTURE_TYPE_APPLICATION_INFO,
	nullptr,
	"paraLLEl-RDP",
	0,
	"Granite",
	0,
	VK_API_VERSION_1_1,
};

const VkApplicationInfo *parallel_get_application_info(void)
{
	return &parallel_app_info;
}

