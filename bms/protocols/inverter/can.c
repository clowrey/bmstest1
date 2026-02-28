#include "config/allocations.h"
#include "config/pins.h"

#include "can2040.h"

static struct can2040 cbus;

static void PIOx_IRQHandler(void)
{
    can2040_pio_irq_handler(&cbus);
}

void init_inverter_can(can2040_rx_cb rx_cb) {
    const int can_bitrate = 500000; // 500 kbps

    can2040_setup(&cbus, CAN2040_PIO_NUM);
    can2040_callback_config(&cbus, rx_cb);

    // Enable irqs
    irq_set_exclusive_handler(CAN2040_PIO_IRQ0, PIOx_IRQHandler);
    irq_set_priority(CAN2040_PIO_IRQ0, 1);
    irq_set_enabled(CAN2040_PIO_IRQ0, 1);

    // Start canbus
    can2040_start(&cbus, SYS_CLK_HZ, can_bitrate, PIN_CAN_RX, PIN_CAN_TX);
}

int inverter_can_transmit(const struct can2040_msg *msg) {
    return can2040_transmit(&cbus, msg);
}
