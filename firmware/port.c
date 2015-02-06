#include "firmware.h"

typedef enum PortState {
    PORT_DISABLE,
    PORT_READ_CMD,
    PORT_READ_ARG,
    PORT_EXEC,
    PORT_EXEC_ASYNC,
} PortState;

typedef enum PortCmd {
    CMD_NOP = 0,
    CMD_FLUSH = 1,
    CMD_ECHO = 2,
    CMD_GPIO_IN = 3,
    CMD_GPIO_HIGH = 4,
    CMD_GPIO_LOW = 5,
} PortCmd;

typedef enum ExecStatus {
    EXEC_DONE = PORT_READ_CMD,
    EXEC_CONTINUE = PORT_EXEC,
    EXEC_ASYNC = PORT_EXEC_ASYNC,
} ExecStatus;

void port_step(PortData* p);

void port_init(PortData* p, u8 chan, const TesselPort* port) {
    memset(p, 0, sizeof(PortData));
    p->chan = chan;
    p->port = port;
    port_gpio_init(p->port);
    p->pending_out = true;
    bridge_start_out(p->chan, p->cmd_buf);
    p->state = PORT_READ_CMD;
}

bool port_cmd_has_arg(PortCmd cmd) {
    switch (cmd) {
        case CMD_NOP:
        case CMD_FLUSH:
            return false;

        // Commands that take a length argument:
        case CMD_ECHO:
            return true;

        // Commands that take a pin argument:
        case CMD_GPIO_IN:
        case CMD_GPIO_HIGH:
        case CMD_GPIO_LOW:
            return true;
    }
    invalid();
    return false;
}

u32 port_tx_len(PortData* p) {
    u32 size = p->arg;
    u32 cmd_remaining = p->cmd_len - p->cmd_pos;
    if (cmd_remaining < size) {
        size = cmd_remaining;
    }
    return size;
}

u32 port_rx_len(PortData* p) {
    u32 size = p->arg;
    u32 reply_remaining = BUF_SIZE - p->reply_len;
    if (reply_remaining < size) {
        size = reply_remaining;
    }
    return size;
}

u32 port_txrx_len(PortData *p) {
    u32 size = p->arg;
    u32 cmd_remaining = p->cmd_len - p->cmd_pos;
    if (cmd_remaining < size) {
        size = cmd_remaining;
    }
    u32 reply_remaining = BUF_SIZE - p->reply_len;
    if (reply_remaining < size) {
        size = reply_remaining;
    }
    return size;
}

Pin port_selected_pin(PortData* p) {
    return p->port->gpio[p->arg % 8];
}

void port_exec_async_complete(PortData* p, ExecStatus s) {
    if (p->state != PORT_EXEC_ASYNC) {
        invalid();
    }
    p->state = s;
    port_step(p);
}

ExecStatus port_begin_cmd(PortData *p) {
    switch (p->cmd) {
        case CMD_NOP:
            return EXEC_DONE;
        case CMD_ECHO:
            return EXEC_CONTINUE;
        case CMD_GPIO_IN:
            pin_in(port_selected_pin(p));
            return EXEC_DONE;
        case CMD_GPIO_HIGH:
            pin_high(port_selected_pin(p));
            pin_out(port_selected_pin(p));
            return EXEC_DONE;
        case CMD_GPIO_LOW:
            pin_low(port_selected_pin(p));
            pin_out(port_selected_pin(p));
            return EXEC_DONE;
    }
    invalid();
    return EXEC_DONE;
}

ExecStatus port_continue_cmd(PortData *p) {
    switch (p->cmd) {
        case CMD_NOP:
            return EXEC_DONE;
        case CMD_ECHO: {
            u32 size = port_txrx_len(p);
            memcpy(&p->reply_buf[p->reply_len], &p->cmd_buf[p->cmd_pos], size);
            p->reply_len += size;
            p->cmd_pos += size;
            p->arg -= size;
            return p->arg == 0 ? EXEC_DONE : EXEC_CONTINUE;
        }
        case CMD_GPIO_IN:
        case CMD_GPIO_HIGH:
        case CMD_GPIO_LOW:
            return EXEC_DONE;
    }
    invalid();
    return EXEC_DONE;
}

void port_step(PortData* p) {
    if (p->state == PORT_DISABLE || p->state == PORT_EXEC_ASYNC) {
        invalid();
        return;
    }

    while (1) {
        // If the command buffer has been processed, request a new one
        if (p->cmd_pos >= p->cmd_len && !p->pending_out) {
            p->pending_out = true;
            bridge_start_out(p->chan, p->cmd_buf);
        }
        // If the reply buffer is full, flush it.
        // Or, if there is any data and no commands, might as well flush.
        if ((p->reply_len >= BUF_SIZE || (p->pending_out && p->reply_len > 0)) && !p->pending_in) {
            p->pending_in = true;
            bridge_start_in(p->chan, p->reply_buf, p->reply_len);
        }

        // Wait for bridge transfers to complete;
        // TODO: multiple-buffer FIFO
        if (p->pending_in || p->pending_out) break;

        if (p->state == PORT_READ_CMD) {
            p->cmd = p->cmd_buf[p->cmd_pos++];
            if (port_cmd_has_arg(p->cmd)) {
                p->state = PORT_READ_ARG;
            } else {
                p->state = port_begin_cmd(p);
            }
        } else if (p->state == PORT_READ_ARG) {
            p->arg = p->cmd_buf[p->cmd_pos++];
            p->state = port_begin_cmd(p);
        } else if (p->state == PORT_EXEC) {
            p->state = port_continue_cmd(p);
        } else if (p->state == PORT_EXEC_ASYNC) {
            break;
        }
    }
}

void port_bridge_out_completion(PortData* p, u8 len) {
    p->pending_out = false;
    p->cmd_len = len;
    p->cmd_pos = 0;
    port_step(p);
}

void port_bridge_in_completion(PortData* p) {
    p->pending_in = false;
    p->reply_len = 0;
    port_step(p);
}