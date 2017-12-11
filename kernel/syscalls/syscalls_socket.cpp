// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <lib/user_copy/user_ptr.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/socket_dispatcher.h>

#include <zircon/syscalls/policy.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>

#include "syscalls_priv.h"

using fbl::AutoLock;

#define LOCAL_TRACE 0

zx_status_t sys_socket_create(uint32_t options, user_out_ptr<zx_handle_t> out0, user_out_ptr<zx_handle_t> out1) {
    LTRACEF("entry out_handles %p, %p\n", out0.get(), out1.get());

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryPolicy(ZX_POL_NEW_SOCKET);
    if (res != ZX_OK)
        return res;

    fbl::RefPtr<Dispatcher> socket0, socket1;
    zx_rights_t rights;
    zx_status_t result = SocketDispatcher::Create(options, &socket0, &socket1, &rights);
    if (result != ZX_OK)
        return result;

    HandleOwner h0(Handle::Make(fbl::move(socket0), rights));
    if (!h0)
        return ZX_ERR_NO_MEMORY;

    HandleOwner h1(Handle::Make(fbl::move(socket1), rights));
    if (!h1)
        return ZX_ERR_NO_MEMORY;

    zx_status_t status = out0.copy_to_user(up->MapHandleToValue(h0));
    if (status != ZX_OK)
        return status;

    status = out1.copy_to_user(up->MapHandleToValue(h1));
    if (status != ZX_OK)
        return status;

    up->AddHandle(fbl::move(h0));
    up->AddHandle(fbl::move(h1));

    return ZX_OK;
}

zx_status_t sys_socket_write(zx_handle_t handle, uint32_t options,
                             user_in_ptr<const void> buffer, size_t size,
                             user_out_ptr<size_t> actual) {
    LTRACEF("handle %x\n", handle);

    if ((size > 0u) && !buffer)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SocketDispatcher> socket;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &socket);
    if (status != ZX_OK)
        return status;

    size_t nwritten;
    switch (options) {
    case 0:
        status = socket->Write(buffer, size, &nwritten);
        break;
    case ZX_SOCKET_CONTROL:
        status = socket->WriteControl(buffer, size);
        if (status == ZX_OK)
            nwritten = size;
        break;
    case ZX_SOCKET_SHUTDOWN_WRITE:
    case ZX_SOCKET_SHUTDOWN_READ:
    case ZX_SOCKET_SHUTDOWN_READ | ZX_SOCKET_SHUTDOWN_WRITE:
        if (size == 0)
            return socket->Shutdown(options & ZX_SOCKET_SHUTDOWN_MASK);
        // fallthrough
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    // Caller may ignore results if desired.
    if (status == ZX_OK && actual)
        status = actual.copy_to_user(nwritten);

    return status;
}

zx_status_t sys_socket_read(zx_handle_t handle, uint32_t options,
                            user_out_ptr<void> buffer, size_t size,
                            user_out_ptr<size_t> actual) {
    LTRACEF("handle %x\n", handle);

    if (!buffer && size > 0)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SocketDispatcher> socket;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &socket);
    if (status != ZX_OK)
        return status;

    size_t nread;

    switch (options) {
    case 0:
        status = socket->Read(buffer, size, &nread);
        break;
    case ZX_SOCKET_CONTROL:
        status = socket->ReadControl(buffer, size, &nread);
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }

    // Caller may ignore results if desired.
    if (status == ZX_OK && actual)
        status = actual.copy_to_user(nread);

    return status;
}

zx_status_t sys_socket_share(zx_handle_t handle, zx_handle_t other) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SocketDispatcher> socket;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &socket);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<SocketDispatcher> other_socket;
    status = up->GetDispatcherWithRights(other, ZX_RIGHT_TRANSFER, &other_socket);
    if (status != ZX_OK)
        return status;

    status = socket->CheckShareable(other_socket.get());
    if (status != ZX_OK)
        return status;

    Handle* h = up->RemoveHandle(other).release();

    status = socket->Share(h);

    if (status != ZX_OK) {
        AutoLock lock(up->handle_table_lock());
        up->UndoRemoveHandleLocked(other);
        return status;
    }

    return ZX_OK;
}

zx_status_t sys_socket_accept(zx_handle_t handle, user_out_ptr<zx_handle_t> out) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<SocketDispatcher> socket;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &socket);
    if (status != ZX_OK)
        return status;

    HandleOwner outhandle;
    status = socket->Accept(&outhandle);
    if (status != ZX_OK)
        return status;

    status = out.copy_to_user(up->MapHandleToValue(outhandle));
    if (status != ZX_OK)
        return status;

    up->AddHandle(fbl::move(outhandle));

    return ZX_OK;
}
