// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/io_apic.h>
#include <hypervisor/uart.h>
#include <hypervisor/vcpu.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

/* UART configuration masks. */
static const uint8_t kUartInterruptIdNoFifoMask = bit_mask<uint8_t>(4);

void uart_init(uart_t* uart, const io_apic_t* io_apic) {
    memset(uart, 0, sizeof(*uart));
    cnd_init(&uart->ready_cnd);
    uart->io_apic = io_apic;
    uart->line_status = UART_LINE_STATUS_THR_EMPTY;
    uart->interrupt_id = UART_INTERRUPT_ID_NONE;
    uart->interrupt_enable = UART_INTERRUPT_ENABLE_NONE;
    uart->raise_interrupt = mx_vcpu_interrupt;
}

static mx_status_t try_raise_interrupt(uart_t* uart, uint8_t interrupt_id) {
    uint8_t vector = 0;
    mx_handle_t vcpu;
    mx_status_t status = io_apic_redirect(uart->io_apic, X86_INT_UART, &vector, &vcpu);
    if (status != MX_OK)
        return status;

    // UART IRQs overlap with CPU exception handlers, so they need to be
    // remapped. If that hasn't happened yet, don't fire the interrupt - it
    // would be bad.
    if (vector == 0)
        return MX_OK;

    uart->interrupt_id = interrupt_id;
    return uart->raise_interrupt(vcpu, vector);
}

// Checks whether an interrupt can successfully be raised. This is a
// convenience for the input thread that allows it to delay processing until
// the caller is ready. Others just always call try_raise_interrupt and hope.
static bool can_raise_interrupt(uart_t* uart) {
    uint8_t vector = 0;
    mx_handle_t vcpu;
    mx_status_t status = io_apic_redirect(uart->io_apic, X86_INT_UART, &vector, &vcpu);
    return status == MX_OK && vector != 0;
}

// Determines whether an interrupt needs to be raised and does so if necessary.
// Will not raise an interrupt if the interrupt_enable bit is not set.
static mx_status_t raise_next_interrupt(uart_t* uart) {
    cnd_signal(&uart->ready_cnd);
    if (uart->interrupt_id != UART_INTERRUPT_ID_NONE)
        // Don't wipe out a pending interrupt, just wait.
        return MX_OK;
    if (uart->interrupt_enable & UART_INTERRUPT_ENABLE_RDA &&
        uart->line_status & UART_LINE_STATUS_DATA_READY)
        return try_raise_interrupt(uart, UART_INTERRUPT_ID_RDA);
    if (uart->interrupt_enable & UART_INTERRUPT_ENABLE_THR_EMPTY &&
        uart->line_status & UART_LINE_STATUS_THR_EMPTY)
        return try_raise_interrupt(uart, UART_INTERRUPT_ID_THR_EMPTY);
    return MX_OK;
}

