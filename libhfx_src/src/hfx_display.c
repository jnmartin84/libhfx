#include <hfx.h>
#include <hfx_types.h>
#include <hfx_int.h>
#include <hfx_rb.h>
#include <hfx_rdp.h>
#include <stdio.h>
#include <stdbool.h>
#include <libdragon.h>

#define DISPLAY_TIMEOUT (1*1000*1000)
#define get_disp_buffer(x) __safe_buffer[(x)-1]

uint16_t hfx_depth_buffer[320*240*2] __attribute__((aligned(64)));

uint16_t *depth_buffer;

extern void *__safe_buffer[];

void *hfx_display_get_pointer(hfx_state *state)
{
    return state->display_ptr;
}

void hfx_get_display(hfx_state *state)
{
    display_context_t disp = 0;
    state->last_display = state->display;
    uint32_t count = 0;
    do
    {
        if(count >= DISPLAY_TIMEOUT)
        {
            break;
        }

        count += 1;
        disp = display_lock();
        hfx_wait_us(1);
    } while(disp == 0);

    if(count >= DISPLAY_TIMEOUT)
    {
        hfx_fatal_error(state, "timeout wait for display");
    }

    state->display = disp;
    state->display_ptr = (void*)(0x1ffffff & (uintptr_t)get_disp_buffer(state->display));
}

void hfx_cmd_register_display(hfx_state *state)
{
    depth_buffer = hfx_depth_buffer;
    uint64_t buffer_cmds[2];
    /* Get display handle */
    hfx_get_display(state);

    buffer_cmds[0] = HFX_RDP_PKT_SET_COLOR_IMAGE(HFX_RDP_CMD_SET_COLOR_IMAGE_FORMAT_RGBA, HFX_RDP_CMD_SET_COLOR_IMAGE_SIZE_16B, state->display_dim.width, hfx_display_get_pointer(state));
    buffer_cmds[1] = HFX_RDP_PKT_SET_Z_IMAGE(0x1ffffff&(uintptr_t)hfx_depth_buffer);
    hfx_cmd_rdp(state, sizeof(buffer_cmds)/sizeof(uint64_t), buffer_cmds);
}

void hfx_swap_buffers(hfx_state *state)
{
    // TODO if we want to support triple buffering
    // we wouldn't wait for idle here
    hfx_wait_for_idle(state);
#if 0
#define CONV_COLOR(r,g,b) ((uint16_t)(((((uint8_t)(r) >> 3)&0x1f) << 11) | ((((uint8_t)(g) >> 3)&0x1f) << 6) | ((((uint8_t)(b) >> 3)&0x1f)<<1) | 1))

	for(int i=0;i<320*240;i++) {
		uint16_t d = *(uint16_t*)(((uintptr_t)hfx_depth_buffer | 0xA0000000) + (i<<1));
    float dF = d/65532.0f;
	float zNear = 32.0f;    // TODO: Replace by the zNear of your perspective projection
    float zFar  = 10.1f * 1024.0f; // TODO: Replace by the zFar  of your perspective projection
	float mF = (2.0f * zNear) / (zFar + zNear - dF * (zFar - zNear));

	*(uint16_t *)(((uintptr_t)hfx_display_get_pointer(state) | 0xA0000000) + (i << 1)) =
	CONV_COLOR( mF*255, mF*255,mF*255);//mF*255, mF*255); 
	}
#endif
#if 0	
	__n64_memcpy_ASM((uintptr_t)hfx_display_get_pointer(state) | 0xA0000000, (uintptr_t)hfx_depth_buffer| 0xA0000000, 320*240*2);
#endif
    display_show(state->display);
    hfx_cmd_register_display(state); 
}
