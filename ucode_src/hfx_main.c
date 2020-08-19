#include <hfx_cmds.h>
#include <stdint.h>
#include <stdbool.h>

#define HFX_SET_XBUS_DMEM_DMA 0x0002
#define HFX_RDP_BUFFER_SIZE 64

#define HFX_READ_REG(reg) hfx_registers[(reg)>>2]
#define HFX_WRITE_REG(reg, value) hfx_registers[(reg)>>2] = (value)
// TODO fix masking here
#define HFX_READ_RB(offset) hfx_rb_buffer[((rb_start+((offset)<<2))&0x3FFF)>>2]
#define OFFSET_OF(addr, offset) (((uint8_t*)(addr))+(offset))

extern uint32_t volatile hfx_registers[HFX_REGISTER_SPACE_SIZE];
volatile uint32_t hfx_rb_buffer[256] __attribute__((aligned(8)));
volatile uint32_t hfx_rdb_buffer[HFX_RDP_BUFFER_SIZE] __attribute__((aligned(8)));
static uint32_t hfx_rb_end;
static uint32_t hfx_rdp_start, hfx_rdp_end;

void hfx_check_rb_ptr()
{
    uint32_t status;
    uint32_t rb_end = HFX_READ_REG(HFX_REG_RB_END);

    if(hfx_rb_end != rb_end)
    {
        /* Setup DMA transfer registers */
        // TODO only copy data we need to
        asm volatile ("mtc0 %0, $0\n"
                      "mtc0 %1, $1\n"
                      "mtc0 %2, $2"
                      :: "r"(hfx_rb_buffer),
                         "r"(HFX_READ_REG(HFX_REG_RB_ADDR)),
                         "r"(HFX_READ_REG(HFX_REG_RB_SIZE)-1));

        /* Wait for DMA to finish copying data */
        do
        {
            asm volatile ("mfc0 %0, $6" : "=r"(status));
        } while(status != 0);

        /* Update cached rb end value with updated value */
        hfx_rb_end = rb_end;
    }

    return;
}

void hfx_cmd_dma(bool write, uint32_t rb_start)
{
    uint32_t dma_dmem_addr = HFX_READ_RB(1);
    uint32_t dma_mem_addr = HFX_READ_RB(2);
    uint32_t dma_size = HFX_READ_RB(3);
    uint32_t status;

    /* Setup DMA transfer registers */
    asm volatile ("mtc0 %0, $0\n"
                  "mtc0 %1, $1"
                  :: "r"(dma_dmem_addr),
                     "r"(dma_mem_addr));
    if(write)
    {
        asm volatile ("mtc0 %0, $3" :: "r"(dma_size));
    }
    else
    {
        asm volatile ("mtc0 %0, $2" :: "r"(dma_size));
    }

    /* Wait for DMA to finish copying data */
    do
    {
        asm volatile ("mfc0 %0, $6" : "=r"(status));
    } while(status != 0);


    return;
}

void hfx_rdp_init()
{
    hfx_rdp_start = 0;
    hfx_rdp_end = 0;

    /* Set RDP to load using XBUS DMA (DMA from DMEM) */
    /* Set RDP start and end pointer to statically allocated buffer */
    asm volatile("mtc0 %0, $11\n"
                 "mtc0 %1, $8\n"
                 "mtc0 %2, $9"
                 :: "r"(HFX_SET_XBUS_DMEM_DMA),
                    "r"(hfx_rdb_buffer),
                    "r"(hfx_rdb_buffer));
}

uint32_t hfx_rdp_size()
{
    return hfx_rdp_end - hfx_rdp_start;
}

void hfx_rdp_reserve(uint32_t num_bytes)
{
    uint32_t rdp_size = hfx_rdp_size();
    if((rdp_size + num_bytes) >= (HFX_RDP_BUFFER_SIZE*sizeof(uint32_t)))
    {
        /* TODO wrap buffer */
    }
}

void hfx_rdp_queue(uint32_t cmd)
{
    hfx_rdb_buffer[hfx_rdp_end>>2] = cmd;
    hfx_rdp_end += sizeof(uint32_t);
}

void hfx_rdp_submit()
{
    asm volatile("mtc0 %0, $9"
                 :: "r"(OFFSET_OF(hfx_rdb_buffer, hfx_rdp_end)));

    hfx_rdp_start = hfx_rdp_end;
}

void hfx_cmd_set_display(uint32_t rb_start)
{
    uint32_t disp_addr = HFX_READ_RB(1);

    hfx_rdp_reserve(sizeof(uint32_t)*2);
    hfx_rdp_queue(0xFF10013F);
    hfx_rdp_queue(disp_addr);
    hfx_rdp_submit();
}

void hfx_cmd_set_rdp(uint32_t rb_start, uint32_t num_cmds)
{
    hfx_rdp_reserve(sizeof(uint32_t)*num_cmds);

    for(int i=0; i < num_cmds; i++)
    {
        hfx_rdp_queue(HFX_READ_RB(2+i));
    }

    hfx_rdp_submit();
}

int main()
{
    uint32_t num_rdp_cmds = 0;
    /* Set REG RB_END to zero */
    hfx_rb_end = 0;

    hfx_rdp_init();

    for(;;)
    { 
        hfx_check_rb_ptr();
        uint32_t rb_start = HFX_READ_REG(HFX_REG_RB_START);
        if(rb_start == hfx_rb_end)
            continue;

        uint32_t cmd = hfx_rb_buffer[rb_start>>2];
        switch(cmd & 0xFF)
        {
            case HFX_CMD_NOP:
                rb_start += 4;
                break;
            case HFX_CMD_INT:
                asm volatile ("break");
                rb_start += 4;
                break;
            case HFX_CMD_DMA:
                hfx_cmd_dma(cmd>>8, rb_start);
                rb_start += 16;
                break;
            case HFX_CMD_SET_DISPLAY:
                hfx_cmd_set_display(rb_start);
                rb_start += 8;
            case HFX_CMD_SEND_RDP:
                num_rdp_cmds = HFX_READ_RB(1);
                hfx_cmd_set_rdp(rb_start, num_rdp_cmds);
                rb_start += 8 + num_rdp_cmds;
                break;
        }

        HFX_WRITE_REG(HFX_REG_RB_START, rb_start & 0x3FFF);
    }

    return 0;
}