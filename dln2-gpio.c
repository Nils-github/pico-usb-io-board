// SPDX-License-Identifier: CC0-1.0
/*
 * Written in 2021 by Noralf Trønnes <noralf@tronnes.org>
 *
 * To the extent possible under law, the author(s) have dedicated all copyright and related and
 * neighboring rights to this software to the public domain worldwide. This software is
 * distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along with this software.
 * If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "dln2.h"

#define LOG1    //printf
#define LOG2    //printf

#define DLN2_GPIO_CMD(cmd)       DLN2_CMD(cmd, DLN2_MODULE_GPIO)

#define DLN2_GPIO_GET_PIN_COUNT         DLN2_GPIO_CMD(0x01)
#define DLN2_GPIO_SET_DEBOUNCE          DLN2_GPIO_CMD(0x04)
#define DLN2_GPIO_PIN_GET_VAL           DLN2_GPIO_CMD(0x0B)
#define DLN2_GPIO_PIN_SET_OUT_VAL       DLN2_GPIO_CMD(0x0C)
#define DLN2_GPIO_PIN_GET_OUT_VAL       DLN2_GPIO_CMD(0x0D)
#define DLN2_GPIO_CONDITION_MET_EV      DLN2_GPIO_CMD(0x0F)
#define DLN2_GPIO_PIN_ENABLE            DLN2_GPIO_CMD(0x10)
#define DLN2_GPIO_PIN_DISABLE           DLN2_GPIO_CMD(0x11)
#define DLN2_GPIO_PIN_SET_DIRECTION     DLN2_GPIO_CMD(0x13)
#define DLN2_GPIO_PIN_GET_DIRECTION     DLN2_GPIO_CMD(0x14)
#define DLN2_GPIO_PIN_OPENDRAIN_ENABLE  DLN2_GPIO_CMD(0x15)
#define DLN2_GPIO_PIN_OPENDRAIN_DISABLE DLN2_GPIO_CMD(0x16)
#define DLN2_GPIO_PIN_PULLUP_ENABLE     DLN2_GPIO_CMD(0x18)
#define DLN2_GPIO_PIN_PULLUP_DISABLE    DLN2_GPIO_CMD(0x19)
#define DLN2_GPIO_PIN_DEBOUNCE_ENABLE   DLN2_GPIO_CMD(0x1B)
#define DLN2_GPIO_PIN_DEBOUNCE_DISABLE  DLN2_GPIO_CMD(0x1C)
#define DLN2_GPIO_PIN_SET_EVENT_CFG     DLN2_GPIO_CMD(0x1E)
#define DLN2_GPIO_PIN_PULLDOWN_ENABLE   DLN2_GPIO_CMD(0x20)
#define DLN2_GPIO_PIN_PULLDOWN_DISABLE  DLN2_GPIO_CMD(0x21)

#define DLN2_GPIO_EVENT_NONE            0
#define DLN2_GPIO_EVENT_CHANGE          1
#define DLN2_GPIO_EVENT_LVL_HIGH        2
#define DLN2_GPIO_EVENT_LVL_LOW         3
#define DLN2_GPIO_EVENT_CHANGE_RISING	0x11
#define DLN2_GPIO_EVENT_CHANGE_FALLING  0x21

#define DLN2_GPIO_NUM_PINS  29

#ifdef PICO_DEFAULT_LED_PIN
  #define LED_PIN   PICO_DEFAULT_LED_PIN
#else
  #define LED_PIN   0xff    // out of bounds value that will never match
#endif

struct dln2_gpio_event {
    uint8_t gpio;
    uint8_t events;
    uint8_t value;
};

struct dln2_gpio_settings {
    uint8_t gpio;
    uint16_t flags;
/* flag symbols are bit numbers */
#define FLAG_IS_OUTPUT         1
#define FLAG_OPEN_DRAIN        2
#define FLAG_PULL_UP           3
#define FLAG_PULL_DOWN         4
#define FLAG_USED_AS_IRQ       5
#define FLAG_DEBOUNCE_EN       6
    uint8_t irq_type;
    uint32_t debounce_us;
    absolute_time_t debounced_time;
};