mx_status_t uart_read(uart_t* uart, uint16_t port, mx_vcpu_io_t* vcpu_io) {
    switch (port) {
    case UART_MODEM_CONTROL_PORT:
    case UART_MODEM_STATUS_PORT:
    case UART_SCR_SCRATCH_PORT:
        vcpu_io->access_size = 1;
        vcpu_io->u8 = 0;
        break;
    case UART_RECEIVE_PORT: {
        vcpu_io->access_size = 1;
        mtx_lock(&uart->mutex);
        vcpu_io->u8 = uart->rx_buffer;
        uart->rx_buffer = 0;
        uart->line_status = static_cast<uint8_t>(uart->line_status & ~UART_LINE_STATUS_DATA_READY);

        // Reset RDA interrupt on RBR read.
        if (uart->interrupt_id & UART_INTERRUPT_ID_RDA)
            uart->interrupt_id = UART_INTERRUPT_ID_NONE;

        mx_status_t status = raise_next_interrupt(uart);
        mtx_unlock(&uart->mutex);
        return status;
    }
    case UART_INTERRUPT_ENABLE_PORT:
        vcpu_io->access_size = 1;
        mtx_lock(&uart->mutex);
        vcpu_io->u8 = uart->interrupt_enable;
        mtx_unlock(&uart->mutex);
        break;
    case UART_INTERRUPT_ID_PORT:
        vcpu_io->access_size = 1;
        mtx_lock(&uart->mutex);
        vcpu_io->u8 = kUartInterruptIdNoFifoMask & uart->interrupt_id;

        // Reset THR empty interrupt on IIR read (or THR write).
        if (uart->interrupt_id & UART_INTERRUPT_ID_THR_EMPTY)
            uart->interrupt_id = UART_INTERRUPT_ID_NONE;
        mtx_unlock(&uart->mutex);
        break;
    case UART_LINE_CONTROL_PORT:
        vcpu_io->access_size = 1;
        mtx_lock(&uart->mutex);
        vcpu_io->u8 = uart->line_control;
        mtx_unlock(&uart->mutex);
        break;
    case UART_LINE_STATUS_PORT:
        vcpu_io->access_size = 1;
        mtx_lock(&uart->mutex);
        vcpu_io->u8 = uart->line_status;
        mtx_unlock(&uart->mutex);
        break;
    default:
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
}

static void flush_tx_buffer(uart_t* uart) {
    fprintf(stderr, "%.*s", uart->tx_offset, uart->tx_buffer);
    uart->tx_offset = 0;
}

mx_status_t uart_write(uart_t* uart, const mx_packet_guest_io_t* io) {
    switch (io->port) {
    case UART_RECEIVE_PORT: {
        mx_status_t status;

        mtx_lock(&uart->mutex);
        if (uart->line_control & UART_LINE_CONTROL_DIV_LATCH) {
            // Ignore writes when divisor latch is enabled.
            status = (io->access_size != 1) ? MX_ERR_IO_DATA_INTEGRITY : MX_OK;
        } else {
            for (int i = 0; i < io->access_size; i++) {
                uart->tx_buffer[uart->tx_offset++] = io->data[i];
                if (uart->tx_offset == UART_BUFFER_SIZE || io->data[i] == '\r') {
                    flush_tx_buffer(uart);
                }
            }
            uart->line_status |= UART_LINE_STATUS_THR_EMPTY;

            // Reset THR empty interrupt on THR write.
            if (uart->interrupt_id & UART_INTERRUPT_ID_THR_EMPTY)
                uart->interrupt_id = UART_INTERRUPT_ID_NONE;

            // TODO(andymutton): Raise interrupts asynchronously so that we don't overrun Linux's
            // interrupt flood check. Note: Implementing FIFOs will heavily mitigate this.
            status = raise_next_interrupt(uart);
        }
        mtx_unlock(&uart->mutex);
        return status;
    }
    case UART_INTERRUPT_ENABLE_PORT: {
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        mx_status_t status = MX_OK;
        mtx_lock(&uart->mutex);
        // Ignore writes when divisor latch is enabled.
        if (!(uart->line_control & UART_LINE_CONTROL_DIV_LATCH)) {
            uart->interrupt_enable = io->u8;
            // Flush output whenever RDA interrupt is enabled.
            if (uart->interrupt_enable & UART_INTERRUPT_ENABLE_RDA)
                flush_tx_buffer(uart);
            status = raise_next_interrupt(uart);
        }
        mtx_unlock(&uart->mutex);
        return status;
    }
    case UART_LINE_CONTROL_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        mtx_lock(&uart->mutex);
        uart->line_control = io->u8;
        mtx_unlock(&uart->mutex);
        return MX_OK;
    case UART_INTERRUPT_ID_PORT:
    case UART_MODEM_CONTROL_PORT ... UART_SCR_SCRATCH_PORT:
        return MX_OK;
    default:
        return MX_ERR_INTERNAL;
    }
}

static mx_status_t uart_handler(mx_port_packet_t* packet, void* ctx) {
    uart_t* uart = static_cast<uart_t*>(ctx);
    return uart_write(uart, &packet->guest_io);
}

mx_status_t uart_output_async(uart_t* uart, mx_handle_t guest) {
    const trap_args_t trap = {
        .kind = MX_GUEST_TRAP_IO,
        .addr = UART_RECEIVE_PORT,
        .len = 1,
        .key = 0,
    };
    return device_async(guest, &trap, 1, uart_handler, uart);
}

static int uart_input_loop(void* arg) {
    uart_t* uart = reinterpret_cast<uart_t*>(arg);

    mx_status_t status;
    do {
        mtx_lock(&uart->mutex);
        // Wait for a signal that the line is clear.
        // The locking here is okay, because we yield when we wait.
        while (!can_raise_interrupt(uart) && uart->line_status & UART_LINE_STATUS_DATA_READY)
            cnd_wait(&uart->ready_cnd, &uart->mutex);
        mtx_unlock(&uart->mutex);

        int pending_char = getchar();
        if (pending_char == EOF)
            status = MX_ERR_PEER_CLOSED;
        else {
            mtx_lock(&uart->mutex);
            uart->rx_buffer = static_cast<uint8_t>(pending_char);
            uart->line_status |= UART_LINE_STATUS_DATA_READY;
            status = raise_next_interrupt(uart);
            mtx_unlock(&uart->mutex);
        }
    } while (status == MX_OK);
    fprintf(stderr, "Stopped processing UART input (%d)\n", status);
    return status;
}

mx_status_t uart_input_async(uart_t* uart) {
    thrd_t uart_input_thread;
    int ret = thrd_create(&uart_input_thread, uart_input_loop, uart);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create UART input thread %d\n", ret);
        return MX_ERR_INTERNAL;
    }
    ret = thrd_detach(uart_input_thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach UART input thread %d\n", ret);
        return MX_ERR_INTERNAL;
    }
    return MX_OK;
}
