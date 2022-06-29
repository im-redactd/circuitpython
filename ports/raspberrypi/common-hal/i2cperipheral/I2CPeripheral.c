/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Noralf Trønnes
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* TEMP kds notes:
   next level down I2C functionality appears to come from:
   ports/raspberrypi/sdk/src/rp2_common/hardware_i2c/i2c.c
 */

#include "shared-bindings/i2cperipheral/I2CPeripheral.h"

#include "common-hal/busio/I2C.h"

#include "py/mperrno.h"
#include "py/mphal.h"
#include "shared-bindings/busio/I2C.h"
#include "py/runtime.h"

#include "src/rp2_common/hardware_gpio/include/hardware/gpio.h"

//#include "hardware/clocks.h"
//#include "pico/timeout_helper.h"

STATIC i2c_inst_t *i2c[2] = {i2c0, i2c1};

#define NO_PIN 0xff

// One second
#define BUS_TIMEOUT_USEC 1000000


void common_hal_i2cperipheral_i2c_peripheral_construct(
    i2cperipheral_i2c_peripheral_obj_t *self,
    const mcu_pin_obj_t *scl,
    const mcu_pin_obj_t *sda,
    uint8_t *addresses,
    unsigned int num_addresses,
    bool smbus
){

    mp_printf(&mp_plat_print, "i2cperipheral - you're here: construct.\n");
    mp_printf(&mp_plat_print, "i2cperipheral - addresses: %p.\n", *addresses);
    mp_printf(&mp_plat_print, "i2cperipheral - num_addresses: %d.\n", num_addresses);
    mp_printf(&mp_plat_print, "i2cperipheral - sda: %p, scl: %p.\n", sda->number,scl->number);

    // I2C pins have a regular pattern. SCL is always odd and SDA is even. They match up in pairs
    // so we can divide by two to get the instance. This pattern repeats.
    size_t scl_instance = (scl->number / 2) % 2;
    size_t sda_instance = (sda->number / 2) % 2;
    if (scl->number % 2 == 1 && sda->number % 2 == 0 && scl_instance == sda_instance) {
        self->peripheral = i2c[sda_instance];
    }

    if (self->peripheral == NULL) {
        mp_raise_ValueError(translate("Invalid pins"));
    }
    if ((i2c_get_hw(self->peripheral)->enable & I2C_IC_ENABLE_ENABLE_BITS) != 0) {
        mp_raise_ValueError(translate("I2C peripheral in use"));
    }

    // kds: copy things into our tracking instanmce.
    // self->base = ?? ;
    self->addresses = addresses;
    self->num_addresses = num_addresses;
    self->scl_pin = scl->number;
    self->sda_pin = sda->number;
    // self->writing = ?? ;

    const uint32_t frequency = 400000;
    i2c_init(self->peripheral, frequency);

    gpio_set_function(sda->number, GPIO_FUNC_I2C);
    gpio_set_function(scl->number, GPIO_FUNC_I2C);

    gpio_set_pulls(sda->number, true, 0);
    gpio_set_pulls(scl->number, true, 0);

    i2c_set_slave_mode(self->peripheral, true, *addresses);

    return;
}


bool common_hal_i2cperipheral_i2c_peripheral_deinited(i2cperipheral_i2c_peripheral_obj_t *self) {

    mp_printf(&mp_plat_print, "i2cperipheral - you're here: deinited; returning false unless 0xff = [%p]\n", self->sda_pin);
    return self->sda_pin == NO_PIN;
}

void common_hal_i2cperipheral_i2c_peripheral_deinit(i2cperipheral_i2c_peripheral_obj_t *self) {

    if (common_hal_i2cperipheral_i2c_peripheral_deinited(self)) {
        return;
    }

    i2c_deinit(self->peripheral);
    mp_printf(&mp_plat_print, "i2cperipheral - you're here: deinit.\n");
    return;
}

static int i2c_peripheral_check_error(i2cperipheral_i2c_peripheral_obj_t *self, bool raise) {

    mp_printf(&mp_plat_print, "i2cperipheral - you're here: checking for error. [%b]\n",raise);

    return 0;
}

int common_hal_i2cperipheral_i2c_peripheral_is_addressed(
    i2cperipheral_i2c_peripheral_obj_t *self,
    uint8_t *address,
    bool *is_read,
    bool *is_restart)
{
    mp_printf(&mp_plat_print, "i2cperipheral - you're here: is addressed [num address: %d].\n", self->num_addresses);

    int err = i2c_peripheral_check_error(self, false);
    if (err) {
        mp_printf(&mp_plat_print, "i2cperipheral - you're here: error in is_addressed: %p.\n", err);
        return err;
    }

    //!!@@ *address = ...;
    *address = self->peripheral->hw->sar;  // kds: best guess again...

    *is_read = ((self->peripheral->hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_RD_REQ_BITS) !=0);
    self->writing = ! *is_read;

    // kds: best guess...
    *is_restart = ((self->peripheral->hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_RD_REQ_RESET) !=0);

    for (unsigned int i = 0; i < self->num_addresses; i++) {
        if (*address == self->addresses[i]) {
            mp_printf(&mp_plat_print, "i2cperipheral - is_addressed matched on address %p; is_read:%b, is_restart:%b; sending ack\n", *address, *is_read, *is_restart);
            common_hal_i2cperipheral_i2c_peripheral_ack(self, true);
            return 1;
        }
    }

    // This should clear AMATCH, but it doesn't...
    common_hal_i2cperipheral_i2c_peripheral_ack(self, false);

    mp_printf(&mp_plat_print, "i2cperipheral - is_addressed no matches; sending nak.");
    return 0;
}

int common_hal_i2cperipheral_i2c_peripheral_read_byte(
    i2cperipheral_i2c_peripheral_obj_t *self,
    uint8_t *data)
{
    mp_printf(&mp_plat_print, "i2cperipheral - you're here: read byte\n");

    uint8_t buffer;
    int rslt = i2c_read_blocking(self->peripheral, self->addresses[0], &buffer, 1, true);
    mp_printf(&mp_plat_print, "i2c_read_blocking returned %d", rslt);

    // That returned byte-count, so anything but 1 is bad.
    return (rslt == 1) ? 0 : 1;
}

int common_hal_i2cperipheral_i2c_peripheral_write_byte(i2cperipheral_i2c_peripheral_obj_t *self, uint8_t data)
{
    mp_printf(&mp_plat_print, "i2cperipheral - you're here: write byte\n");

    int rslt = i2c_write_blocking(self->peripheral, self->addresses[0], &data, 1, true);
    mp_printf(&mp_plat_print, "i2c_write_blocking returned %d", rslt);

    // That returned byte-count, so anything but 1 is bad.
    return (rslt == 1) ? 0 : 1;
}

void common_hal_i2cperipheral_i2c_peripheral_ack(i2cperipheral_i2c_peripheral_obj_t *self, bool ack) {
    mp_printf(&mp_plat_print, "i2cperipheral - you're here: ack: %b.\n", ack);
    return;
}

void common_hal_i2cperipheral_i2c_peripheral_close(i2cperipheral_i2c_peripheral_obj_t *self) {
    mp_printf(&mp_plat_print, "i2cperipheral - you're here: peripheral close.\n");
    return;
}