struct dln2_gpio_settings settings[DLN2_GPIO_NUM_PINS];

#define DLN2_GPIO_MAX_EVENTS    32
static struct dln2_gpio_event dln2_gpio_events[DLN2_GPIO_MAX_EVENTS];
uint16_t dln2_gpio_event_count;

#define BIT_SET(flags, flagnr) flags |= (1 << flagnr)
#define BIT_CLEAR(flags, flagnr) flags &= (~(1 << flagnr))
#define BIT_CHECK(flags, flagnr) ((flags >> flagnr) & 0x01)

static const char *dln2_gpio_id_to_name(uint16_t id)
{
    switch (id) {
    case DLN2_GPIO_GET_PIN_COUNT:
        return "GPIO_GET_PIN_COUNT";
    case DLN2_GPIO_SET_DEBOUNCE:
        return "GPIO_SET_DEBOUNCE";
    case DLN2_GPIO_PIN_GET_VAL:
        return "GPIO_PIN_GET_VAL";
    case DLN2_GPIO_PIN_SET_OUT_VAL:
        return "GPIO_PIN_SET_OUT_VAL";
    case DLN2_GPIO_PIN_GET_OUT_VAL:
        return "GPIO_PIN_GET_OUT_VAL";
    case DLN2_GPIO_CONDITION_MET_EV:
        return "GPIO_CONDITION_MET_EV";
    case DLN2_GPIO_PIN_ENABLE:
        return "GPIO_PIN_ENABLE";
    case DLN2_GPIO_PIN_DISABLE:
        return "GPIO_PIN_DISABLE";
    case DLN2_GPIO_PIN_SET_DIRECTION:
        return "GPIO_PIN_SET_DIRECTION";
    case DLN2_GPIO_PIN_GET_DIRECTION:
        return "GPIO_PIN_GET_DIRECTION";
    case DLN2_GPIO_PIN_OPENDRAIN_ENABLE:
        return "GPIO_PIN_OPENDRAIN_ENABLE";
    case DLN2_GPIO_PIN_OPENDRAIN_DISABLE:
        return "GPIO_PIN_OPENDRAIN_DISABLE";
    case DLN2_GPIO_PIN_PULLUP_ENABLE:
        return "GPIO_PIN_PULLUP_ENABLE";
    case DLN2_GPIO_PIN_PULLUP_DISABLE:
        return "GPIO_PIN_PULLUP_DISABLE";
    case DLN2_GPIO_PIN_DEBOUNCE_ENABLE:
        return "GPIO_PIN_DEBOUNCE_ENABLE";
    case DLN2_GPIO_PIN_DEBOUNCE_DISABLE:
        return "GPIO_PIN_DEBOUNCE_DISABLE";
    case DLN2_GPIO_PIN_SET_EVENT_CFG:
        return "GPIO_PIN_SET_EVENT_CFG";
    case DLN2_GPIO_PIN_PULLDOWN_ENABLE:
        return "GPIO_PIN_PULLDOWN_ENABLE";
    case DLN2_GPIO_PIN_PULLDOWN_DISABLE:
        return "GPIO_PIN_PULLDOWN_DISABLE";
    }
    return NULL;
}

static int dln2_gpio_slot_pin_val(struct dln2_slot *slot, uint8_t *val)
{
    size_t len = dln2_slot_header_data_size(slot);
    if ((!val && len != 2) || (val && len != 3))
        return -1;

    void *data = dln2_slot_header_data(slot);
    uint16_t *pin = data;
    if (*pin > (DLN2_GPIO_NUM_PINS - 1))
        return -1;

    if (val)
        *val = *(uint8_t *)(data + 2);

    const struct dln2_header *hdr = dln2_slot_header(slot);
    const char *name = dln2_gpio_id_to_name(hdr->id);
    if (name)
        LOG1("\n%s: ", name);
    else
        LOG1("DLN_GPIO UNKNOWN 0x%02x: ", hdr->id);
    LOG1("pin=%u val=%d\n", *pin, val ? *val : -1);

    return *pin;
}

#define DLN2_GPIO_GET_PIN_VERIFY(_slot, _pin, _val)                     \
    (_pin) = dln2_gpio_slot_pin_val((_slot), (_val));                   \
    if ((_pin) < 0 || !dln2_pin_is_requested((_pin), DLN2_MODULE_GPIO))     \
        return dln2_response_error((_slot), DLN2_RES_INVALID_PIN_NUMBER)

static bool dln2_gpio_response_pin_val(struct dln2_slot *slot, uint16_t pin, uint8_t *val)
{
    uint8_t *data = dln2_slot_response_data(slot);
    put_unaligned_le16(pin, data);
    if (val)
        data[2] = *val;

    LOG1("GPIO RSP: pin=%u val=%d\n", pin, val ? *val : -1);

    return dln2_response(slot, val ? 3 : 2);
}

static bool dln2_gpio_pin_enable(struct dln2_slot *slot, bool enable)
{
    int pin = dln2_gpio_slot_pin_val(slot, NULL);
    if (pin < 0)
        return dln2_response_error(slot, DLN2_RES_INVALID_PIN_NUMBER);

    LOG1("%s: pin=%u enable=%u\n", __func__, pin, enable);
    if (enable) {
        int res = dln2_pin_request(pin, DLN2_MODULE_GPIO);
        if (res)
            return dln2_response_error(slot, res);

        gpio_function_t fn = gpio_get_function(pin);
        LOG1("    gpio_get_function=%u\n", fn);

        if (pin != LED_PIN) {
            gpio_init(pin);
            gpio_disable_pulls(pin); // default to no-pull
        }
    } else {
        int res = dln2_pin_free(pin, DLN2_MODULE_GPIO);
        if (res)
            return dln2_response_error(slot, res);
        if (pin != LED_PIN)
            gpio_deinit(pin);
    }
    return dln2_response(slot, 0);
}

static bool dln2_gpio_pin_set_debounce_cfg(struct dln2_slot *slot)
{
    struct {
        uint32_t duration;
        uint16_t pin;
    } TU_ATTR_PACKED *cmd = dln2_slot_header_data(slot);

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*cmd));

    if (!dln2_pin_is_requested(cmd->pin, DLN2_MODULE_GPIO))
        return dln2_response_error(slot, DLN2_RES_INVALID_PIN_NUMBER);

    if (cmd->pin == LED_PIN)
        return dln2_response_error(slot, DLN2_RES_INVALID_VALUE);

    BIT_SET(settings[cmd->pin].flags, FLAG_DEBOUNCE_EN);
    settings[cmd->pin].debounce_us = cmd->duration;

    return dln2_response(slot, 0);
}

static bool dln2_gpio_pin_set_event_cfg(struct dln2_slot *slot)
{
    struct {
        uint16_t pin;
        uint8_t type;
        uint16_t period;
    } TU_ATTR_PACKED *cmd = dln2_slot_header_data(slot);

    DLN2_VERIFY_COMMAND_SIZE(slot, sizeof(*cmd));

    LOG1("\nDLN2_GPIO_PIN_SET_EVENT_CFG: pin=%u type=%u period=%u\n", cmd->pin, cmd->type, cmd->period);

    if (!dln2_pin_is_requested(cmd->pin, DLN2_MODULE_GPIO))
        return dln2_response_error(slot, DLN2_RES_INVALID_PIN_NUMBER);

    if (cmd->period)
        return dln2_response_error(slot, DLN2_RES_INVALID_EVENT_PERIOD);

    if (cmd->pin == LED_PIN)
        return dln2_response_error(slot, DLN2_RES_INVALID_VALUE);

    switch (cmd->type) {
    case DLN2_GPIO_EVENT_NONE:
        gpio_set_irq_enabled(cmd->pin, GPIO_IRQ_LEVEL_LOW | GPIO_IRQ_LEVEL_HIGH | GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);
        settings[cmd->pin].irq_type = 0;
        break;
    // The Linux driver always uses this so we don't know which edge(s) it actually cares about.
    case DLN2_GPIO_EVENT_CHANGE:
        gpio_set_irq_enabled(cmd->pin, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
        settings[cmd->pin].irq_type = GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE;
        break;
    // The Linux driver doesn't use these, maybe because they were mistaken to be only level
    // interrupts, but with period=0 they are actually edge interrupts according to the docs:
    // http://dlnware.com/dll/DLN_GPIO_EVENT_LEVEL_HIGH-Events
    case DLN2_GPIO_EVENT_LVL_HIGH:
        gpio_set_irq_enabled(cmd->pin, GPIO_IRQ_LEVEL_HIGH, true);
        settings[cmd->pin].irq_type = GPIO_IRQ_LEVEL_HIGH;
        break;
    case DLN2_GPIO_EVENT_LVL_LOW:
        gpio_set_irq_enabled(cmd->pin, GPIO_IRQ_LEVEL_LOW, true);
        settings[cmd->pin].irq_type = GPIO_IRQ_LEVEL_LOW;
        break;
    case DLN2_GPIO_EVENT_CHANGE_RISING:
        gpio_set_irq_enabled(cmd->pin, GPIO_IRQ_EDGE_RISE, true);
        settings[cmd->pin].irq_type = GPIO_IRQ_EDGE_RISE;
        break;
    case DLN2_GPIO_EVENT_CHANGE_FALLING:
        gpio_set_irq_enabled(cmd->pin, GPIO_IRQ_EDGE_FALL, true);
        settings[cmd->pin].irq_type = GPIO_IRQ_EDGE_FALL;
        break;
    default:
        return dln2_response_error(slot, DLN2_RES_INVALID_EVENT_TYPE);
    }

    return dln2_response(slot, 0);
}

bool dln2_handle_gpio(struct dln2_slot *slot)
{
    struct dln2_header *hdr = dln2_slot_header(slot);
    uint8_t val;
    int pin;

    switch (hdr->id) {
    case DLN2_GPIO_GET_PIN_COUNT:
        LOG1("DLN2_GPIO_GET_PIN_COUNT\n");
        if (dln2_slot_header_data_size(slot))
            return dln2_response_error(slot, DLN2_RES_INVALID_COMMAND_SIZE);
        return dln2_response_u16(slot, DLN2_GPIO_NUM_PINS);
    case DLN2_GPIO_SET_DEBOUNCE:
        // The Linux driver can set the default debounce value, but it does not enable it for the pin?!
        // The DLN-2 adapter does not support debounce, but 4M and 4S do.
        LOG1("DLN2_GPIO_SET_DEBOUNCE\n");
        return dln2_gpio_pin_set_debounce_cfg(slot);
    case DLN2_GPIO_PIN_GET_VAL:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        val = gpio_get(pin);
        return dln2_gpio_response_pin_val(slot, pin, &val);
    case DLN2_GPIO_PIN_SET_OUT_VAL:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, &val);
        if (BIT_CHECK(settings[pin].flags, FLAG_OPEN_DRAIN)) {
            if (val == 0) {  // prevent glitch while switching
                gpio_put(pin, val);
            }
            gpio_set_oeover(pin, 0x3 - val);
        }
        gpio_put(pin, val);
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_GET_OUT_VAL:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        val = gpio_get_out_level(pin);
        return dln2_gpio_response_pin_val(slot, pin, &val);
    case DLN2_GPIO_PIN_ENABLE:
        return dln2_gpio_pin_enable(slot, true);
    case DLN2_GPIO_PIN_DISABLE:
        return dln2_gpio_pin_enable(slot, false);
    case DLN2_GPIO_PIN_SET_DIRECTION:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, &val);
        if (pin == LED_PIN && !val)
            return dln2_response_error(slot, DLN2_RES_INVALID_VALUE);
        if (val == 0) {
            BIT_CLEAR(settings[pin].flags, FLAG_IS_OUTPUT);
            BIT_CLEAR(settings[pin].flags, FLAG_PULL_UP);
            BIT_CLEAR(settings[pin].flags, FLAG_PULL_DOWN);
        } else if (val == 1) {
            BIT_SET(settings[pin].flags, FLAG_IS_OUTPUT);
            BIT_SET(settings[pin].flags, FLAG_OPEN_DRAIN);
            gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
        }
        gpio_set_dir(pin, val);
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_GET_DIRECTION:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        val = gpio_get_dir(pin);
        return dln2_gpio_response_pin_val(slot, pin, &val);
    case DLN2_GPIO_PIN_OPENDRAIN_ENABLE:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        BIT_SET(settings[pin].flags, FLAG_OPEN_DRAIN);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
        gpio_set_oeover(pin, 0x2);
        gpio_put(pin, true); // set default high == disabled
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_OPENDRAIN_DISABLE:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        BIT_CLEAR(settings[pin].flags, FLAG_OPEN_DRAIN);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
        gpio_set_oeover(pin, 0x3);
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_PULLUP_ENABLE:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        BIT_SET(settings[pin].flags, FLAG_PULL_UP);
        gpio_set_pulls(pin, true, BIT_CHECK(settings[pin].flags, FLAG_PULL_DOWN));
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_PULLUP_DISABLE:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        BIT_CLEAR(settings[pin].flags, FLAG_PULL_UP);
        gpio_set_pulls(pin, false, BIT_CHECK(settings[pin].flags, FLAG_PULL_DOWN));
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_DEBOUNCE_ENABLE:
        if (!gpio_is_dir_out(pin)) {
            BIT_SET(settings[pin].flags, FLAG_DEBOUNCE_EN);
        }
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_DEBOUNCE_DISABLE:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        if (!gpio_is_dir_out(pin)) {
            BIT_CLEAR(settings[pin].flags, FLAG_DEBOUNCE_EN);
            settings[pin].debounce_us = 0;
        }
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_SET_EVENT_CFG:
        return dln2_gpio_pin_set_event_cfg(slot);
    case DLN2_GPIO_PIN_PULLDOWN_ENABLE:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        BIT_SET(settings[pin].flags, FLAG_PULL_DOWN);
        gpio_set_pulls(pin, BIT_CHECK(settings[pin].flags, FLAG_PULL_UP), true);
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    case DLN2_GPIO_PIN_PULLDOWN_DISABLE:
        DLN2_GPIO_GET_PIN_VERIFY(slot, pin, NULL);
        BIT_CLEAR(settings[pin].flags, FLAG_PULL_DOWN);
        gpio_set_pulls(pin, BIT_CHECK(settings[pin].flags, FLAG_PULL_UP), false);
        return dln2_gpio_response_pin_val(slot, pin, NULL);
    default:
        LOG1("GPIO command not supported: 0x%04x\n", hdr->id);
        return dln2_response_error(slot, DLN2_RES_COMMAND_NOT_SUPPORTED);
    }
}

static bool dln2_gpio_queue_event(struct dln2_gpio_event *event)
{
    struct {
        uint16_t count;
        uint8_t type;
        uint16_t pin;
        uint8_t value;
    } TU_ATTR_PACKED *ev;

    LOG1("%s(gpio=%u, value=%u)\n", __func__, event->gpio, event->value);

    struct dln2_slot *slot = dln2_get_slot();
    if (!slot) {
        LOG1("Run out of slots!\n");
        LOG2("-\n");
        return false;
    }

    struct dln2_header *hdr = dln2_slot_header(slot);
    hdr->size = sizeof(*hdr) + sizeof(*ev);
    hdr->id = DLN2_GPIO_CONDITION_MET_EV;
    hdr->echo = 0;
    hdr->handle = DLN2_HANDLE_EVENT;

    ev = dln2_slot_header_data(slot);
    // The Linux driver ignores count and type
    ev->count = dln2_gpio_event_count;
    ev->type = 0;
    ev->pin = event->gpio;
    ev->value = event->value;

    dln2_print_slot(slot);
    dln2_queue_slot_in(slot);

    return true;
}

void dln2_gpio_task(void)
{
    bool queued;
    int i;

    do {
        queued = false;
        uint32_t ints = save_and_disable_interrupts();

        if (dln2_gpio_events[0].events) {
            if (dln2_gpio_queue_event(&dln2_gpio_events[0])) {
                for (i = 0; i < (DLN2_GPIO_MAX_EVENTS - 1); i++) {
                    dln2_gpio_events[i] = dln2_gpio_events[i + 1];
                }
                dln2_gpio_events[DLN2_GPIO_MAX_EVENTS - 1].events = 0;
                queued = true;
            }
        }
        restore_interrupts(ints);
    } while (queued);

    for (i = 0; i < DLN2_GPIO_NUM_PINS; i++) {
        if ((settings[i].debounced_time) && (settings[i].debounced_time < get_absolute_time())) {
            settings[i].debounced_time = 0;
            gpio_set_irq_enabled(i, settings[i].irq_type, true);
        }
    }
}

static void dln2_gpio_irq_callback(uint gpio, uint32_t events)
{
    if (gpio >= DLN2_GPIO_NUM_PINS)
        return;

    bool value;

    if (events == GPIO_IRQ_EDGE_FALL)
        value = 0;
    else if (events == GPIO_IRQ_EDGE_RISE)
        value = 1;
    else if (events == GPIO_IRQ_LEVEL_LOW)
        value = 0;
    else if (events == GPIO_IRQ_LEVEL_HIGH)
        value = 1;
    else
        return;

    if (events == GPIO_IRQ_EDGE_FALL)
        LOG2("F\n");
    else if (events == GPIO_IRQ_EDGE_RISE)
        LOG2("R\n");
    else if (events == GPIO_IRQ_LEVEL_LOW)
        LOG2("L\n");
    else if (events == GPIO_IRQ_LEVEL_HIGH)
        LOG2("H\n");
    else {
        LOG2("N\n");
        return;
    }

    LOG1("%s: gpio=%u events=0x%x value=%u\n", __func__, gpio, events, value);

    dln2_gpio_event_count++;

    uint16_t i;
    for (i = 0; i < DLN2_GPIO_MAX_EVENTS; i++) {
        if (!dln2_gpio_events[i].events)
            break;
    }

    if (i == DLN2_GPIO_MAX_EVENTS) {
        LOG1("dln2_gpio_events is FULL\n");
        return;
    }

    dln2_gpio_events[i].gpio = gpio;
    dln2_gpio_events[i].events = events;
    dln2_gpio_events[i].value = value;
    LOG2("%u\n", value);

    if ((BIT_CHECK(settings[gpio].flags, FLAG_DEBOUNCE_EN)) || (events == GPIO_IRQ_LEVEL_LOW) || (events == GPIO_IRQ_LEVEL_HIGH)) {
        if (((events == GPIO_IRQ_LEVEL_LOW) || (events == GPIO_IRQ_LEVEL_HIGH)) && (settings[gpio].debounce_us < 500)) {
            /* add minimum delay to prevent event storm with level irq */
            settings[gpio].debounced_time = make_timeout_time_us(500); // todo: optimize for higher level-interrupt speeds
        } else {
            settings[gpio].debounced_time = make_timeout_time_us(settings[gpio].debounce_us);
        }
        gpio_set_irq_enabled(gpio, events, false);
    }
}

void dln2_gpio_init(void)
{
    gpio_set_irq_callback(&dln2_gpio_irq_callback);
    irq_set_enabled(IO_IRQ_BANK0, true);
}
